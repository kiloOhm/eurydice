#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <deque>
#include "EngineSnapshot.h"

// Real-time playback engine. Audio thread reads an immutable EngineSnapshot,
// generates MIDI from pattern/playlist data, renders generators, and mixes
// through the insert buses. No locks (beyond a try-lock handoff), no
// allocation, no model access on the audio thread.
class AudioEngine : private juce::AudioIODeviceCallback,
                    private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::String initialise();   // returns error string if device open failed
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    // The app starts output-only (see initialise); recording turns input on.
    // Returns an error string if the device could not be reopened.
    juce::String setInputEnabled (bool enabled);
    bool isInputEnabled() const;

    // Message thread: publish a freshly built snapshot.
    void publishSnapshot (std::shared_ptr<const EngineSnapshot>);

    // Latest published (possibly not yet adopted) snapshot; for tests/tools.
    std::shared_ptr<const EngineSnapshot> getPendingSnapshot() const
    {
        const juce::SpinLock::ScopedLockType sl (pendingLock);
        return pendingSnapshot;
    }

    // Transport control (any thread).
    void play();
    // Clicks the count-in bars before the transport starts moving.
    void playWithCountIn();
    void stop();
    void pausePlayback() { playing.store (false); }   // no rewind, voices ring out
    void togglePlayStop();
    void setPositionTicks (double ticks);

    // Preview a note outside the sequencer (piano roll clicks, typing keys,
    // browser preview). durationMs <= 0 means "until previewNoteOff".
    void previewNote (int channelId, int key, float velocity, int durationMs);
    void previewNoteOff (int channelId, int key);

    // --- metronome / count-in (any thread) ---
    // The click is synthesised, mixed straight into the hardware output after
    // the master bus, so it never reaches the mixer, the meters or a stem.
    void setMetronomeEnabled (bool on) { metronomeEnabled.store (on); }
    bool isMetronomeEnabled() const    { return metronomeEnabled.load(); }
    void setMetronomeLevel (float gain){ metronomeLevel.store (juce::jlimit (0.0f, 1.0f, gain)); }
    float getMetronomeLevel() const    { return metronomeLevel.load(); }
    void setCountInBars (int bars)     { countInBars.store (juce::jlimit (0, 2, bars)); }
    int  getCountInBars() const        { return countInBars.load(); }
    bool isCountingIn() const noexcept { return countingIn.load(); }

    bool   isPlaying() const noexcept        { return playing.load(); }
    double getPositionTicks() const noexcept { return publishedTickPos.load(); }
    double getPositionBeats() const noexcept { return publishedTickPos.load() / 960.0; }
    float  getMasterPeak (int channel) const noexcept { return masterPeak[channel & 1].load(); }
    float  getInsertPeak (int insertIndex, int channel) const noexcept
    {
        return insertPeaks[(size_t) juce::jlimit (0, maxInserts - 1, insertIndex) * 2
                           + (size_t) (channel & 1)].load (std::memory_order_relaxed);
    }
    double getCpuLoad() const                { return deviceManager.getCpuUsage(); }

    // --- audio-input recording ---
    // Message thread hands over a prepared ThreadedWriter; the callback feeds
    // input samples into it while set. Ownership stays with the caller.
    void setRecorder (juce::AudioFormatWriter::ThreadedWriter* writer) { recorder.store (writer); }

    // --- offline rendering (call only while detached from the device) ---
    void detachFromDevice()   { deviceManager.removeAudioCallback (this); }
    void reattachToDevice()   { deviceManager.addAudioCallback (this); }
    // Ignores the project's loop range without touching it, so a render plays
    // the arrangement through to its end even with a loop armed.
    void setLoopBypassed (bool on) { loopBypassed.store (on); }
    void prepareOffline (double offlineSampleRate, int offlineBlockSize)
    {
        prepareInternal (offlineSampleRate, offlineBlockSize);
    }
    void processBlockOffline (float* const* outs, int numOuts, int numSamples)
    {
        audioDeviceIOCallbackWithContext (nullptr, 0, outs, numOuts, numSamples, {});
    }
    const juce::AudioBuffer<float>& getInsertBusForStem (int index) const
    {
        return insertBus[(size_t) juce::jlimit (0, maxInserts - 1, index)];
    }
    // Per-channel isolation for stem rendering: each channel's own output at
    // its rack volume/pan, before it reaches an insert. Off by default because
    // it costs a clear + copy per channel per block.
    void setChannelStemCapture (bool on) { channelStemCapture = on; }
    const juce::AudioBuffer<float>& getChannelBusForStem (int index) const
    {
        return channelStemBus[(size_t) juce::jlimit (0, maxChannels - 1, index)];
    }
    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept     { return blockSize; }

    static constexpr int maxChannels = 128;
    static constexpr int maxInserts  = 33;   // master + 32
    static constexpr int maxActiveNotes = 512;

