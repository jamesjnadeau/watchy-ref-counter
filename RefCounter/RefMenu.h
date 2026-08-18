#ifndef REF_MENU_H
#define REF_MENU_H

#include "RefClock.h"

// The settings menu, held open with the bottom-left button from the ready
// screen. Runs modally and returns once the user backs out or it times out;
// the caller repaints its own screen afterwards.
namespace RefMenu {

void open(RefClock &refClock);

} // namespace RefMenu

#endif // REF_MENU_H
