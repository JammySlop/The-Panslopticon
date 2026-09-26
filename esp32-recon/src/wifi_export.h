// On-demand WiFi export: the one time this device transmits. When explicitly
// triggered it brings up its own password-protected access point, serves the SD
// log over HTTP, then shuts the radio off and hands control back to the passive
// scanner. It never joins an existing network. Needs RECON_SD; stubbed out
// otherwise.
#pragma once

#include <Arduino.h>

namespace wifi_export {

#if RECON_WIFI_EXPORT
// Blocks until the export ends (idle timeout, hard cap). Credentials and the URL
// are printed to Serial so the host or a person can connect. Restores passive
// scanning before returning.
void run(Print& out);
#else
inline void run(Print& out) { out.println("!export-unsupported"); }
#endif

}  // namespace wifi_export
