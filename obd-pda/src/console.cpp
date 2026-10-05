#include "console.h"

#include <Arduino.h>
#include <string.h>

#include "buttons.h"
#include "config.h"
#include "link.h"
#include "settings.h"
#include "ui.h"

namespace console {
namespace {

char gLine[96];
size_t gLen = 0;

void show() {
    const settings::Settings& s = settings::get();
    Serial.printf("link  %s\n", s.link == settings::LinkKind::Elm327Wifi ? "elm" : "can");
    Serial.printf("ssid  %s\n", s.wifiSsid);
    Serial.printf("pass  %s\n", s.wifiPass[0] ? "(set)" : "(none)");
    Serial.printf("host  %s\n", s.elmHost[0] ? s.elmHost : "auto (WiFi gateway)");
    if (s.elmPort) Serial.printf("port  %u\n", s.elmPort);
    else Serial.println("port  auto (35000, then 23)");
    Serial.printf("rate  %lu\n", static_cast<unsigned long>(s.canBitrate));
}

void help() {
    Serial.println("Commands:");
    Serial.println("  show                 current settings");
    Serial.println("  ssid <name>          dongle WiFi network");
    Serial.println("  pass <password>      dongle WiFi password (\"pass\" alone clears it)");
    Serial.println("  host <ip>|auto       dongle address (auto = WiFi gateway)");
    Serial.println("  port <n>|auto        dongle TCP port");
    Serial.println("  link elm|can         ELM327 over WiFi, or direct CAN transceiver");
    Serial.println("  nav                  which nav key is held (and the ladder voltage)");
}

void handle(char* line) {
    char* arg = strchr(line, ' ');
    if (arg) {
        *arg++ = '\0';
        while (*arg == ' ') ++arg;
    } else {
        arg = line + strlen(line);
    }
    settings::Settings& s = settings::get();
    bool changed = true;
    if (strcmp(line, "ssid") == 0 && *arg) {
        strlcpy(s.wifiSsid, arg, sizeof(s.wifiSsid));
    } else if (strcmp(line, "pass") == 0) {
        strlcpy(s.wifiPass, arg, sizeof(s.wifiPass));
    } else if (strcmp(line, "host") == 0 && *arg) {
        strlcpy(s.elmHost, strcmp(arg, "auto") == 0 ? "" : arg, sizeof(s.elmHost));
    } else if (strcmp(line, "port") == 0 && *arg) {
        s.elmPort = strcmp(arg, "auto") == 0 ? 0 : static_cast<uint16_t>(atoi(arg));
    } else if (strcmp(line, "link") == 0 && strcmp(arg, "can") == 0 && !config::kDirectCanAvailable) {
        Serial.println("direct CAN needs the ladder nav wiring (kNavWiring in config.h): "
                       "digital wiring uses the CAN pins");
        return;
    } else if (strcmp(line, "link") == 0 && (strcmp(arg, "elm") == 0 || strcmp(arg, "can") == 0)) {
        s.link = arg[0] == 'e' ? settings::LinkKind::Elm327Wifi : settings::LinkKind::DirectCan;
        obdlink::select(s.link);
    } else {
        changed = false;
        if (strcmp(line, "show") == 0) {
            show();
        } else if (strcmp(line, "nav") == 0) {
            // Hold a direction and type "nav" to check the ladder thresholds.
            Serial.printf("held: %s", buttons::heldKeyName());
            if (config::kNavWiring == config::NavWiring::Ladder) {
                Serial.printf("  ladder: %u mV", buttons::ladderMillivolts());
            }
            Serial.println();
        }
        else if (line[0]) help();
    }
    if (!changed) return;
    settings::save();
    // Reconnect with the new settings the next time a screen needs the car.
    obdlink::active().disconnect();
    ui::refresh();
    Serial.println("ok");
    show();
}

}  // namespace

void poll() {
    while (Serial.available()) {
        const int c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (gLen == 0) continue;
            gLine[gLen] = '\0';
            gLen = 0;
            handle(gLine);
        } else if (gLen + 1 < sizeof(gLine)) {
            gLine[gLen++] = static_cast<char>(c);
        }
    }
}

}  // namespace console
