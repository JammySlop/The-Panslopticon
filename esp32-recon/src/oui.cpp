#include "oui.h"

namespace oui {
namespace {

struct Entry {
    uint8_t prefix[3];
    const char* name;
};

// A handful of vendors seen in most homes. Extend freely; keep it sorted only
// for readability, the lookup is linear.
const Entry kTable[] = {
    {{0x5C, 0x35, 0xFC}, "TP-Link"},
    {{0x70, 0x58, 0xA4}, "TP-Link"},
    {{0x00, 0x1A, 0x11}, "Google"},
    {{0x94, 0xB2, 0xCC}, "Arris"},
    {{0xFC, 0xFB, 0xFB}, "Cisco"},
    {{0xB8, 0x27, 0xEB}, "Raspberry Pi"},
    {{0xDC, 0xA6, 0x32}, "Raspberry Pi"},
    {{0x3C, 0x22, 0xFB}, "Apple"},
    {{0xF0, 0x18, 0x98}, "Apple"},
    {{0xAC, 0xDE, 0x48}, "Apple"},
    {{0x00, 0x17, 0x88}, "Philips Hue"},
    {{0xEC, 0xFA, 0xBC}, "Espressif"},
    {{0x24, 0x0A, 0xC4}, "Espressif"},
    {{0x7C, 0xDF, 0xA1}, "Espressif"},
    {{0x18, 0xFE, 0x34}, "Espressif"},
    {{0x00, 0x50, 0xF2}, "Microsoft"},
    {{0x50, 0xC7, 0xBF}, "TP-Link"},
    {{0x8C, 0x85, 0x90}, "Apple"},
    {{0x40, 0xB4, 0xCD}, "Amazon"},
    {{0x74, 0xC2, 0x46}, "Amazon"},
    {{0x68, 0x37, 0xE9}, "Amazon"},
    {{0xD8, 0x0D, 0x17}, "TP-Link"},
    {{0xA4, 0x77, 0x33}, "Google"},
    {{0x00, 0x0C, 0x43}, "Ralink/MediaTek"},
    {{0xE4, 0x5F, 0x01}, "Raspberry Pi"},
};

}  // namespace

const char* vendor(const uint8_t mac[6]) {
    if (isRandomized(mac)) return "";
    for (const Entry& e : kTable) {
        if (mac[0] == e.prefix[0] && mac[1] == e.prefix[1] && mac[2] == e.prefix[2]) {
            return e.name;
        }
    }
    return "";
}

namespace {
bool parseMac(const String& s, uint8_t out[6]) {
    if (s.length() < 17) return false;
    for (int i = 0; i < 6; ++i) {
        const int hi = i * 3;
        char* end = nullptr;
        const long v = strtol(s.substring(hi, hi + 2).c_str(), &end, 16);
        if (v < 0 || v > 255) return false;
        out[i] = static_cast<uint8_t>(v);
    }
    return true;
}
}  // namespace

String vendorFromString(const String& mac) {
    uint8_t m[6];
    if (!parseMac(mac, m)) return String();
    return String(vendor(m));
}

bool randomizedFromString(const String& mac) {
    uint8_t m[6];
    if (!parseMac(mac, m)) return false;
    return isRandomized(m);
}

}  // namespace oui
