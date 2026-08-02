#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace fx
{
// Fixed-capacity multi-channel circular delay line. All storage is claimed in
// prepare(); write/read/advance are allocation-free.
// Usage per sample: write() every channel, read() every channel, advance().
class DelayLine
{
public:
    void prepare (int numChannels, int maxDelaySamples)
    {
        capacity = juce::jmax (4, maxDelaySamples + 4);
        buffer.setSize (juce::jmax (1, numChannels), capacity);
        reset();
    }

    void reset()
    {
        buffer.clear();
        writePos = 0;
    }

    void write (int channel, float value) noexcept
    {
        buffer.getWritePointer (channel)[writePos] = value;
    }

    void advance() noexcept
    {
        if (++writePos >= capacity)
            writePos = 0;
    }

    // delaySamples == 0 returns the sample just written.
    float read (int channel, float delaySamples) const noexcept
    {
        const float d = juce::jlimit (0.0f, (float) (capacity - 2), delaySamples);
        const int whole = (int) d;
        const float frac = d - (float) whole;

        int p0 = writePos - whole;
        if (p0 < 0)
            p0 += capacity;
        int p1 = p0 - 1;
        if (p1 < 0)
            p1 += capacity;

        const auto* data = buffer.getReadPointer (channel);
        return data[p0] + frac * (data[p1] - data[p0]);
    }

    int getCapacity() const noexcept { return capacity; }

private:
    juce::AudioBuffer<float> buffer;
    int capacity = 0;
    int writePos = 0;
};
} // namespace fx
