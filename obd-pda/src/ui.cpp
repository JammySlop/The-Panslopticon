#include "ui.h"

#include <Arduino.h>
#include <Preferences.h>

#include "buttons.h"
#include "can_bus.h"
#include "config.h"
#include "display.h"
#include "obd.h"
#include "vbat.h"

namespace ui {
namespace {

using buttons::Event;

// --- Look ------------------------------------------------------------------

constexpr int kW = 240;
constexpr int kH = 320;
constexpr int kHeaderH = 24;

constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kDim = 0x8410;      // grey
constexpr uint16_t kAccent = 0x07FF;   // cyan
constexpr uint16_t kHeaderBg = 0x1082; // near-black blue-grey
constexpr uint16_t kSelectBg = 0x0230; // dark teal
constexpr uint16_t kWarn = 0xFD20;     // orange
constexpr uint16_t kBad = 0xF800;      // red
constexpr uint16_t kGood = 0x07E0;     // green
constexpr uint16_t kChanged = 0xFFE0;  // yellow

LGFX_Sprite gRow(&lcd);   // one sniffer row
LGFX_Sprite gTile(&lcd);  // one live-data tile

// --- Settings (persisted) ----------------------------------------------------

constexpr uint32_t kRateChoices[] = {0, 500000, 250000, 125000};  // 0 = auto
constexpr uint8_t kBrightChoices[] = {40, 100, 160, 200, 255};

Preferences gPrefs;
uint32_t gRateSetting = 0;
uint8_t gBrightness = config::kBacklightDefault;

void loadSettings() {
    gPrefs.begin("obdpda", false);
    gRateSetting = gPrefs.getUInt("rate", 0);
    gBrightness = gPrefs.getUChar("bl", config::kBacklightDefault);
}

void saveSettings() {
    gPrefs.putUInt("rate", gRateSetting);
    gPrefs.putUChar("bl", gBrightness);
}

// --- Common drawing ------------------------------------------------------------

const char* gTitle = "";
char gHeaderNote[16] = "";
float gShownVolts = -1;

void drawHeader() {
    lcd.fillRect(0, 0, kW, kHeaderH, kHeaderBg);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(kFg, kHeaderBg);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.drawString(gTitle, 6, kHeaderH / 2);
    if (gHeaderNote[0]) {
        lcd.setTextColor(kAccent, kHeaderBg);
        lcd.setTextDatum(textdatum_t::middle_center);
        lcd.drawString(gHeaderNote, 140, kHeaderH / 2);
    }
    gShownVolts = -1;  // force the voltage to redraw
}

void setHeaderNote(const char* note) {
    if (strncmp(note, gHeaderNote, sizeof(gHeaderNote)) == 0) return;
    strlcpy(gHeaderNote, note, sizeof(gHeaderNote));
    drawHeader();
}

void updateHeaderVolts() {
    const float v = vbat::volts();
    if (fabsf(v - gShownVolts) < 0.05f) return;
    gShownVolts = v;
    char buf[12];
    if (v > 0) snprintf(buf, sizeof(buf), "%.1fV", v);
    else strlcpy(buf, "USB", sizeof(buf));
    lcd.fillRect(kW - 52, 0, 52, kHeaderH, kHeaderBg);
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(textdatum_t::middle_right);
    // Below ~12.0 V a resting lead-acid battery is getting flat.
    lcd.setTextColor(v == 0 ? kDim : v < 12.0f ? kWarn : kGood, kHeaderBg);
    lcd.drawString(buf, kW - 6, kHeaderH / 2);
}

void clearBody() { lcd.fillRect(0, kHeaderH, kW, kH - kHeaderH, kBg); }

// Centred message in the body, e.g. "Reading codes...".
void message(const char* line1, const char* line2 = nullptr, uint16_t color = kFg) {
    clearBody();
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(color, kBg);
    lcd.drawString(line1, kW / 2, kH / 2 - (line2 ? 10 : 0));
    if (line2) {
        lcd.setTextColor(kDim, kBg);
        lcd.drawString(line2, kW / 2, kH / 2 + 12);
    }
}

void rateLabel(uint32_t rate, char* out, size_t len) {
    if (rate == 0) strlcpy(out, "auto", len);
    else snprintf(out, len, "%luk", static_cast<unsigned long>(rate / 1000));
}

// --- Bus ownership -------------------------------------------------------------

uint32_t gDetectedRate = 0;
bool gDiscovered = false;

// Opens the bus in `mode`, auto-detecting the bitrate on first use. Draws a
// message and returns false if there is no bus to talk to.
bool openBus(can_bus::Mode mode) {
    uint32_t rate = gRateSetting;
    if (rate == 0) {
        if (gDetectedRate == 0) {
            message("Detecting bus speed...");
            gDetectedRate = can_bus::autodetect();
        }
        rate = gDetectedRate;
    }
    if (rate == 0) {
        message("No CAN traffic found", "Ignition on? Wiring? Long-press: back", kWarn);
        return false;
    }
    if (!can_bus::ensure(rate, mode)) {
        message("CAN driver failed to start", nullptr, kBad);
        return false;
    }
    return true;
}

// Normal mode plus a cached PID discovery; needed by every OBD screen.
bool openObd() {
    if (!openBus(can_bus::Mode::Normal)) return false;
    if (!gDiscovered) {
        message("Querying ECUs...");
        gDiscovered = obd::discover() > 0;
    }
    if (!gDiscovered) {
        message("No OBD-II response", "Ignition on? Long-press: back", kWarn);
        return false;
    }
    return true;
}

// --- Screens -------------------------------------------------------------------

enum class Screen : uint8_t { Menu, Live, Sniffer, Dtc, Info, Settings };

struct ScreenDef {
    void (*enter)();
    void (*tick)();
    void (*event)(Event ev);
};

void go(Screen s);

// ---- Menu ----

constexpr const char* kMenuItems[] = {"Live data", "CAN sniffer", "Trouble codes", "Vehicle info",
                                      "Settings"};
constexpr Screen kMenuTargets[] = {Screen::Live, Screen::Sniffer, Screen::Dtc, Screen::Info,
                                   Screen::Settings};
constexpr int kMenuCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);
constexpr int kMenuRowH = 44;
int gMenuSel = 0;

void drawMenuRow(int i) {
    const int y = kHeaderH + 12 + i * kMenuRowH;
    const bool sel = i == gMenuSel;
    const uint16_t bg = sel ? kSelectBg : kBg;
    lcd.fillRoundRect(8, y, kW - 16, kMenuRowH - 6, 6, bg);
    if (sel) lcd.drawRoundRect(8, y, kW - 16, kMenuRowH - 6, 6, kAccent);
    lcd.setFont(&fonts::Font4);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setTextColor(sel ? kFg : kDim, bg);
    lcd.drawString(kMenuItems[i], 22, y + (kMenuRowH - 6) / 2);
}

void menuEnter() {
    gTitle = "OBD PDA";
    gHeaderNote[0] = '\0';
    drawHeader();
    clearBody();
    for (int i = 0; i < kMenuCount; ++i) drawMenuRow(i);
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(textdatum_t::bottom_center);
    lcd.setTextColor(kDim, kBg);
    lcd.drawString("UP/DOWN move  SELECT open", kW / 2, kH - 4);
}

void menuTick() {}

void menuEvent(Event ev) {
    const int old = gMenuSel;
    if (ev == Event::Up) gMenuSel = (gMenuSel + kMenuCount - 1) % kMenuCount;
    if (ev == Event::Down) gMenuSel = (gMenuSel + 1) % kMenuCount;
    if (ev == Event::Select) {
        go(kMenuTargets[gMenuSel]);
        return;
    }
    if (old != gMenuSel) {
        drawMenuRow(old);
        drawMenuRow(gMenuSel);
    }
}

// ---- Live data ----

constexpr int kTileCols = 2;
constexpr int kTileRows = 4;
constexpr int kTilesPerPage = kTileCols * kTileRows;
constexpr int kTileW = kW / kTileCols;
constexpr int kTileH = (kH - kHeaderH) / kTileRows;

constexpr size_t kMaxLive = 32;
const obd::PidInfo* gLive[kMaxLive];  // supported PIDs, in display order
float gLiveValue[kMaxLive];
bool gLiveValid[kMaxLive];
bool gLiveDirty[kMaxLive];
size_t gLiveCount = 0;
size_t gLivePage = 0;
size_t gLiveNext = 0;  // next tile on the page to poll
uint32_t gLiveLastPoll = 0;
bool gLiveOk = false;

size_t livePages() { return (gLiveCount + kTilesPerPage - 1) / kTilesPerPage; }

void drawTile(size_t index) {
    const size_t slot = index % kTilesPerPage;
    const int x = (slot % kTileCols) * kTileW;
    const int y = kHeaderH + (slot / kTileCols) * kTileH;
    const obd::PidInfo& info = *gLive[index];

    gTile.fillSprite(kBg);
    gTile.drawRoundRect(2, 2, kTileW - 4, kTileH - 4, 5, kHeaderBg);
    gTile.setFont(&fonts::Font2);
    gTile.setTextDatum(textdatum_t::top_left);
    gTile.setTextColor(kDim);
    gTile.drawString(info.name, 9, 6);
    gTile.setTextDatum(textdatum_t::bottom_right);
    gTile.drawString(info.unit, kTileW - 9, kTileH - 6);

    char buf[16];
    if (gLiveValid[index]) {
        snprintf(buf, sizeof(buf), "%.*f", info.decimals, gLiveValue[index]);
    } else {
        strlcpy(buf, "--", sizeof(buf));
    }
    gTile.setFont(&fonts::Font4);
    gTile.setTextDatum(textdatum_t::middle_center);
    gTile.setTextColor(gLiveValid[index] ? kFg : kDim);
    gTile.drawString(buf, kTileW / 2, kTileH / 2 + 2);
    gTile.pushSprite(x, y);
    gLiveDirty[index] = false;
}

void drawLivePage() {
    clearBody();
    char note[16];
    snprintf(note, sizeof(note), "%u/%u", unsigned(gLivePage + 1), unsigned(livePages()));
    setHeaderNote(note);
    const size_t first = gLivePage * kTilesPerPage;
    for (size_t i = first; i < gLiveCount && i < first + kTilesPerPage; ++i) drawTile(i);
}

void liveEnter() {
    gTitle = "Live data";
    gHeaderNote[0] = '\0';
    drawHeader();
    gLiveOk = openObd();
    if (!gLiveOk) return;

    size_t count = 0;
    const obd::PidInfo* all = obd::livePids(count);
    gLiveCount = 0;
    for (size_t i = 0; i < count && gLiveCount < kMaxLive; ++i) {
        if (!obd::isSupported(all[i].pid)) continue;
        gLive[gLiveCount] = &all[i];
        gLiveValid[gLiveCount] = false;
        gLiveDirty[gLiveCount] = true;
        ++gLiveCount;
    }
    if (gLiveCount == 0) {
        gLiveOk = false;
        message("ECU supports none of", "the PIDs this screen shows", kWarn);
        return;
    }
    gLivePage = min(gLivePage, livePages() - 1);
    gLiveNext = 0;
    drawLivePage();
}

void liveTick() {
    if (!gLiveOk) return;
    const uint32_t now = millis();
    if (now - gLiveLastPoll < config::kLivePollGapMs) return;
    gLiveLastPoll = now;

    // Poll only the tiles on screen, round-robin.
    const size_t first = gLivePage * kTilesPerPage;
    const size_t onPage = min<size_t>(kTilesPerPage, gLiveCount - first);
    const size_t index = first + (gLiveNext++ % onPage);
    float value = 0;
    const bool ok = obd::readPid(*gLive[index], value);
    if (ok != gLiveValid[index] || (ok && value != gLiveValue[index])) {
        gLiveValid[index] = ok;
        gLiveValue[index] = value;
        drawTile(index);
    }
}

void liveEvent(Event ev) {
    if (!gLiveOk || livePages() <= 1) return;
    if (ev == Event::Down || ev == Event::Select) gLivePage = (gLivePage + 1) % livePages();
    else if (ev == Event::Up) gLivePage = (gLivePage + livePages() - 1) % livePages();
    else return;
    gLiveNext = 0;
    drawLivePage();
}

// ---- Sniffer ----

struct SnifferEntry {
    uint32_t key;  // id, with bit 31 set for 29-bit IDs; the table is sorted on it
    uint8_t dlc;
    uint8_t data[8];
    uint8_t changedMask;
    uint32_t changedAt;
    uint32_t updatedAt;
    uint32_t count;
    uint32_t countAtLastRate;
    uint16_t rate;  // frames per second
};

struct SnifferRow {
    uint32_t key = UINT32_MAX;
    uint32_t drawnAt = 0;
    bool highlighted = false;
};

constexpr int kSnifferFooterH = 18;
constexpr int kSnifferRowH = 16;
constexpr int kSnifferRows = (kH - kHeaderH - kSnifferFooterH) / kSnifferRowH;

SnifferEntry gEntries[config::kSnifferMaxIds];
size_t gEntryCount = 0;
uint32_t gDroppedIds = 0;
SnifferRow gRows[kSnifferRows];
size_t gSnifferTop = 0;
bool gSnifferPaused = false;
bool gSnifferOk = false;
uint32_t gLastRateCalc = 0;
uint32_t gLastSnifferDraw = 0;
uint32_t gFramesAtLastRate = 0;
uint32_t gBusRate = 0;  // total frames/s

void snifferIngest(const can_bus::Frame& f, uint32_t now) {
    const uint32_t key = f.id | (f.extended ? 0x80000000u : 0);
    // Binary search for the insertion point.
    size_t lo = 0, hi = gEntryCount;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (gEntries[mid].key < key) lo = mid + 1;
        else hi = mid;
    }
    if (lo == gEntryCount || gEntries[lo].key != key) {
        if (gEntryCount >= config::kSnifferMaxIds) {
            ++gDroppedIds;
            return;
        }
        memmove(&gEntries[lo + 1], &gEntries[lo], (gEntryCount - lo) * sizeof(SnifferEntry));
        ++gEntryCount;
        SnifferEntry& e = gEntries[lo];
        e = SnifferEntry();
        e.key = key;
        e.dlc = f.dlc;
        memcpy(e.data, f.data, 8);
        e.updatedAt = now;
        e.count = 1;
        return;
    }
    SnifferEntry& e = gEntries[lo];
    uint8_t diff = 0;
    for (int i = 0; i < 8; ++i) {
        if (e.data[i] != f.data[i]) diff |= 1 << i;
    }
    if (diff) {
        const bool stillLit = now - e.changedAt < config::kSnifferHighlightMs;
        e.changedMask = stillLit ? (e.changedMask | diff) : diff;
        e.changedAt = now;
    }
    e.dlc = f.dlc;
    memcpy(e.data, f.data, 8);
    e.updatedAt = now;
    ++e.count;
}

