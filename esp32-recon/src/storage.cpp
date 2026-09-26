#include "storage.h"

#if RECON_SD

#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

#include "config.h"

namespace storage {
namespace {

bool mounted = false;
File logFile;
uint32_t lastFlushMs = 0;
uint32_t session = 0;

// Only allow a plain name inside kSdDir: letters, digits, dot, dash, underscore.
// Rejects anything with a path separator or "..", so a request can't escape the
// log directory.
bool safeName(const String& name) {
    if (name.isEmpty() || name.length() > 64) return false;
    for (size_t i = 0; i < name.length(); ++i) {
        const char c = name[i];
        const bool ok = isalnum(c) || c == '.' || c == '-' || c == '_';
        if (!ok) return false;
    }
    return name.indexOf("..") < 0;
}

String pathOf(const String& name) { return String(config::kSdDir) + "/" + name; }

}  // namespace

void begin() {
    SPI.begin(config::kSpiSck, config::kSpiMiso, config::kSpiMosi);
#if RECON_DISPLAY
    // Deassert the other devices on the bus so they ignore the SD card's clocks
    // during mount. The display task is not running yet (main starts it later).
    pinMode(config::kTftCs, OUTPUT);
    digitalWrite(config::kTftCs, HIGH);
    pinMode(config::kTouchCs, OUTPUT);
    digitalWrite(config::kTouchCs, HIGH);
#endif

    if (!SD.begin(config::kSdCs, SPI, config::kSdSpiHz) || SD.cardType() == CARD_NONE) {
        Serial.println("SD: no card or mount failed; continuing without local logging");
        return;
    }

    Preferences prefs;
    prefs.begin("recon", false);
    session = prefs.getUInt("boot", 0) + 1;
    prefs.putUInt("boot", session);
    prefs.end();

    if (!SD.exists(config::kSdDir)) SD.mkdir(config::kSdDir);
    char path[48];
    snprintf(path, sizeof(path), "%s/scan%04lu.jsonl", config::kSdDir,
             static_cast<unsigned long>(session));
    logFile = SD.open(path, FILE_APPEND, true);
    if (!logFile) {
        Serial.printf("SD: cannot open %s; local logging disabled\n", path);
        return;
    }

    mounted = true;
    lastFlushMs = millis();
    Serial.printf("SD: logging to %s (%llu MB card)\n", path,
                  SD.cardSize() / (1024ULL * 1024ULL));

    String marker = "{\"t\":\"boot\",\"session\":";
    marker += session;
    marker += ",\"chip\":\"";
    marker += ESP.getChipModel();
    marker += "\",\"up\":";
    marker += millis() / 1000;
    marker += "}";
    append(marker);
}

bool ready() { return mounted; }

void append(const String& line) {
    if (!mounted) return;
    logFile.println(line);
    const uint32_t now = millis();
    if (now - lastFlushMs >= config::kSdFlushMs) {
        logFile.flush();
        lastFlushMs = now;
    }
}

void flush() {
    if (mounted) logFile.flush();
}

void list(Print& out) {
    out.println("!ls-begin");
    if (mounted) {
        flush();  // So the current file's advertised size is up to date.
        File dir = SD.open(config::kSdDir);
        if (dir) {
            for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                if (!f.isDirectory()) {
                    out.printf("F %s %u\n", f.name(), static_cast<unsigned>(f.size()));
                }
                f.close();
            }
            dir.close();
        }
    }
    out.println("!ls-end");
}

bool cat(Print& out, const String& name) {
    if (!mounted || !safeName(name)) {
        out.printf("!cat-error %s\n", name.c_str());
        return false;
    }
    flush();
    File f = SD.open(pathOf(name), FILE_READ);
    if (!f) {
        out.printf("!cat-error %s\n", name.c_str());
        return false;
    }
    out.printf("!cat-begin %s %u\n", name.c_str(), static_cast<unsigned>(f.size()));
    uint8_t buf[512];
    while (f.available()) {
        const size_t n = f.read(buf, sizeof(buf));
        out.write(buf, n);
    }
    f.close();
    out.printf("\n!cat-end %s\n", name.c_str());
    return true;
}

void status(Print& out) {
    if (!mounted) {
        out.println("!sd status=absent");
        return;
    }
    out.printf("!sd status=mounted session=%lu size=%llu used=%llu\n",
               static_cast<unsigned long>(session), SD.cardSize(), SD.usedBytes());
}

}  // namespace storage

#endif  // RECON_SD
