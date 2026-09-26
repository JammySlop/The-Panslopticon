#include "FileShareWifi.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

namespace {

constexpr char kNvsNamespace[] = "fileshare";

// No look-alike characters, so the password is easy to type into a phone.
constexpr char kPassChars[] = "abcdefghijkmnpqrstuvwxyz23456789";

String randomToken(uint8_t len) {
    String s;
    for (uint8_t i = 0; i < len; ++i) s += kPassChars[esp_random() % (sizeof(kPassChars) - 1)];
    return s;
}

String htmlEscape(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        switch (in[i]) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += in[i];
        }
    }
    return out;
}

const char kPageHead[] PROGMEM =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>Wi-Fi setup</title>"
    "<style>body{font:16px system-ui,sans-serif;margin:0 auto;max-width:480px;padding:16px;"
    "background:#0f1115;color:#e6e8ec}input,button{font:inherit;width:100%;box-sizing:border-box;"
    "margin:4px 0 12px;padding:8px;background:#1a1d24;color:#e6e8ec;border:1px solid #3a3f4b;"
    "border-radius:4px}a{color:#5b9dd9}</style></head><body><h1>Wi-Fi setup</h1>";

}  // namespace

void FileShareWifi::begin(const FileShareWifiConfig& cfg, Print* log) {
    cfg_ = cfg;
    log_ = log;

    Preferences prefs;
    prefs.begin(kNvsNamespace, false);
    ssid_ = prefs.getString("ssid", "");
    pass_ = prefs.getString("pass", "");
    if (ssid_.isEmpty() && cfg_.defaultSsid && *cfg_.defaultSsid) {
        ssid_ = cfg_.defaultSsid;
        pass_ = cfg_.defaultPassword ? cfg_.defaultPassword : "";
    }
    if (cfg_.apPassword && strlen(cfg_.apPassword) >= 8) {
        apPass_ = cfg_.apPassword;
    } else {
        if (cfg_.apPassword && log_) log_->println("[wifi] apPassword under 8 chars, using a generated one");
        apPass_ = prefs.getString("appass", "");
        if (apPass_.length() < 8) {
            apPass_ = randomToken(10);
            prefs.putString("appass", apPass_);
        }
    }
    prefs.end();

    if (cfg_.apSsid && *cfg_.apSsid) {
        apSsid_ = cfg_.apSsid;
    } else {
        const uint64_t mac = ESP.getEfuseMac();  // byte 0 of the MAC is the low byte
        char buf[24];
        snprintf(buf, sizeof(buf), "FileShare-%02X%02X", static_cast<unsigned>((mac >> 32) & 0xFF),
                 static_cast<unsigned>((mac >> 40) & 0xFF));
        apSsid_ = buf;
    }

    WiFi.persistent(false);  // credentials live in our own NVS namespace
    WiFi.setHostname(cfg_.hostname);
    WiFi.setAutoReconnect(true);

    if (ssid_.isEmpty()) {
        startPortal();
    } else {
        startConnect();
    }
}

void FileShareWifi::startConnect() {
    if (log_) log_->printf("[wifi] connecting to \"%s\"\n", ssid_.c_str());
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(cfg_.hostname);
    WiFi.begin(ssid_.c_str(), pass_.isEmpty() ? nullptr : pass_.c_str());
    state_ = State::Connecting;
    stateSinceMs_ = millis();
    linkUp_ = false;
}

void FileShareWifi::startPortal() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(apSsid_.c_str(), apPass_.c_str())) {
        if (log_) log_->println("[wifi] failed to start setup hotspot");
    }
    state_ = State::Portal;
    stateSinceMs_ = millis();
    if (log_) {
        log_->printf("[wifi] setup hotspot \"%s\" password \"%s\"; open http://%s/wifi\n", apSsid_.c_str(),
                     apPass_.c_str(), WiFi.softAPIP().toString().c_str());
    }
}

void FileShareWifi::onConnected() {
    state_ = State::Connected;
    linkUp_ = true;
    if (log_) {
        log_->printf("[wifi] connected, IP %s, http://%s.local/\n", WiFi.localIP().toString().c_str(),
                     cfg_.hostname);
    }
    if (!mdnsStarted_ && MDNS.begin(cfg_.hostname)) {
        mdnsStarted_ = true;
        if (cfg_.httpPort) MDNS.addService("http", "tcp", cfg_.httpPort);
    }
}

