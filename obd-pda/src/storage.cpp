#include "storage.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <ctype.h>
#include <stdarg.h>

#include "config.h"

namespace storage {
namespace {

struct StreamState {
    const char* fileName;
    File file;
    char* buf = nullptr;
    size_t len = 0;
    bool open = false;
};

StreamState gStreams[static_cast<size_t>(Stream::Count)] = {
    {"events.log"}, {"sniff.log"}, {"live.csv"},
};

bool gMounted = false;
char gSession[32] = "";
uint32_t gLastFlush = 0;
uint32_t gDropped = 0;

StreamState& state(Stream s) { return gStreams[static_cast<size_t>(s)]; }

void unmountAfterError();

// Writes the RAM buffer to the file. False if the card refused.
bool drain(StreamState& st) {
    if (st.len == 0) return true;
    const size_t written = st.file.write(reinterpret_cast<const uint8_t*>(st.buf), st.len);
    if (written != st.len) {
        gDropped += st.len - written;
        st.len = 0;
        unmountAfterError();
        return false;
    }
    st.len = 0;
    return true;
}

void closeState(StreamState& st, bool flushFirst) {
    if (!st.open) return;
    if (flushFirst) drain(st);
    st.file.close();
    st.open = false;
    st.len = 0;
}

// The card was pulled or failed: stop writing until remount().
void unmountAfterError() {
    for (StreamState& st : gStreams) {
        if (st.open) {
            st.file.close();
            st.open = false;
        }
        st.len = 0;
    }
    gMounted = false;
    Serial.println("[sd] write failed; card removed? logging stopped");
}

// Finds the highest numbered session folder and creates the next one.
bool ensureSession() {
    if (!gMounted) return false;
    if (gSession[0]) return true;
    if (!SD.exists(config::kSdRootDir) && !SD.mkdir(config::kSdRootDir)) return false;
    File root = SD.open(config::kSdRootDir);
    unsigned highest = 0;
    if (root) {
        for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
            const char* name = entry.name();
            // Some core versions return the full path; keep the last part.
            if (const char* slash = strrchr(name, '/')) name = slash + 1;
            bool numeric = entry.isDirectory() && *name;
            for (const char* p = name; *p && numeric; ++p) numeric = isdigit(static_cast<unsigned char>(*p));
            if (numeric) highest = max<unsigned>(highest, strtoul(name, nullptr, 10));
            entry.close();
        }
        root.close();
    }
    char dir[sizeof(gSession)];
    snprintf(dir, sizeof(dir), "%s/%04u", config::kSdRootDir, highest + 1);
    if (!SD.mkdir(dir)) return false;
    strlcpy(gSession, dir, sizeof(gSession));
    Serial.printf("[sd] session folder %s\n", gSession);
    return true;
}

}  // namespace

bool begin() {
    // The display driver has already started the shared SPI bus with these
    // pins; begin() is a no-op if so, and starts it if not.
    SPI.begin(config::kPinSpiSclk, config::kPinSpiMiso, config::kPinSpiMosi, -1);
    gMounted = SD.begin(config::kPinSdCs, SPI, config::kSdSpiHz);
    if (gMounted && SD.cardType() == CARD_NONE) {
        SD.end();
        gMounted = false;
    }
    if (gMounted) {
        Serial.printf("[sd] mounted, %llu MB free\n", freeBytes() / (1024 * 1024));
    } else {
        Serial.println("[sd] no card");
    }
    return gMounted;
}

bool remount() {
    for (StreamState& st : gStreams) closeState(st, gMounted);
    if (gMounted) SD.end();
    gMounted = false;
    gSession[0] = '\0';
    return begin();
}

bool mounted() { return gMounted; }

void service() {
    if (!gMounted || millis() - gLastFlush < config::kLogFlushMs) return;
    gLastFlush = millis();
    for (StreamState& st : gStreams) {
        if (!st.open) continue;
        if (!drain(st)) return;  // unmounted; every stream is closed now
        // flush() commits the FAT entry too, so a power cut loses at most
        // about kLogFlushMs of data rather than the whole file.
        st.file.flush();
    }
}

bool open(Stream s, const char* header) {
    StreamState& st = state(s);
    if (st.open) return true;
    if (!ensureSession()) return false;
    if (!st.buf) {
        st.buf = static_cast<char*>(malloc(config::kLogBufferBytes));
        if (!st.buf) return false;
    }
    char path[48];
    snprintf(path, sizeof(path), "%s/%s", gSession, st.fileName);
    const bool isNew = !SD.exists(path);
    st.file = SD.open(path, FILE_APPEND);
    if (!st.file) return false;
    st.open = true;
    st.len = 0;
    if (isNew && header) write(s, header, strlen(header));
    return true;
}

void close(Stream s) { closeState(state(s), true); }

bool isOpen(Stream s) { return state(s).open; }

void write(Stream s, const char* text, size_t len) {
    StreamState& st = state(s);
    if (!st.open) return;
    if (st.len + len > config::kLogBufferBytes && !drain(st)) return;
    if (len > config::kLogBufferBytes) {
        if (st.file.write(reinterpret_cast<const uint8_t*>(text), len) != len) unmountAfterError();
        return;
    }
    memcpy(st.buf + st.len, text, len);
    st.len += len;
}

void printf(Stream s, const char* fmt, ...) {
    if (!state(s).open) return;
    char line[160];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n > 0) write(s, line, min<size_t>(n, sizeof(line) - 1));
}

void event(const char* fmt, ...) {
    char line[160];
    const uint32_t ms = millis();
    int n = snprintf(line, sizeof(line), "[%6lu.%03lu] ", static_cast<unsigned long>(ms / 1000),
                     static_cast<unsigned long>(ms % 1000));
    va_list args;
    va_start(args, fmt);
    n += vsnprintf(line + n, sizeof(line) - n - 1, fmt, args);
    va_end(args);
    n = min<int>(n, sizeof(line) - 2);
    line[n++] = '\n';
    line[n] = '\0';
    Serial.print(line);
    if (gMounted && open(Stream::Events)) write(Stream::Events, line, n);
}

bool writeReport(const char* prefix, const char* text, char* outName, size_t outLen) {
    if (!ensureSession()) return false;
    char path[64];
    for (unsigned i = 1; i < 1000; ++i) {
        snprintf(path, sizeof(path), "%s/%s_%02u.txt", gSession, prefix, i);
        if (SD.exists(path)) continue;
        File f = SD.open(path, FILE_WRITE);
        if (!f) return false;
        const size_t len = strlen(text);
        const bool ok = f.write(reinterpret_cast<const uint8_t*>(text), len) == len;
        f.close();
        if (!ok) {
            unmountAfterError();
            return false;
        }
        if (outName) strlcpy(outName, strrchr(path, '/') + 1, outLen);
        return true;
    }
    return false;
}

const char* sessionDir() { return gSession; }
uint64_t freeBytes() { return gMounted ? SD.totalBytes() - SD.usedBytes() : 0; }
uint64_t totalBytes() { return gMounted ? SD.totalBytes() : 0; }
uint32_t droppedBytes() { return gDropped; }

}  // namespace storage
