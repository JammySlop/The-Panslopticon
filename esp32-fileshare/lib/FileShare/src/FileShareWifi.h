// Joins a WiFi network, with a setup hotspot for entering credentials.
//
// On begin() it connects with the saved credentials (NVS). If there are none,
// or the network can't be reached, it starts a WPA2 setup hotspot; the user
// joins it and opens http://192.168.4.1/wifi to pick a network. While the
// hotspot is up and nobody is on it, the saved network is retried every
// portalRetryMs, so a router that was briefly down doesn't strand the device.
//
// Optional: skip this class entirely if the host firmware already manages
// WiFi (Meshtastic does). FileShareServer only needs a working network.
#pragma once

#include <Arduino.h>
#include <WebServer.h>

struct FileShareWifiConfig {
    // DHCP hostname and mDNS name (http://<hostname>.local).
    const char* hostname = "fileshare";
    // Used only when nothing is saved in NVS yet, e.g. from build flags.
    const char* defaultSsid = nullptr;
    const char* defaultPassword = nullptr;
    // Setup hotspot. Null SSID = "FileShare-XXXX" from the MAC. Null password =
    // random, generated once and kept in NVS (read it with apPassword()).
    const char* apSsid = nullptr;
    const char* apPassword = nullptr;
    uint32_t connectTimeoutMs = 20000;
    // Start the setup hotspot when connecting fails. If false, keep retrying.
    bool apFallback = true;
    uint32_t portalRetryMs = 5 * 60 * 1000;
    // Advertised over mDNS as _http._tcp; 0 = don't advertise.
    uint16_t httpPort = 80;
};

class FileShareWifi {
public:
    enum class State { Idle, Connecting, Connected, Portal };

    void begin(const FileShareWifiConfig& cfg = FileShareWifiConfig(), Print* log = &Serial);
    // Call from the main loop.
    void loop();

    // Adds GET/POST /wifi to a server (e.g. FileShareServer::server()). The
    // page only answers while the setup hotspot is up, so credentials can't be
    // changed from the joined network.
    void attachSetupPage(WebServer& server);

    // Saves credentials to NVS and connects to them.
    void setCredentials(const String& ssid, const String& password);
    // Forgets the saved network and starts the setup hotspot.
    void clearCredentials();

    State state() const { return state_; }
    bool connected() const { return state_ == State::Connected; }
    bool portalActive() const { return state_ == State::Portal; }
    IPAddress ip() const;
    const String& apSsid() const { return apSsid_; }
    const String& apPassword() const { return apPass_; }

private:
    void startConnect();
    void startPortal();
    void onConnected();
    void saveCredentials();
    void handleSetupGet(WebServer& s);
    void handleSetupPost(WebServer& s);

    FileShareWifiConfig cfg_;
    Print* log_ = nullptr;
    State state_ = State::Idle;
    String ssid_, pass_, apSsid_, apPass_;
    uint32_t stateSinceMs_ = 0;
    uint32_t pendingConnectAtMs_ = 0;  // 0 = none
    bool linkUp_ = false;
    bool mdnsStarted_ = false;
};