void drawSnifferRow(int row, uint32_t now) {
    const size_t index = gSnifferTop + row;
    const int y = kHeaderH + row * kSnifferRowH;
    SnifferRow& r = gRows[row];
    gRow.fillSprite(kBg);
    if (index >= gEntryCount) {
        gRow.pushSprite(0, y);
        r.key = UINT32_MAX;
        return;
    }
    const SnifferEntry& e = gEntries[index];
    const bool lit = now - e.changedAt < config::kSnifferHighlightMs;
    gRow.setFont(&fonts::AsciiFont8x16);
    gRow.setTextDatum(textdatum_t::top_left);

    // Layout (8 px per char): "7E8      0241 0C1A F800 0000  25"
    char buf[12];
    const bool ext = e.key & 0x80000000u;
    snprintf(buf, sizeof(buf), ext ? "%08lX" : "%03lX", static_cast<unsigned long>(e.key & 0x1FFFFFFF));
    gRow.setTextColor(kAccent);
    gRow.drawString(buf, 0, 0);

    int x = 9 * 8;
    for (int i = 0; i < 8; ++i) {
        if (i < e.dlc) {
            snprintf(buf, sizeof(buf), "%02X", e.data[i]);
            gRow.setTextColor(lit && (e.changedMask & (1 << i)) ? kChanged : kFg);
            gRow.drawString(buf, x, 0);
        }
        x += (i % 2 == 1) ? 20 : 16;  // a 4 px gap between byte pairs
    }
    snprintf(buf, sizeof(buf), "%3u", unsigned(min<uint16_t>(e.rate, 999)));
    gRow.setTextColor(kDim);
    gRow.setTextDatum(textdatum_t::top_right);
    gRow.drawString(buf, kW, 0);
    gRow.pushSprite(0, y);

    r.key = e.key;
    r.drawnAt = now;
    r.highlighted = lit;
}

