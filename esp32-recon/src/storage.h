// Local SD-card log. Every JSON sweep line the reporter emits is appended to a
// per-boot file, so the scanner keeps a record with no computer attached.
// Builds without RECON_SD get no-op stubs, so the shared reporter and main loop
// need no #ifdefs.
#pragma once

#include <Arduino.h>

namespace storage {

#if RECON_SD
// Mounts the card on the shared SPI bus and opens this boot's log file. Safe to
// call with no card: it prints a note and leaves logging disabled.
void begin();
bool ready();

// Appends one line (a JSON sweep record). No-op until a card is mounted.
void append(const String& line);
void flush();

// USB-export protocol, written to `out` (the serial link). The host drives it
// with the !ls / !cat / !sd-status commands; the markers frame each reply.
void list(Print& out);
bool cat(Print& out, const String& name);
void status(Print& out);
#else
inline void begin() {}
inline bool ready() { return false; }
inline void append(const String&) {}
inline void flush() {}
inline void list(Print&) {}
inline bool cat(Print&, const String&) { return false; }
inline void status(Print&) {}
#endif

}  // namespace storage
