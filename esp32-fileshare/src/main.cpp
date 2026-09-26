// FileShare example: join WiFi (setup hotspot if needed) and share /share on
// LittleFS. Open http://fileshare.local/ once it's on your network.
#include <Arduino.h>
#include <LittleFS.h>

#include "FileShareServer.h"
#include "FileShareWifi.h"

// Build-time options, all optional. See platformio.ini.
#ifndef FILESHARE_SSID
#define FILESHARE_SSID nullptr
#endif
#ifndef FILESHARE_PASS
#define FILESHARE_PASS nullptr
#endif
#ifndef FILESHARE_USER
#define FILESHARE_USER nullptr
#endif
#ifndef FILESHARE_PASSWORD
#define FILESHARE_PASSWORD nullptr
#endif

namespace {

FileShareWifi wifi;
FileShareServer share;

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\nFileShare example");

    // Formats the partition on first boot.
    if (!LittleFS.begin(/*formatOnFail=*/true)) {
        Serial.println("LittleFS mount failed; halting");
        for (;;) delay(1000);
    }

    FileShareWifiConfig wcfg;
    wcfg.defaultSsid = FILESHARE_SSID;
    wcfg.defaultPassword = FILESHARE_PASS;
    wifi.begin(wcfg);

    FileShareConfig scfg;
    scfg.user = FILESHARE_USER;
    scfg.password = FILESHARE_PASSWORD;
    scfg.maxFileBytes = 512 * 1024;
    scfg.maxTotalBytes = 2 * 1024 * 1024;
    if (!share.begin(LittleFS, scfg)) {
        Serial.println("File share failed to start; halting");
        for (;;) delay(1000);
    }
    wifi.attachSetupPage(*share.server());

    share.onEvent([](FileShareServer::Event e, const String& name, size_t size) {
        // A host project would announce this, e.g. as a mesh text message.
        Serial.printf("event: %s %s (%u bytes)\n", e == FileShareServer::Event::Uploaded ? "uploaded" : "deleted",
                      name.c_str(), static_cast<unsigned>(size));
    });
}

void loop() {
    wifi.loop();
    share.loop();
    delay(2);
}