void drawSnifferFooter() {
    const can_bus::Stats st = can_bus::stats();
    char buf[48];
    snprintf(buf, sizeof(buf), "%u ids  %lu/s  %s  miss %lu", unsigned(gEntryCount),
             static_cast<unsigned long>(gBusRate), st.state, static_cast<unsigned long>(st.rxMissed));
    const int y = kH - kSnifferFooterH;
    lcd.fillRect(0, y, kW, kSnifferFooterH, kHeaderBg);
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setTextColor(strcmp(st.state, "ok") == 0 ? kDim : kWarn, kHeaderBg);
    lcd.drawString(buf, 4, y + kSnifferFooterH / 2);
}

void snifferEnter() {
    gTitle = "Sniffer";
    gHeaderNote[0] = '\0';
    drawHeader();
    gSnifferOk = openBus(can_bus::Mode::ListenOnly);
    if (!gSnifferOk) return;
    clearBody();
    char note[16];
    rateLabel(can_bus::bitrate(), note, sizeof(note));
    setHeaderNote(gSnifferPaused ? "PAUSED" : note);
    for (SnifferRow& r : gRows) r = SnifferRow();
    drawSnifferFooter();
}

void snifferTick() {
    if (!gSnifferOk) return;
    const uint32_t now = millis();
    can_bus::Frame f;
    // Drain everything waiting; a full queue means lost frames.
    while (can_bus::receive(f, 0)) {
        if (!gSnifferPaused) snifferIngest(f, now);
    }

    if (now - gLastRateCalc >= 1000) {
        const uint32_t total = can_bus::stats().rxFrames;
        gBusRate = total - gFramesAtLastRate;
        gFramesAtLastRate = total;
        for (size_t i = 0; i < gEntryCount; ++i) {
            SnifferEntry& e = gEntries[i];
            e.rate = static_cast<uint16_t>(min<uint32_t>(e.count - e.countAtLastRate, UINT16_MAX));
            e.countAtLastRate = e.count;
        }
        gLastRateCalc = now;
        drawSnifferFooter();
        for (SnifferRow& r : gRows) r.drawnAt = 0;  // rates changed: redraw all
    }

    if (now - gLastSnifferDraw < config::kSnifferRedrawMs) return;
    gLastSnifferDraw = now;
    for (int row = 0; row < kSnifferRows; ++row) {
        const size_t index = gSnifferTop + row;
        const SnifferRow& r = gRows[row];
        bool dirty;
        if (index >= gEntryCount) {
            dirty = r.key != UINT32_MAX;
        } else {
            const SnifferEntry& e = gEntries[index];
            const bool lit = now - e.changedAt < config::kSnifferHighlightMs;
            dirty = r.key != e.key || r.drawnAt == 0 || lit != r.highlighted ||
                    static_cast<int32_t>(e.updatedAt - r.drawnAt) >= 0;
        }
        if (dirty) drawSnifferRow(row, now);
    }
}

