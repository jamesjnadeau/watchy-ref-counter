#pragma once

// Host stub for the ESP32 core's Arduino.h.
//
// board.h is the only header the host tests pull in that needs it, and all it
// takes from Arduino.h are the two pin levels. board.h's other core
// references -- the ext1 wake mode and the light sleep level -- sit inside
// #define bodies that nothing on the host expands, so they are not needed
// here. Keep this file that small: it is a stub, not a second core.

#include <cstdint>

#define LOW  0x0
#define HIGH 0x1
