#include "ble_recon.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#include <algorithm>
#include <cmath>

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

// The high 6 bits of the 16-bit appearance value select a category.
const char* appearanceName(uint16_t a) {
    switch (a >> 6) {
        case 0:  return a ? "Generic" : "";
        case 1:  return "Phone";
        case 2:  return "Computer";
        case 3:  return "Watch";
        case 4:  return "Clock";
        case 5:  return "Display";
        case 6:  return "Remote";
        case 7:  return "Glasses";
        case 8:  return "Tag";
        case 9:  return "Keyring";
        case 10: return "Media Player";
        case 11: return "Barcode Scanner";
        case 12: return "Thermometer";
        case 13: return "Heart Rate Sensor";
        case 14: return "Blood Pressure";
        case 15: return "HID";           // keyboard/mouse/gamepad
        case 17: return "Glucose Meter";
        case 20: return "Weight Scale";
        case 49: return "Headphones";
        case 51: return "Speaker";
        default: return "";
    }
}

// Names the product family from the Apple manufacturer-data subtype byte. Apple
// devices broadcast one of these constantly even as their address rotates.
const char* appleProduct(const std::string& mfg) {
    if (mfg.size() < 3) return "";
    switch (static_cast<uint8_t>(mfg[2])) {
        case 0x02: return "iBeacon";
        case 0x05: return "AirDrop";
        case 0x07: return "AirPods";
        case 0x09: return "AirPlay";
        case 0x0A: return "AirPlay";
        case 0x0C: return "Handoff";
        case 0x10: return "Nearby";
        case 0x12: return "Find My";   // AirTag and other trackers
        default:   return "";
    }
}

String manufacturerOf(BLEAdvertisedDevice& dev, const std::string& mfg) {
    if (mfg.size() < 2) return String();
    const uint16_t id = static_cast<uint8_t>(mfg[0]) |
                        (static_cast<uint8_t>(mfg[1]) << 8);
    if (const char* company = companyName(id)) return String(company);
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%04X", id);
    return String(buf);
}

// Log-distance path-loss estimate. txPower is the advertised power at 1 m; with
// a typical indoor path-loss exponent of 2 this is a rough metres estimate.
bool estimateDistance(BLEAdvertisedDevice& dev, float& out) {
    if (!dev.haveTXPower()) return false;
    const int8_t txAt1m = dev.getTXPower();
    out = powf(10.0f, (static_cast<float>(txAt1m) - dev.getRSSI()) / 20.0f);
    return true;
}

BleDevice toRecord(BLEAdvertisedDevice& dev) {
    BleDevice rec;
    rec.address = dev.getAddress().toString().c_str();
    rec.addressType = addressTypeName(dev.getAddressType());
    rec.rssi = dev.getRSSI();
    rec.name = dev.haveName() ? dev.getName().c_str() : "";

    const std::string mfg = dev.haveManufacturerData() ? dev.getManufacturerData()
                                                       : std::string();
    rec.manufacturer = mfg.empty() ? String() : manufacturerOf(dev, mfg);
    // Apple's company ID is 0x004C, little-endian 0x4C 0x00.
    if (mfg.size() >= 3 && static_cast<uint8_t>(mfg[0]) == 0x4C &&
        static_cast<uint8_t>(mfg[1]) == 0x00) {
        rec.product = appleProduct(mfg);
    }

    rec.hasTxPower = dev.haveTXPower();
    rec.txPower = rec.hasTxPower ? dev.getTXPower() : 0;
    rec.hasDistance = estimateDistance(dev, rec.distanceM);

    if (dev.haveAppearance()) rec.appearance = appearanceName(dev.getAppearance());

    for (int i = 0; i < dev.getServiceUUIDCount(); ++i) {
        rec.services.push_back(dev.getServiceUUID(i).toString().c_str());
    }
    return rec;
}

}  // namespace

void begin() {
    BLEDevice::init("");
    scanner = BLEDevice::getScan();
    scanner->setActiveScan(config::kBleActiveScan);
    scanner->setInterval(100);
    scanner->setWindow(99);  // Listen ~99% of the time.
}

std::vector<BleDevice> scan() {
    BLEScanResults results = scanner->start(config::kBleScanSeconds, false);

    std::vector<BleDevice> devices;
    devices.reserve(results.getCount());
    for (int i = 0; i < results.getCount(); ++i) {
        BLEAdvertisedDevice dev = results.getDevice(i);
        devices.push_back(toRecord(dev));
    }
    scanner->clearResults();  // Free the result buffer before the next cycle.

    std::sort(devices.begin(), devices.end(),
              [](const BleDevice& a, const BleDevice& b) { return a.rssi > b.rssi; });
    return devices;
}

}  // namespace ble_recon
