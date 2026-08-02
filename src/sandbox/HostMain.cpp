// EurydiceHelper — hosts one plugin out of process. The DAW streams audio
// through a SharedAudioRing and drives everything else over stdin/stdout
// JSON lines (one object per line, mirroring the control API's framing).
// If the plugin crashes, it takes this process down, not the DAW: the ring
// degrades to silence and the DAW's supervisor notices the death.
//
//   EurydiceHelper --ring <name> [--test-gain]
//
// --test-gain skips plugin hosting and multiplies the input by 0.5 — the
// mode integration tests use to prove the transport without a plugin DB.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>
#include <thread>
#include "SharedAudioRing.h"
#include "plugins/PluginManager.h"

namespace
{
juce::var parseLine (const juce::String& line)
{
    return juce::JSON::parse (line);
}

void reply (const juce::var& payload)
{
    std::cout << juce::JSON::toString (payload, true) << "\n" << std::flush;
}

juce::var okReply (std::initializer_list<std::pair<juce::String, juce::var>> fields = {})
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("ok", true);
    for (const auto& [key, value] : fields)
        obj->setProperty (key, value);
    return juce::var (obj);
}

juce::var errorReply (const juce::String& message)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("ok", false);
    obj->setProperty ("error", message);
    return juce::var (obj);
}
}

class HelperApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "EurydiceHelper"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override           { return true; }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        const int ringArg = args.indexOf ("--ring");
        testGain = args.contains ("--test-gain");

        if (ringArg < 0 || ringArg + 1 >= args.size() || ! ring.open (args[ringArg + 1]))
        {
            std::cerr << "EurydiceHelper: cannot open ring\n";
            setApplicationReturnValue (1);
            quit();
            return;
        }

        // The DAW owns this process's lifetime through stdin: EOF = shut down.
        stdinThread = std::thread ([this] { readCommands(); });

        audioThread = std::thread ([this] { audioLoop(); });
        reply (okReply ({ { "event", "ready" } }));
    }

    void shutdown() override
    {
        running.store (false);
        ring.kick();
        if (audioThread.joinable())
            audioThread.join();
        if (stdinThread.joinable())
            stdinThread.detach();   // blocked in getline; dies with the process
        editorWindow = nullptr;
        if (instance != nullptr)
        {
            if (prepared)
                instance->releaseResources();
            instance = nullptr;
        }
    }