private:
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void prepareInternal (double newSampleRate, int newBlockSize);
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;

    void timerCallback() override;   // snapshot GC + housekeeping

    // Sequencing helpers (audio thread)
    void generateMidiForRange (const EngineSnapshot&, double t0, double t1, double tps);
    void emitPatternSegment (const EngineSnapshot&, const PatternSnapshot&,
                             double segStart, double segEnd, double blockStartTick,
                             int tickOffsetToSong, double tps);
    void addNoteOn (int channelIndex, int key, float velocity, int sampleOffset, double offTick);
    void scheduleClicks (double t0, double t1, double tps, int sampleBase);
    void renderMetronome (float* const* outs, int numOuts, int numSamples);
    void flushNoteOffs (const EngineSnapshot&, double t0, double t1, double tps);
    void releaseActiveNotes (const EngineSnapshot&, int sampleOffset);
    void allNotesOff (const EngineSnapshot&);
    void mixAudioClips (const EngineSnapshot&, double t0, double t1, double tps,
                        int startSample, int numChunkSamples);

    juce::AudioDeviceManager deviceManager;

    // --- snapshot handoff ---
    mutable juce::SpinLock pendingLock;
    std::shared_ptr<const EngineSnapshot> pendingSnapshot;      // guarded by pendingLock
    std::atomic<bool> snapshotDirty { false };
    std::shared_ptr<const EngineSnapshot> currentSnapshot;      // audio thread only
    std::atomic<uint64_t> audioGeneration { 0 };
    uint64_t pendingGeneration = 0;                             // message thread
    std::deque<std::pair<uint64_t, std::shared_ptr<const EngineSnapshot>>> history;  // message thread

    // --- transport ---
    std::atomic<bool> playing { false };
    std::atomic<double> publishedTickPos { 0.0 };
    std::atomic<double> seekRequest { -1.0 };
    std::atomic<bool> stopRequest { false };
    std::atomic<bool> loopBypassed { false };
    double tickPos = 0.0;                                       // audio thread

    // --- metronome / count-in ---
    std::atomic<bool>  metronomeEnabled { false };
    std::atomic<float> metronomeLevel { 0.5f };
    std::atomic<int>   countInBars { 0 };
    std::atomic<bool>  countInRequest { false };
    std::atomic<bool>  countingIn { false };
    double countInTicksLeft = 0.0;      // audio thread
    double countInTick = 0.0;           // audio thread: ticks since the count-in began
    // One click voice: at any tempo a beat is far longer than a block, so a
    // retrigger can never cut a still-sounding click short.
    double clickPhase = 0.0, clickPhaseDelta = 0.0, clickEnv = 0.0, clickDecay = 0.0;
    int    clickStartSample = -1;       // set while sequencing, consumed at mix time

    // --- audio-thread scratch state ---
    double sampleRate = 44100.0;
    int blockSize = 512;
    int currentBlockSamples = 0;   // numSamples of the block being processed
    int blockSampleBase = 0;       // first sample of the sub-range being sequenced
    juce::AudioBuffer<float> channelScratch;
    std::vector<juce::AudioBuffer<float>> insertBus;            // sized maxInserts in prepare
    std::vector<juce::AudioBuffer<float>> channelStemBus;       // sized maxChannels in prepare
    std::vector<juce::MidiBuffer> channelMidi;                  // sized maxChannels in prepare
    bool channelStemCapture = false;   // only flipped while detached from the device

    struct ActiveNote { int channelIndex = -1; int key = 0; double offTick = 0.0; };
    std::array<ActiveNote, maxActiveNotes> activeNotes;

    // --- preview notes (lock-free queue from message thread) ---
    struct PreviewEvent { int channelId = 0; int key = 0; float velocity = 1.0f;
                          int durationSamples = 0; bool isOn = true; };
    juce::SpinLock previewWriteLock;
    juce::AbstractFifo previewFifo { 64 };
    std::array<PreviewEvent, 64> previewQueue;
    struct PreviewActive { int channelIndex = -1; int channelId = 0; int key = 0; int samplesLeft = 0; };
    std::array<PreviewActive, 32> previewActive;
    void processPreviewEvents (const EngineSnapshot&, int numSamples);

    std::atomic<float> masterPeak[2] { 0.0f, 0.0f };
    std::array<std::atomic<float>, (size_t) maxInserts * 2> insertPeaks {};
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> recorder { nullptr };

    // --- per-block automation overrides (sentinel: < -100 means "none") ---
    std::array<float, maxChannels> channelVolAuto {}, channelPanAuto {};
    std::array<float, maxInserts>  insertVolAuto {},  insertPanAuto {};
    void applyAutomation (const EngineSnapshot&, double tick);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
