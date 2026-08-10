#pragma once

#include <array>
#include <atomic>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Generator.h"

// FPC-style drum machine: a bank of one-shot sample pads, each mapped to a
// MIDI note, with per-pad gain, pan, tune and choke group. Notes that match no
// pad are ignored; several pads on the same note layer. Note-offs are ignored
// — pads always play out (or get choked). Sample loading happens on the
// message thread; the audio thread only reads via shared_ptr.
class DrumMachineGenerator : public Generator
{
public:
    static constexpr int maxPads = 64;
    static constexpr int maxVoices = 32;

    struct PadParams
    {
        std::atomic<int>   key { -1 };       // -1 = unassigned
        std::atomic<float> gain { 0.9f };
        std::atomic<float> pan { 0.0f };     // -1..1
        std::atomic<float> tune { 0.0f };    // semitones
        std::atomic<int>   choke { 0 };      // 0 = off, 1..8
    };

    DrumMachineGenerator() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) override;
    void reset() override;

    // --- message thread ---
    void setNumPads (int n) { numPads.store (juce::jlimit (0, maxPads, n)); }
    int getNumPads() const  { return numPads.load(); }
    PadParams& padParams (int pad) { return pads[(size_t) pad].params; }

    // Points a pad at a sample file, a synthesised drum ("kick"/"snare"/
    // "clap"/"hat"), or nothing. Only reloads when the source actually
    // changed, so it is safe to call on every snapshot rebuild.
    void setPadSource (int pad, const juce::String& samplePath, const juce::String& synthKind);

    bool padHasSample (int pad) const;
    double getPadLengthSeconds (int pad) const;

    // Bumped on every pad trigger (any thread source: pattern, preview,
    // live MIDI). The editor polls it to flash pads.
    std::uint32_t getTriggerCount (int pad) const
    {
        return pads[(size_t) pad].triggerCount.load (std::memory_order_relaxed);
    }

private:
    struct Sample
    {
        juce::AudioBuffer<float> data;   // always stereo
        double sourceSampleRate = 44100.0;
    };

    struct Pad
    {
        PadParams params;
        std::shared_ptr<const Sample> sample;   // guarded by sampleLock
        juce::String source;                    // path or "synth:kind"; message thread
        std::atomic<std::uint32_t> triggerCount { 0 };
    };

    struct Voice
    {
        bool active = false;
        int padIndex = -1;
        double pos = 0.0;
        double rate = 1.0;
        float gainL = 0.0f, gainR = 0.0f;
        bool choked = false;
        float fade = 1.0f;
        std::shared_ptr<const Sample> sample;   // voice keeps its sample alive
    };

    void triggerNote (int key, float velocity);
    void renderSegment (juce::AudioBuffer<float>& out, int from, int to);
    std::shared_ptr<const Sample> getPadSample (int pad) const;
    void storePadSample (int pad, std::shared_ptr<const Sample>);

    std::array<Pad, maxPads> pads;
    std::array<Voice, maxVoices> voices;
    std::atomic<int> numPads { 0 };

    mutable juce::SpinLock sampleLock;
    double deviceSampleRate = 44100.0;
    float chokeFadeCoef = 0.99f;   // ~3 ms time constant, set in prepare
    float pendingPan = 0.0f;       // CC10 latch, consumed by the next note-on

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumMachineGenerator)
};