void snifferEvent(Event ev) {
    if (!gSnifferOk) return;
    const size_t maxTop = gEntryCount > size_t(kSnifferRows) ? gEntryCount - kSnifferRows : 0;
    if (ev == Event::Up && gSnifferTop > 0) --gSnifferTop;
    if (ev == Event::Down && gSnifferTop < maxTop) ++gSnifferTop;
    if (ev == Event::Select) {
        gSnifferPaused = !gSnifferPaused;
        char note[16];
        rateLabel(can_bus::bitrate(), note, sizeof(note));
        setHeaderNote(gSnifferPaused ? "PAUSED" : note);
    }
    for (SnifferRow& r : gRows) r.drawnAt = 0;
}

// ---- Trouble codes ----

constexpr size_t kMaxDtcs = 32;
obd::Dtc gDtcs[kMaxDtcs];
size_t gDtcCount = 0;
size_t gDtcTop = 0;
obd::MilStatus gMil;
bool gDtcOk = false;

void drawDtcList() {
    clearBody();
    int y = kHeaderH + 8;
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(textdatum_t::top_left);
    if (gMil.valid) {
        lcd.setTextColor(gMil.milOn ? kBad : kGood, kBg);
        char buf[40];
        snprintf(buf, sizeof(buf), "Check engine light: %s", gMil.milOn ? "ON" : "off");
        lcd.drawString(buf, 8, y);
    }
    y += 22;
    if (gDtcCount == 0) {
        lcd.setTextColor(kGood, kBg);
        lcd.drawString("No stored or pending codes", 8, y);
    }
    constexpr int kLineH = 28;
    const size_t visible = (kH - y - 20) / kLineH;
    for (size_t i = gDtcTop; i < gDtcCount && i < gDtcTop + visible; ++i) {
        const obd::Dtc& d = gDtcs[i];
        lcd.setFont(&fonts::Font4);
        lcd.setTextColor(d.pending ? kWarn : kFg, kBg);
        lcd.drawString(d.code, 8, y);
        lcd.setFont(&fonts::Font2);
        lcd.setTextColor(kDim, kBg);
        char buf[24];
        snprintf(buf, sizeof(buf), "%s  %03lX", d.pending ? "pending" : "stored",
                 static_cast<unsigned long>(d.ecuId));
        lcd.drawString(buf, 110, y + 6);
        y += kLineH;
    }
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(textdatum_t::bottom_center);
    lcd.setTextColor(kDim, kBg);
    lcd.drawString("SELECT re-read   hold: back", kW / 2, kH - 4);
}

