#include "ble_recon.h"

#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#include <algorithm>
#include <vector>

#include "config.h"

namespace ble_recon {
namespace {

BLEScan* scanner = nullptr;

// Random and resolvable-private addresses rotate every few minutes, so the same
// phone can appear as a "new" device. Public addresses are stable.
const char* addressTypeName(esp_ble_addr_type_t type) {
    switch (type) {
        case BLE_ADDR_TYPE_PUBLIC:     return "public";
        case BLE_ADDR_TYPE_RANDOM:     return "random";
        case BLE_ADDR_TYPE_RPA_PUBLIC: return "rpa-pub";
        case BLE_ADDR_TYPE_RPA_RANDOM: return "rpa-rnd";
        default:                       return "?";
    }
}

// Bluetooth SIG company identifiers for vendors commonly seen nearby.
const char* companyName(uint16_t id) {
    switch (id) {
        case 0x0006: return "Microsoft";
        case 0x004C: return "Apple";
        case 0x0075: return "Samsung";
        case 0x0087: return "Garmin";
        case 0x009E: return "Bose";
        case 0x00E0: return "Google";
        case 0x012D: return "Sony";
        default:     return nullptr;
    }
}

String describe(BLEAdvertisedDevice& dev) {
    String out;
    char buf[24];

    if (dev.haveManufacturerData()) {
        // The first two bytes are the company ID, little-endian.
        const std::string mfg = dev.getManufacturerData();
        if (mfg.size() >= 2) {
            const uint16_t id = static_cast<uint8_t>(mfg[0]) |
                                (static_cast<uint8_t>(mfg[1]) << 8);
            if (const char* company = companyName(id)) {
                out += "mfg=";
                out += company;
                out += ' ';
            } else {
                snprintf(buf, sizeof(buf), "mfg=0x%04X ", id);
                out += buf;
            }
        }
    }
    if (dev.haveTXPower()) {
        snprintf(buf, sizeof(buf), "tx=%ddBm ", dev.getTXPower());
        out += buf;
    }
    for (int i = 0; i < dev.getServiceUUIDCount(); ++i) {
        out += "svc=";
        out += dev.getServiceUUID(i).toString().c_str();
        out += ' ';
    }
    return out;
}

}  // namespace

void begin() {
    BLEDevice::init("");
    scanner = BLEDevice::getScan();
    scanner->setActiveScan(config::kBleActiveScan);
    scanner->setInterval(100);
    scanner->setWindow(99);  // Listen ~99% of the time.
}

void scanAndReport() {
    BLEScanResults results = scanner->start(config::kBleScanSeconds, false);

    std::vector<BLEAdvertisedDevice> devices;
    devices.reserve(results.getCount());
    for (int i = 0; i < results.getCount(); ++i) {
        devices.push_back(results.getDevice(i));
    }
    std::sort(devices.begin(), devices.end(),
              [](BLEAdvertisedDevice& a, BLEAdvertisedDevice& b) {
                  return a.getRSSI() > b.getRSSI();
              });

    Serial.printf("\n--- BLE: %u device(s) in %lus ---\n",
                  static_cast<unsigned>(devices.size()),
                  static_cast<unsigned long>(config::kBleScanSeconds));
    Serial.printf("%-17s  %-7s  %4s  %-20s  %s\n",
                  "ADDRESS", "TYPE", "RSSI", "NAME", "DETAILS");
    for (BLEAdvertisedDevice& dev : devices) {
        const std::string name = dev.haveName() ? dev.getName() : "";
        Serial.printf("%-17s  %-7s  %4d  %-20.20s  %s\n",
                      dev.getAddress().toString().c_str(),
                      addressTypeName(dev.getAddressType()),
                      dev.getRSSI(), name.c_str(), describe(dev).c_str());
    }

    scanner->clearResults();  // Free the result buffer before the next cycle.
}

}  // namespace ble_recon
