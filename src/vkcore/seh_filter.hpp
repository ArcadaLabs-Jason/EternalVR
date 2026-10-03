#pragma once

// The filter of every structured exception handler in the layer (guarded reads of game memory and guarded
// calls into the game): an access violation, a freed or unmapped address, is handled; anything else (a
// guard page hit on another thread's stack, a game exception) goes on to the game's own handlers as if the
// layer were not there. Use: __except (accessViolationOnly(GetExceptionCode())).

#include <windows.h>

namespace evr::vkcore {

inline int accessViolationOnly(unsigned long code) {
    return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

} // namespace evr::vkcore