void readDtcs() {
    gDtcOk = openObd();
    if (!gDtcOk) return;
    message("Reading trouble codes...");
    gMil = obd::readMilStatus();
    gDtcCount = obd::readDtcs(gDtcs, kMaxDtcs);
    gDtcTop = 0;
    char note[16];
    snprintf(note, sizeof(note), "%u codes", unsigned(gDtcCount));
    setHeaderNote(note);
    drawDtcList();
}

void dtcEnter() {
    gTitle = "Trouble codes";
    gHeaderNote[0] = '\0';
    drawHeader();
    readDtcs();
}

void dtcTick() {}

void dtcEvent(Event ev) {
    if (ev == Event::Select) {
        readDtcs();
        return;
    }
    if (!gDtcOk) return;
    if (ev == Event::Up && gDtcTop > 0) --gDtcTop;
    else if (ev == Event::Down && gDtcTop + 1 < gDtcCount) ++gDtcTop;
    else return;
    drawDtcList();
}

// ---- Vehicle info ----

void infoLine(int& y, const char* label, const char* value, uint16_t color = kFg) {
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(kDim, kBg);
    lcd.drawString(label, 8, y);
    lcd.setTextColor(color, kBg);
    lcd.drawString(value, 8, y + 16);
    y += 40;
}

