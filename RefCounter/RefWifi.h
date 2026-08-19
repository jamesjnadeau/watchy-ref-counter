#ifndef REF_WIFI_H
#define REF_WIFI_H

// Whether the menu's "Setup WiFi" screen has ever saved working credentials.
//
// Nothing here touches the radio. The flag is a single bool in NVS, written by
// the setup screen and read by the menu, so deciding whether to offer "Sync
// NTP" costs nothing -- asking the ESP32 for its stored SSID instead would
// mean powering the WiFi driver up every time the menu drew a row.
//
// The flag is cleared as well as set. "Setup WiFi" wipes the saved credentials
// before it raises its access point, so a setup that fails or times out really
// has left the watch with nothing to connect to, and the row it gates has to
// go away with them.
namespace RefWifi {

// Load the saved flag out of NVS. Nothing stored means "never set up", which
// is also what a watch upgraded from a firmware without this flag reads --
// running "Setup WiFi" once more is what brings "Sync NTP" back.
// Call once at startup.
void begin();

bool configured();
void setConfigured(bool on); // persists immediately

} // namespace RefWifi

#endif // REF_WIFI_H
