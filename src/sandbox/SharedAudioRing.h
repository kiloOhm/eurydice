#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <unistd.h>

// Shared-memory audio transport between the DAW and a plugin-host helper
// process. Deliberately asynchronous with one block of latency so the DAW's
// audio thread NEVER blocks on the child:
//
//   DAW block N:  write input slot (N & 1) -> publish inputSeq = N
//                 -> sem_post (wakes the child)
//                 -> if outputSeq >= N-1, read output slot ((N-1) & 1);
//                    else output silence and count an overrun.
//   child loop:   sem_wait -> read newest inputSeq -> process through the
//                 plugin -> write output slot (seq & 1) -> outputSeq = seq.
//
// Double-buffered slots plus the one-block lag mean reader and writer never
// touch the same slot at the same time as long as the child keeps up; when it
// does not (or is dead), the DAW hears silence rather than a glitch or a hang.
// A dead child is additionally detected by process supervision — the ring
// itself only ever degrades to silence.
namespace sandbox
{
struct RingHeader
{
    static constexpr juce::uint32 magicExpected = 0x45555239;   // "EUR9"

    std::atomic<juce::uint32> magic;
    std::atomic<juce::int64> inputSeq;      // last input block published by the DAW
    std::atomic<juce::int64> outputSeq;     // last output block completed by the child
    std::atomic<juce::int64> heartbeat;     // child increments per processed block
    std::atomic<juce::int32> sampleRate;
    std::atomic<juce::int32> blockSize;     // maximum samples per block slot
    std::atomic<juce::int32> inputLen[2];   // actual samples in each input slot
                                            // (engine blocks vary: loop splits)
    std::atomic<juce::int32> paramCount;    // param events pending for the next block

    static constexpr int maxChannels = 2;
    static constexpr int maxParamEvents = 64;

    struct ParamEvent { juce::int32 index; float value; };
    ParamEvent paramEvents[maxParamEvents];
};

class SharedAudioRing
{
public:
    static constexpr int maxBlock = 4096;

    // Total mapping: header + 2 slots of input + 2 slots of output.
    static size_t mappingSize()
    {
        return sizeof (RingHeader)
             + (size_t) 4 * RingHeader::maxChannels * maxBlock * sizeof (float);
    }

    SharedAudioRing() = default;
    ~SharedAudioRing() { close(); }

    // DAW side: create and own the objects. Names must be short (macOS caps
    // POSIX names at 31 chars) and unique per instance.
    bool create (const juce::String& name)
    {
        shmName = "/" + name;
        semName = "/" + name + "s";
        owner = true;

        ::shm_unlink (shmName.toRawUTF8());
        ::sem_unlink (semName.toRawUTF8());

        fd = ::shm_open (shmName.toRawUTF8(), O_CREAT | O_RDWR | O_EXCL, 0600);
        if (fd < 0)
            return false;
        if (::ftruncate (fd, (off_t) mappingSize()) != 0)
            return false;
        if (! map())
            return false;

        new (base) RingHeader();
        // -1 = "nothing published yet"; zero-init would make block 0 look
        // ready before the child ever ran.
        header()->inputSeq.store (-1);
        header()->outputSeq.store (-1);
        header()->magic.store (RingHeader::magicExpected);

        sem = ::sem_open (semName.toRawUTF8(), O_CREAT | O_EXCL, 0600, 0);
        return sem != SEM_FAILED;
    }

    // Child side: open what the DAW created.
    bool open (const juce::String& name)
    {
        shmName = "/" + name;
        semName = "/" + name + "s";
        owner = false;

        fd = ::shm_open (shmName.toRawUTF8(), O_RDWR, 0600);
        if (fd < 0)
            return false;
        if (! map())
            return false;
        if (header()->magic.load() != RingHeader::magicExpected)
            return false;

        sem = ::sem_open (semName.toRawUTF8(), 0);
        return sem != SEM_FAILED;
    }

    bool isOpen() const { return base != nullptr && sem != SEM_FAILED && sem != nullptr; }

    RingHeader* header() const { return reinterpret_cast<RingHeader*> (base); }

    float* inputSlot (juce::int64 seq, int channel) const
    {
        return slotBase (0, seq) + (size_t) channel * maxBlock;
    }

    float* outputSlot (juce::int64 seq, int channel) const
    {
        return slotBase (2, seq) + (size_t) channel * maxBlock;
    }

    // ---- DAW side (audio thread; syscall-free except the sem_post) ----

    // Writes the block and publishes it. numSamples <= blockSize <= maxBlock.
    void publishInput (juce::int64 seq, const float* const* channels, int numChannels, int numSamples)
    {
        for (int ch = 0; ch < RingHeader::maxChannels; ++ch)
        {
            float* dest = inputSlot (seq, ch);
            const float* src = ch < numChannels ? channels[ch] : channels[0];
            std::memcpy (dest, src, (size_t) numSamples * sizeof (float));
        }
        header()->inputLen[seq & 1].store (numSamples, std::memory_order_relaxed);
        header()->inputSeq.store (seq, std::memory_order_release);
        ::sem_post (sem);
    }

    // Copies the child's output for `seq` if it is ready; returns false (and
    // leaves the destination untouched) when the child is behind or gone.
    bool readOutput (juce::int64 seq, float* const* channels, int numChannels, int numSamples) const
    {
        if (seq < 0 || header()->outputSeq.load (std::memory_order_acquire) < seq)
            return false;
        for (int ch = 0; ch < numChannels; ++ch)
            std::memcpy (channels[ch], outputSlot (seq, ch % RingHeader::maxChannels),
                         (size_t) numSamples * sizeof (float));
        return true;
    }

    // ---- child side ----

    // Blocks until the DAW posts; returns the newest published sequence.
    juce::int64 waitForInput()
    {
        while (::sem_wait (sem) != 0)
        {
            if (errno != EINTR)
                return -1;
        }
        return header()->inputSeq.load (std::memory_order_acquire);
    }

    void publishOutput (juce::int64 seq)
    {
        header()->outputSeq.store (seq, std::memory_order_release);
        header()->heartbeat.fetch_add (1, std::memory_order_relaxed);
    }

    // Unblocks a child sitting in waitForInput (used on shutdown).
    void kick() { if (isOpen()) ::sem_post (sem); }

    void close()
    {
        if (base != nullptr)
        {
            ::munmap (base, mappingSize());
            base = nullptr;
        }
        if (fd >= 0)
        {
            ::close (fd);
            fd = -1;
        }
        if (sem != nullptr && sem != SEM_FAILED)
        {
            ::sem_close (sem);
            sem = nullptr;
        }
        if (owner)
        {
            ::shm_unlink (shmName.toRawUTF8());
            ::sem_unlink (semName.toRawUTF8());
        }
    }

private:
    bool map()
    {
        base = ::mmap (nullptr, mappingSize(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base == MAP_FAILED)
        {
            base = nullptr;
            return false;
        }
        return true;
    }

    float* slotBase (int region, juce::int64 seq) const
    {
        auto* floats = reinterpret_cast<float*> (static_cast<char*> (base) + sizeof (RingHeader));
        const auto slot = (size_t) region + (size_t) (seq & 1);
        return floats + slot * RingHeader::maxChannels * maxBlock;
    }

    juce::String shmName, semName;
    void* base = nullptr;
    int fd = -1;
    sem_t* sem = nullptr;
    bool owner = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SharedAudioRing)
};
} // namespace sandbox
