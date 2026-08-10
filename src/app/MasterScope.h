#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>

// Header strip: log-frequency spectrum of the master bus ("EQ scope") with a
// 0 dBFS redline. Content that crosses the line is drawn red and leaves a
// fading clip mark at its frequency, so overs are attributable after the
// fact. Engine-agnostic through std::function hooks, like TransportBar.
// Right-click for options; the owner persists them via onOptionsChanged.
class MasterScope : public juce::Component,
                    public juce::SettableTooltipClient,
                    private juce::Timer
{
public:
    struct Options
    {
        int fftOrder = 11;          // 11..13 -> 2048/4096/8192-point FFT
        int rangeDb = 90;           // scope floor below 0 dBFS: 60 / 90 / 120
        int decayDbPerSecond = 45;  // trace fall: 90 fast / 45 medium / 20 slow
        bool fill = true;           // shade under the curve
        bool peakHold = true;       // slow-falling outline of recent maxima
    };

    MasterScope();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    Options getOptions() const { return options; }
    void setOptions (const Options&);   // sanitises; does not fire onOptionsChanged
    std::function<void (const Options&)> onOptionsChanged;

    // Polled at UI rate.
    std::function<int (float*, float*, int)> pullSamples;   // drains the master tap
    std::function<double()> getSampleRate;

    // One analysis step, identical to a timer tick; lets tests drive the scope.
    void refreshNow();

    // Smoothed spectrum level / clip-mark strength at the bin nearest a
    // frequency. For tests.
    float spectrumDbAt (double hz) const;
    float clipMarkAt (double hz) const;

    static constexpr int preferredWidth = 300;
    static constexpr int minimumWidth   = 140;

private:
    void timerCallback() override { refreshNow(); }
    void rebuildFft();
    void analyse();
    void applyOptions (const Options&);   // setOptions + notify owner
    void showOptionsMenu();
    void clearClipMarks();
    int binForFreq (double hz) const;
    float yForDb (float db, juce::Rectangle<float> area) const;

    Options options;
    bool frozen = false;
    double sampleRate = 44100.0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;      // Hann, fftSize
    std::vector<float> history;     // mono ring of the last fftSize samples
    int historyWrite = 0;
    std::vector<float> fftData;     // 2 * fftSize scratch
    std::vector<float> binDb;       // displayed dB per bin, decays per tick
    std::vector<float> holdDb;      // slow-falling peak trace
    std::vector<float> clipMark;    // 0..1 fading redline marks per bin
    std::vector<float> pullL, pullR;

    static constexpr float headroomDb = 6.0f;   // shown above the redline
    static constexpr float floorDb = -160.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterScope)
};