void FileShareWifi::loop() {
    const uint32_t now = millis();
    switch (state_) {
        case State::Idle:
            break;
        case State::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                onConnected();
            } else if (now - stateSinceMs_ >= cfg_.connectTimeoutMs) {
                if (log_) log_->println("[wifi] connect timed out");
                if (cfg_.apFallback) {
                    startPortal();
                } else {
                    startConnect();
                }
            }
            break;
        case State::Connected: {
            // The driver reconnects on its own (setAutoReconnect); just report it.
            const bool up = WiFi.status() == WL_CONNECTED;
            if (up != linkUp_ && log_) {
                if (up) {
                    log_->printf("[wifi] reconnected, IP %s\n", WiFi.localIP().toString().c_str());
                } else {
                    log_->println("[wifi] link lost, reconnecting");
                }
            }
            linkUp_ = up;
            break;
        }
        case State::Portal:
            if (pendingConnectAtMs_ && static_cast<int32_t>(now - pendingConnectAtMs_) >= 0) {
                pendingConnectAtMs_ = 0;
                startConnect();
            } else if (!ssid_.isEmpty() && cfg_.portalRetryMs && WiFi.softAPgetStationNum() == 0 &&
                       now - stateSinceMs_ >= cfg_.portalRetryMs) {
                startConnect();
            }
            break;
    }
}

IPAddress FileShareWifi::ip() const {
    if (state_ == State::Portal) return WiFi.softAPIP();
    return WiFi.localIP();
}

void FileShareWifi::setCredentials(const String& ssid, const String& password) {
    ssid_ = ssid;
    pass_ = password;
    saveCredentials();
    startConnect();
}

void FileShareWifi::saveCredentials() {
    Preferences prefs;
    prefs.begin(kNvsNamespace, false);
    prefs.putString("ssid", ssid_);
    prefs.putString("pass", pass_);
    prefs.end();
}

void FileShareWifi::clearCredentials() {
    ssid_ = "";
    pass_ = "";
    Preferences prefs;
    prefs.begin(kNvsNamespace, false);
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.end();
    startPortal();
}

void FileShareWifi::attachSetupPage(WebServer& server) {
    WebServer* s = &server;
    server.on("/wifi", HTTP_GET, [this, s] { handleSetupGet(*s); });
    server.on("/wifi", HTTP_POST, [this, s] { handleSetupPost(*s); });
}

void FileShareWifi::handleSetupGet(WebServer& s) {
    if (state_ != State::Portal) {
        s.send(404, "text/plain", "Wi-Fi setup is only available on the setup hotspot.");
        return;
    }
    String html = FPSTR(kPageHead);
    html += "<form method=post action='/wifi'>"
            "<label>Network name (SSID)<input name=ssid maxlength=32 required value='";
    html += htmlEscape(ssid_);
    html += "'></label><label>Password (blank for open networks)"
            "<input name=pass type=password maxlength=63></label>"
            "<button>Save and connect</button></form><p><a href='/'>Files</a></p></body></html>";
    s.send(200, "text/html", html);
}

void FileShareWifi::handleSetupPost(WebServer& s) {
    if (state_ != State::Portal) {
        s.send(404, "text/plain", "Wi-Fi setup is only available on the setup hotspot.");
        return;
    }
    const String ssid = s.arg("ssid");
    const String pass = s.arg("pass");
    if (ssid.isEmpty() || ssid.length() > 32) {
        s.send(400, "text/plain", "SSID must be 1-32 characters.");
        return;
    }
    if (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 63)) {
        s.send(400, "text/plain", "Password must be empty or 8-63 characters.");
        return;
    }
    ssid_ = ssid;
    pass_ = pass;
    saveCredentials();

    String html = FPSTR(kPageHead);
    html += "<p>Saved. Connecting to <b>" + htmlEscape(ssid_) +
            "</b>; this hotspot will close.</p><p>Join that network and open <a href='http://" +
            String(cfg_.hostname) + ".local/'>http://" + String(cfg_.hostname) +
            ".local/</a>. If it doesn't connect, the setup hotspot comes back in about " +
            String(cfg_.connectTimeoutMs / 1000) + " seconds.</p></body></html>";
    s.send(200, "text/html", html);
    // Give the response time to reach the phone before the AP goes down.
    pendingConnectAtMs_ = millis() + 1500;
    if (pendingConnectAtMs_ == 0) pendingConnectAtMs_ = 1;
}
