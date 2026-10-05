#include "settings.h"

#include <Preferences.h>
#include <string.h>

#include "config.h"

namespace settings {
namespace {

Preferences gPrefs;
Settings gSettings;

}  // namespace

void load() {
    gPrefs.begin("obdpda", false);
    Settings& s = gSettings;
    s.link = static_cast<LinkKind>(gPrefs.getUChar("link", static_cast<uint8_t>(LinkKind::Elm327Wifi)));
    if (s.link != LinkKind::Elm327Wifi && s.link != LinkKind::DirectCan) s.link = LinkKind::Elm327Wifi;
    s.canBitrate = gPrefs.getUInt("rate", 0);
    s.brightness = gPrefs.getUChar("bl", config::kBacklightDefault);
    if (gPrefs.getString("ssid", s.wifiSsid, sizeof(s.wifiSsid)) == 0) {
        strlcpy(s.wifiSsid, config::kElmDefaultSsid, sizeof(s.wifiSsid));
    }
    if (gPrefs.getString("pass", s.wifiPass, sizeof(s.wifiPass)) == 0) s.wifiPass[0] = '\0';
    if (gPrefs.getString("host", s.elmHost, sizeof(s.elmHost)) == 0) s.elmHost[0] = '\0';
    s.elmPort = gPrefs.getUShort("port", 0);
}

void save() {
    const Settings& s = gSettings;
    gPrefs.putUChar("link", static_cast<uint8_t>(s.link));
    gPrefs.putUInt("rate", s.canBitrate);
    gPrefs.putUChar("bl", s.brightness);
    gPrefs.putString("ssid", s.wifiSsid);
    gPrefs.putString("pass", s.wifiPass);
    gPrefs.putString("host", s.elmHost);
    gPrefs.putUShort("port", s.elmPort);
}

Settings& get() { return gSettings; }

}  // namespace settings
