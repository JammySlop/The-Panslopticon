// Maps the first three bytes of a MAC address (the OUI) to a vendor, using a
// small curated table of makers common in a home/office environment. This is
// not the full IEEE registry; unknown OUIs simply return "".
//
// A locally-administered address (bit 0x02 of the first byte) is not a real
// OUI at all: it is a randomized/virtual MAC, so we report that instead.
#pragma once

#include <Arduino.h>

namespace oui {

// True when the address is randomized/virtual rather than a burned-in MAC.
inline bool isRandomized(const uint8_t mac[6]) { return mac[0] & 0x02; }

const char* vendor(const uint8_t mac[6]);

// Accepts "AA:BB:CC:DD:EE:FF"; returns "" when the string is malformed.
String vendorFromString(const String& mac);
bool randomizedFromString(const String& mac);

}  // namespace oui