void infoEnter() {
    gTitle = "Vehicle info";
    gHeaderNote[0] = '\0';
    drawHeader();
    const bool ok = openObd();
    char vin[18] = "";
    const bool haveVin = ok && obd::readVin(vin, sizeof(vin));
    if (!ok) return;

    clearBody();
    int y = kHeaderH + 8;
    char buf[32];
    infoLine(y, "VIN", haveVin ? vin : "not reported", haveVin ? kFg : kDim);
    rateLabel(can_bus::bitrate(), buf, sizeof(buf));
    strlcat(buf, gRateSetting == 0 ? " (detected)" : " (fixed)", sizeof(buf));
    infoLine(y, "CAN bitrate", buf);
    snprintf(buf, sizeof(buf), "%u", unsigned(obd::ecuCount()));
    infoLine(y, "OBD ECUs answering", buf);
    size_t total = 0, supported = 0;
    const obd::PidInfo* pids = obd::livePids(total);
    for (size_t i = 0; i < total; ++i) supported += obd::isSupported(pids[i].pid);
    snprintf(buf, sizeof(buf), "%u of %u", unsigned(supported), unsigned(total));
    infoLine(y, "Live PIDs supported", buf);
    const float v = vbat::volts();
    if (v > 0) snprintf(buf, sizeof(buf), "%.2f V", v);
    else strlcpy(buf, "no 12 V (USB power)", sizeof(buf));
    infoLine(y, "Battery at OBD port", buf);
}

