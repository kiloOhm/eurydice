#include "KeyboardLayoutDetect.h"

#import <Carbon/Carbon.h>
#include <vector>

namespace keyboardlayout
{
TypingPiano::Layout detect()
{
    TypingPiano::Layout layout = TypingPiano::Layout::qwerty;

    if (TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource())
    {
        if (auto* layoutData = (CFDataRef) TISGetInputSourceProperty (source,
                                               kTISPropertyUnicodeKeyLayoutData))
        {
            // CFData carries no alignment promise, so go through an
            // aligned copy instead of casting the raw pointer.
            const auto size = (size_t) CFDataGetLength (layoutData);
            std::vector<UInt8> aligned ((size + 3) & ~size_t (3));
            std::memcpy (aligned.data(), CFDataGetBytePtr (layoutData), size);
            const auto* keyboardLayout = reinterpret_cast<const UCKeyboardLayout*> (aligned.data());
            UInt32 deadKeyState = 0;
            UniChar chars[4] = {};
            UniCharCount produced = 0;

            // kVK_ANSI_Z = 6: the physical key that types 'z' on QWERTY and
            // 'y' on QWERTZ.
            if (UCKeyTranslate (keyboardLayout, kVK_ANSI_Z, kUCKeyActionDisplay, 0,
                                LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit,
                                &deadKeyState, 4, &produced, chars) == noErr
                && produced > 0
                && (chars[0] == 'y' || chars[0] == 'Y'))
            {
                layout = TypingPiano::Layout::qwertz;
            }
        }
        CFRelease (source);
    }
    return layout;
}
} // namespace keyboardlayout
