#include "wifi_export.h"

#if RECON_WIFI_EXPORT

#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>

#include "config.h"
#include "display.h"
#include "storage.h"
#include "wifi_recon.h"

namespace wifi_export {
namespace {

// No look-alike characters (0/O, 1/l/I), so the password is easy to read off a
// screen and type into a phone.
constexpr char kPassChars[] = "abcdefghijkmnpqrstuvwxyz23456789";

String randomToken(uint8_t len) {
    String s;
    for (uint8_t i = 0; i < len; ++i) {
        s += kPassChars[esp_random() % (sizeof(kPassChars) - 1)];
    }
    return s;
}

// Same rule as storage::safeName: a plain filename, no path traversal.
bool safeName(const String& name) {
    if (name.isEmpty() || name.length() > 64) return false;
    for (size_t i = 0; i < name.length(); ++i) {
        const char c = name[i];
        if (!(isalnum(c) || c == '.' || c == '-' || c == '_')) return false;
    }
    return name.indexOf("..") < 0;
}

WebServer server(80);
uint32_t lastActivityMs = 0;

void touchActivity() { lastActivityMs = millis(); }

void handleIndex() {
    touchActivity();
    String html =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>recon export</title>"
        "<style>body{font:16px system-ui;margin:24px;background:#0f1115;color:#e6e8ec}"
        "a{color:#5b9dd9}li{margin:6px 0}</style>"
        "<h1>Recon export</h1><p>Local SD-card capture. Files are JSON lines.</p><ul>";
    File dir = SD.open(config::kSdDir);
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            if (!f.isDirectory()) {
                const String name = f.name();
                html += "<li><a href='/dl?f=" + name + "'>" + name + "</a> (" +
                        String(static_cast<unsigned>(f.size())) + " bytes)</li>";
            }
            f.close();
        }
        dir.close();
    }
    html += "</ul>";
    server.send(200, "text/html", html);
}

void handleDownload() {
    touchActivity();
    const String name = server.arg("f");
    if (!safeName(name)) {
        server.send(400, "text/plain", "bad filename");
        return;
    }
    File f = SD.open(String(config::kSdDir) + "/" + name, FILE_READ);
    if (!f) {
        server.send(404, "text/plain", "not found");
        return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=" + name);
    server.streamFile(f, "application/x-ndjson");
    f.close();
}

}  // namespace

void run(Print& out) {
    if (!storage::ready()) {
        out.println("!export-error no-sd");
        return;
    }
    storage::flush();
    // The web server reads the SD card from this (main-loop) context, so stop
    // the display task touching the shared bus for the whole session.
    display::pause();

    const String ssid = String(config::kExportApPrefix) + randomToken(4);
    const String pass = randomToken(config::kExportPassLen);

    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    const bool up = WiFi.softAP(ssid.c_str(), pass.c_str(), config::kExportApChannel,
                                /*hidden=*/0, /*max_connection=*/1);
    if (!up) {
        out.println("!export-error ap-failed");
        WiFi.mode(WIFI_OFF);
        display::resume();
        wifi_recon::begin();
        return;
    }

    const IPAddress ip = WiFi.softAPIP();
    server.on("/", handleIndex);
    server.on("/dl", handleDownload);
    server.begin();

    // Machine-readable so the host can surface the credentials; also human-readable.
    out.printf("!export-begin ssid=%s pass=%s url=http://%s/\n", ssid.c_str(),
               pass.c_str(), ip.toString().c_str());
    out.printf("WiFi export up. Join \"%s\" (pass %s) and open http://%s/\n",
               ssid.c_str(), pass.c_str(), ip.toString().c_str());

    const uint32_t startMs = millis();
    lastActivityMs = startMs;
    bool everConnected = false;
    for (;;) {
        server.handleClient();
        delay(2);
        const uint32_t now = millis();
        if (WiFi.softAPgetStationNum() > 0) {
            everConnected = true;
            touchActivity();
        }
        // Stop once idle for a while (only after someone connected), or at the
        // hard cap regardless, so the radio is never left on.
        if (now - startMs >= config::kExportMaxMs) break;
        if (everConnected && now - lastActivityMs >= config::kExportIdleTimeoutMs) break;
        if (!everConnected && now - startMs >= config::kExportIdleTimeoutMs) break;
    }

    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    out.println("!export-end");

    // Restore passive scanning.
    wifi_recon::begin();
    display::resume();
}

}  // namespace wifi_export

#endif  // RECON_WIFI_EXPORT