void infoTick() {}
void infoEvent(Event ev) {
    if (ev == Event::Select) infoEnter();
}

// ---- Settings ----

int gSetSel = 0;
constexpr int kSetCount = 3;

void drawSettings() {
    clearBody();
    char value[24];
    const char* labels[kSetCount] = {"CAN bitrate", "Brightness", "Re-detect bus"};
    for (int i = 0; i < kSetCount; ++i) {
        const int y = kHeaderH + 12 + i * 52;
        const bool sel = i == gSetSel;
        const uint16_t bg = sel ? kSelectBg : kBg;
        lcd.fillRoundRect(8, y, kW - 16, 46, 6, bg);
        if (sel) lcd.drawRoundRect(8, y, kW - 16, 46, 6, kAccent);
        lcd.setFont(&fonts::Font2);
        lcd.setTextDatum(textdatum_t::top_left);
        lcd.setTextColor(kDim, bg);
        lcd.drawString(labels[i], 18, y + 5);
        value[0] = '\0';
        if (i == 0) rateLabel(gRateSetting, value, sizeof(value));
        if (i == 1) snprintf(value, sizeof(value), "%u%%", unsigned(gBrightness * 100 / 255));
        if (i == 2) strlcpy(value, "press SELECT", sizeof(value));
        lcd.setTextColor(kFg, bg);
        lcd.drawString(value, 18, y + 23);
    }
}

void settingsEnter() {
    gTitle = "Settings";
    gHeaderNote[0] = '\0';
    drawHeader();
    drawSettings();
}

void settingsTick() {}

void settingsEvent(Event ev) {
    if (ev == Event::Up) gSetSel = (gSetSel + kSetCount - 1) % kSetCount;
    if (ev == Event::Down) gSetSel = (gSetSel + 1) % kSetCount;
    if (ev == Event::Select) {
        if (gSetSel == 0) {
            size_t i = 0;
            while (i < 4 && kRateChoices[i] != gRateSetting) ++i;
            gRateSetting = kRateChoices[(i + 1) % 4];
            can_bus::end();
            gDiscovered = false;
        } else if (gSetSel == 1) {
            size_t i = 0;
            while (i < 5 && kBrightChoices[i] < gBrightness) ++i;
            gBrightness = kBrightChoices[(i + 1) % 5];
            lcd.setBrightness(gBrightness);
        } else {
            can_bus::end();
            gDetectedRate = 0;
            gDiscovered = false;
            if (openBus(can_bus::Mode::ListenOnly)) {
                char buf[32], rate[12];
                rateLabel(can_bus::bitrate(), rate, sizeof(rate));
                snprintf(buf, sizeof(buf), "Found %s", rate);
                message(buf, nullptr, kGood);
            }
            delay(1200);
        }
        saveSettings();
    }
    drawSettings();
}

// ---- Dispatch ----

constexpr ScreenDef kScreens[] = {
    {menuEnter, menuTick, menuEvent},          {liveEnter, liveTick, liveEvent},
    {snifferEnter, snifferTick, snifferEvent}, {dtcEnter, dtcTick, dtcEvent},
    {infoEnter, infoTick, infoEvent},          {settingsEnter, settingsTick, settingsEvent},
};

Screen gScreen = Screen::Menu;

void go(Screen s) {
    gScreen = s;
    kScreens[static_cast<int>(s)].enter();
    updateHeaderVolts();
}

}  // namespace

void begin() {
    loadSettings();
    lcd.init();
    lcd.setRotation(0);  // portrait, connector at the top
    lcd.setBrightness(gBrightness);
    lcd.fillScreen(kBg);
    gRow.setColorDepth(16);
    gRow.createSprite(kW, kSnifferRowH);
    gTile.setColorDepth(16);
    gTile.createSprite(kTileW, kTileH);
    go(Screen::Menu);
}

void loop() {
    const Event ev = buttons::poll();
    if (ev == Event::Back && gScreen != Screen::Menu) {
        go(Screen::Menu);
    } else if (ev != Event::None) {
        kScreens[static_cast<int>(gScreen)].event(ev);
    }
    kScreens[static_cast<int>(gScreen)].tick();
    updateHeaderVolts();
}

}  // namespace ui
