#pragma once

#include <functional>

// macOS microphone permission. JUCE's CoreAudio backend never asks: the HAL
// throws up the TCC prompt from *inside* the blocking device open, so the
// first record-arm froze all audio (output included) until the user found
// and answered the dialog. Asking here first keeps the output-only device
// running while the prompt is up.
namespace micpermission
{
enum class State { granted, denied, undetermined };

State current();

// Prompts if undetermined, otherwise answers immediately.
// The callback is always delivered on the message thread.
void request (std::function<void (bool granted)> callback);
}
