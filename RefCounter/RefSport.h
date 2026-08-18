#ifndef REF_SPORT_H
#define REF_SPORT_H

#include <stdint.h>

// Sport timing presets: the two countdowns the right-hand buttons start, and
// the marks the buzzer fires on along the way.
//
// The shape here follows RefZone: a fixed table in flash, one selected index
// held in NVS, and a settings.h default used until the menu has been touched.
// The last entry, "Custom", is the only editable one; its numbers live in NVS
// too, so a reflash does not wipe them.
//
// Every mark is in seconds *remaining*, matching what the sketch counts down.
// ReadyRef quote their pre-warnings in seconds elapsed; on a 40 second clock
// their "30 second pre-warning" is 10 remaining, which is what Football ships.
namespace RefSport {

// The display draws at most a skinny leading 1 plus two full digits, so no
// value may exceed 199. A clock of 0 would expire the instant it started.
static const uint16_t MIN_CLOCK_SECONDS = 1;
static const uint16_t MAX_SECONDS       = 199;

struct Preset {
  const char *name;        // menu label, <= 9 chars so the rows fit
  const char *description; // <= 17 chars, shown under the picker
  uint16_t longSeconds;    // top-right button
  uint16_t shortSeconds;   // bottom-right button
  uint16_t warnAtSeconds;  // first early warning; 0 = off
  uint16_t warn2AtSeconds; // second early warning; 0 = off
  uint16_t finalCountdownFrom; // buzz each of the last N seconds; 0 = off
};

// Number of presets in the table, Custom included.
uint8_t count();

// The preset at `index`, or the first one if `index` is out of range.
Preset preset(uint8_t index);

// True for the single editable entry.
bool isCustom(uint8_t index);

// Load the saved sport and the Custom slot out of NVS, falling back to the
// defaults in settings.h. Call once at startup.
void begin();

uint8_t index();
void setIndex(uint8_t index); // persists immediately

// The selected preset. This is what the sketch reads every time it starts a
// clock or decides whether to buzz.
Preset active();

// The Custom slot as stored.
Preset custom();

// Overwrite the Custom slot and persist it. Every field is clamped first, so a
// caller cannot store a value the display could not draw: the two clocks to
// 1..MAX_SECONDS, the three marks to 0..MAX_SECONDS.
void setCustom(uint16_t longSeconds, uint16_t shortSeconds,
               uint16_t warnAtSeconds, uint16_t warn2AtSeconds,
               uint16_t finalCountdownFrom);

} // namespace RefSport

#endif // REF_SPORT_H
