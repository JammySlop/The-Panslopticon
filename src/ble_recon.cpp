#include "ble_recon.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#include <algorithm>

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

String manufacturerOf(BLEAdvertisedDevice& dev) {
    if (!dev.haveManufacturerData()) return String();

    // The first two bytes are the company ID, little-endian.
    const std::string mfg = dev.getManufacturerData();
    if (mfg.size() < 2) return String();

    const uint16_t id = static_cast<uint8_t>(mfg[0]) |
                        (static_cast<uint8_t>(mfg[1]) << 8);
    if (const char* company = companyName(id)) return String(company);

    char buf[8];
    snprintf(buf, sizeof(buf), "0x%04X", id);
    return String(buf);
}

BleDevice toRecord(BLEAdvertisedDevice& dev) {
    BleDevice rec;
    rec.address = dev.getAddress().toString().c_str();
    rec.addressType = addressTypeName(dev.getAddressType());
    rec.rssi = dev.getRSSI();
    rec.name = dev.haveName() ? dev.getName().c_str() : "";
    rec.manufacturer = manufacturerOf(dev);
    rec.hasTxPower = dev.haveTXPower();
    rec.txPower = rec.hasTxPower ? dev.getTXPower() : 0;
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
