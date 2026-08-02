#pragma once

#include "TypingPiano.h"

// Detects whether the current keyboard layout is QWERTZ by asking the OS what
// character the PHYSICAL bottom-left letter key (ANSI Z position, virtual
// key 6) produces. Key events only carry translated characters, so the piano
// mapping has to know the layout up front.
namespace keyboardlayout
{
TypingPiano::Layout detect();
} // namespace keyboardlayout
