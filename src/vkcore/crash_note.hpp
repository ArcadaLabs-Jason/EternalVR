#pragma once

// The crash note: when the game takes a fault of the kind a process dies of (an access violation, an illegal
// instruction, a stack overflow, crash_note_seen.hpp), the log gets a line naming the exception, the module
// and offset of the faulting instruction (EternalVR.dll, the game's exe, a driver), the address an access
// violation read or wrote, and the first return addresses on the thread's stack that lie in the layer or the
// game. A player's crash report then shows whether the layer was in the fault, and where.
//
// The exception is seen first-chance (a vectored handler, before the game's own handlers), so a line is not
// proof of a crash: the last exception line before a log that stops is. An unhandled exception gets a line of
// its own from a filter chained in front of the one before it. The handlers only log; the exception goes on
// as it would have.

namespace evr::vkcore {

// Installs the handlers, once (logOpen calls it). Any thread. Rig only: ETERNALVR_TEST_CRASH=<seconds> (never
// set by the launcher) then raises an access violation, not continuable, on a thread of its own that many
// seconds later, so a run shows the note of a real crash.
void installCrashNote();

} // namespace evr::vkcore