private:
    // ---- control (stdin reader thread -> message thread) ----

    void readCommands()
    {
        std::string raw;
        while (std::getline (std::cin, raw))
        {
            const auto line = juce::String::fromUTF8 (raw.c_str());
            juce::MessageManager::callAsync ([this, line] { handleCommand (parseLine (line)); });
        }
        // DAW went away (or closed us deliberately): exit cleanly.
        juce::MessageManager::callAsync ([this] { quit(); });
    }

    void handleCommand (const juce::var& command)
    {
        const auto cmd = command["cmd"].toString();

        if (cmd == "ping")
        {
            reply (okReply ({ { "heartbeat", (juce::int64) ring.header()->heartbeat.load() } }));
        }
        else if (cmd == "load")
        {
            loadPlugin (command["pluginId"].toString(),
                        (double) command["sampleRate"],
                        (int) command["blockSize"]);
        }
        else if (cmd == "prepare")
        {
            prepare ((double) command["sampleRate"], (int) command["blockSize"]);
            reply (okReply());
        }
        else if (cmd == "getState")
        {
            juce::MemoryBlock state;
            if (instance != nullptr)
                instance->getStateInformation (state);
            reply (okReply ({ { "state", state.toBase64Encoding() } }));
        }
        else if (cmd == "setState")
        {
            juce::MemoryBlock state;
            if (instance != nullptr && state.fromBase64Encoding (command["state"].toString())
                && state.getSize() > 0)
                instance->setStateInformation (state.getData(), (int) state.getSize());
            reply (okReply());
        }
        else if (cmd == "editor")
        {
            showEditor (command["title"].toString());
            reply (okReply());
        }
        else if (cmd == "quit")
        {
            reply (okReply());
            quit();
        }
        else
        {
            reply (errorReply ("unknown cmd: " + cmd));
        }
    }

    void loadPlugin (const juce::String& pluginId, double sampleRate, int blockSize)
    {
        if (testGain)
        {
            reply (okReply ({ { "name", "test-gain" } }));
            return;
        }

        const auto desc = plugins.findByIdentifier (pluginId);
        if (! desc)
        {
            reply (errorReply ("unknown pluginId"));
            return;
        }

        plugins.createInstance (*desc, sampleRate, blockSize,
            [this, sampleRate, blockSize] (std::unique_ptr<juce::AudioPluginInstance> created,
                                           const juce::String& error)
            {
                if (created == nullptr)
                {
                    reply (errorReply (error.isNotEmpty() ? error : "load failed"));
                    return;
                }
                instance = std::move (created);
                isInstrument = instance->acceptsMidi();
                prepare (sampleRate, blockSize);
                juce::Array<juce::var> paramNames;
                const auto& params = instance->getParameters();
                for (int i = 0; i < juce::jmin (params.size(), 128); ++i)
                    paramNames.add (params[i]->getName (48));
                reply (okReply ({ { "name", instance->getName() },
                                  { "instrument", isInstrument.load() },
                                  { "params", juce::var (paramNames) } }));
            });
    }

    void prepare (double sampleRate, int blockSize)
    {
        processingEnabled.store (false);
        // The audio thread checks the flag per block, so a short handshake
        // window is enough for it to fall out of process().
        std::this_thread::sleep_for (std::chrono::milliseconds (5));

        ring.header()->sampleRate.store ((juce::int32) sampleRate);
        ring.header()->blockSize.store ((juce::int32) juce::jmin (blockSize,
                                                                  sandbox::SharedAudioRing::maxBlock));
        if (instance != nullptr)
        {
            if (prepared)
                instance->releaseResources();
            instance->enableAllBuses();
            instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
            instance->prepareToPlay (sampleRate, blockSize);
            prepared = true;
        }
        scratch.setSize (juce::jmax (2, instance != nullptr
                                            ? juce::jmax (instance->getTotalNumInputChannels(),
                                                          instance->getTotalNumOutputChannels())
                                            : 2),
                         juce::jmax (32, blockSize));
        processingEnabled.store (true);
    }

    void showEditor (const juce::String& title)
    {
        if (instance == nullptr)
            return;
        if (editorWindow != nullptr)
        {
            editorWindow->toFront (true);
            return;
        }
        juce::Component* editor = instance->hasEditor()
                                      ? instance->createEditorIfNeeded()
                                      : new juce::GenericAudioProcessorEditor (*instance);
        editorWindow = std::make_unique<EditorWindow> (*this, title, editor);
    }

    // ---- audio (dedicated thread; the plugin's "audio thread") ----

    void audioLoop()
    {
        juce::int64 lastSeq = -1;
        while (running.load())
        {
            const auto seq = ring.waitForInput();
            if (! running.load() || seq < 0 || seq == lastSeq)
                continue;
            lastSeq = seq;

            auto* head = ring.header();
            const int numSamples = juce::jmin ((int) head->inputLen[seq & 1].load(),
                                               sandbox::SharedAudioRing::maxBlock);
            if (numSamples <= 0 || ! processingEnabled.load())
            {
                // Pass through silence-of-input so the DAW still advances.
                for (int ch = 0; ch < sandbox::RingHeader::maxChannels; ++ch)
                    std::memcpy (ring.outputSlot (seq, ch), ring.inputSlot (seq, ch),
                                 (size_t) juce::jmax (0, numSamples) * sizeof (float));
                ring.publishOutput (seq);
                continue;
            }

            scratch.clear();
            for (int ch = 0; ch < sandbox::RingHeader::maxChannels; ++ch)
                scratch.copyFrom (ch % scratch.getNumChannels(), 0,
                                  ring.inputSlot (seq, ch), numSamples);

            if (testGain)
            {
                scratch.applyGain (0, numSamples, 0.5f);
            }
            else if (instance != nullptr && prepared)
            {
                applyPendingParams();
                midiScratch.clear();
                if (isInstrument)
                {
                    const auto slot = (size_t) (seq & 1);
                    const int midiCount = juce::jlimit (0, sandbox::RingHeader::maxMidiEvents,
                                                        (int) head->midiCount[slot]);
                    for (int i = 0; i < midiCount; ++i)
                    {
                        const auto& event = head->midiEvents[slot][(size_t) i];
                        midiScratch.addEvent (event.data, event.size, event.offset);
                    }
                }
                juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                               scratch.getNumChannels(), 0, numSamples);
                instance->processBlock (view, midiScratch);
            }

            for (int ch = 0; ch < sandbox::RingHeader::maxChannels; ++ch)
                std::memcpy (ring.outputSlot (seq, ch),
                             scratch.getReadPointer (ch % scratch.getNumChannels()),
                             (size_t) numSamples * sizeof (float));
            ring.publishOutput (seq);
        }
    }

    void applyPendingParams()
    {
        auto* head = ring.header();
        const int count = juce::jlimit (0, sandbox::RingHeader::maxParamEvents,
                                        (int) head->paramCount.exchange (0));
        if (count == 0 || instance == nullptr)
            return;
        const auto& params = instance->getParameters();
        for (int i = 0; i < count; ++i)
        {
            const auto event = head->paramEvents[i];
            if (event.index >= 0 && event.index < params.size())
                params[event.index]->setValueNotifyingHost (event.value);
        }
    }

    struct EditorWindow : juce::DocumentWindow
    {
        EditorWindow (HelperApp& ownerRef, const juce::String& title, juce::Component* editor)
            : juce::DocumentWindow (title, juce::Colours::darkgrey, closeButton),
              owner (ownerRef)
        {
            setUsingNativeTitleBar (true);
            setContentNonOwned (editor, true);
            ownedEditor.reset (editor);
            setResizable (true, false);
            centreWithSize (juce::jmax (300, editor->getWidth()),
                            juce::jmax (150, editor->getHeight()));
            setVisible (true);
        }

        ~EditorWindow() override
        {
            clearContentComponent();
            if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (ownedEditor.get()))
                if (owner.instance != nullptr)
                    owner.instance->editorBeingDeleted (editor);
            ownedEditor = nullptr;
        }

        void closeButtonPressed() override
        {
            auto* ownerPtr = &owner;
            juce::MessageManager::callAsync ([ownerPtr] { ownerPtr->editorWindow = nullptr; });
        }

        HelperApp& owner;
        std::unique_ptr<juce::Component> ownedEditor;
    };

    sandbox::SharedAudioRing ring;
    PluginManager plugins;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<EditorWindow> editorWindow;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midiScratch;
    std::thread stdinThread, audioThread;
    std::atomic<bool> running { true };
    std::atomic<bool> processingEnabled { false };
    bool prepared = false;
    bool testGain = false;
    std::atomic<bool> isInstrument { false };
};

START_JUCE_APPLICATION (HelperApp)
