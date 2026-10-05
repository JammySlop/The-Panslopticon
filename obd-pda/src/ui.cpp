#include "ui.h"

#include <Arduino.h>
#include <WiFi.h>

#include <algorithm>

#include "buttons.h"
#include "config.h"
#include "display.h"
#include "link.h"
#include "obd.h"
#include "settings.h"

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

obdlink::Link& link() { return obdlink::active(); }

// --- Common drawing ------------------------------------------------------------

const char* gTitle = "";
char gHeaderNote[16] = "";
float gShownVolts = -100;
int gShownLinkState = -1;

void drawHeader() {
    lcd.fillRect(0, 0, kW, kHeaderH, kHeaderBg);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(kFg, kHeaderBg);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.drawString(gTitle, 6, kHeaderH / 2);
    if (gHeaderNote[0]) {
        lcd.setTextColor(kAccent, kHeaderBg);
        lcd.setTextDatum(textdatum_t::middle_center);
        lcd.drawString(gHeaderNote, 128, kHeaderH / 2);
    }
    gShownVolts = -100;  // force the status area to redraw
    gShownLinkState = -1;
}

void setHeaderNote(const char* note) {
    if (strncmp(note, gHeaderNote, sizeof(gHeaderNote)) == 0) return;
    strlcpy(gHeaderNote, note, sizeof(gHeaderNote));
    drawHeader();
}

// Battery voltage as the link sees it: ATRV from an ELM, or our own divider
// on the direct-CAN build (in ELM mode that pin may be floating).
float headerVolts() { return link().batteryVolts(); }

// Right side of the header: link name (green when connected) and volts.
void updateHeaderStatus() {
    const int state = link().connected() ? 1 : 0;
    if (state != gShownLinkState) {
        gShownLinkState = state;
        lcd.fillRect(kW - 84, 0, 30, kHeaderH, kHeaderBg);
        lcd.setFont(&fonts::Font2);
        lcd.setTextDatum(textdatum_t::middle_right);
        lcd.setTextColor(state ? kGood : kDim, kHeaderBg);
        lcd.drawString(link().shortName(), kW - 56, kHeaderH / 2);
    }
    const float v = headerVolts();
    if (fabsf(v - gShownVolts) < 0.05f) return;
    gShownVolts = v;
    char buf[12];
    if (v > 0) snprintf(buf, sizeof(buf), "%.1fV", v);
    else strlcpy(buf, "--.-V", sizeof(buf));
    lcd.fillRect(kW - 52, 0, 52, kHeaderH, kHeaderBg);
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(textdatum_t::middle_right);
    // Below ~12.0 V a resting lead-acid battery is getting flat.
    lcd.setTextColor(v <= 0 ? kDim : v < 12.0f ? kWarn : kGood, kHeaderBg);
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

void ecuLabel(uint32_t id, char* out, size_t len) {
    snprintf(out, len, id > 0x7FF ? "%08lX" : "%03lX", static_cast<unsigned long>(id));
}

// --- Link ownership -------------------------------------------------------------

bool gDiscovered = false;

void connectStatus(const char* line) { message(line); }

// Connects if needed. Draws the reason and returns false on failure.
bool openLink() {
    if (link().connected()) return true;
    gDiscovered = false;
    if (link().connect(connectStatus)) return true;
    const bool elm = settings::get().link == settings::LinkKind::Elm327Wifi;
    message(link().lastError(), elm ? "Dongle plugged in? Ignition on?" : "Ignition on? Wiring?", kWarn);
    return false;
}

// Connected, with the car's supported PIDs known; needed by every OBD screen.
bool openObd() {
    if (!openLink()) return false;
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

enum class Screen : uint8_t { Menu, Live, Sniffer, Dtc, Info, Settings, WifiPick };

struct ScreenDef {
    void (*enter)();
    void (*tick)();
    void (*event)(Event ev);
    void (*leave)();
};

void go(Screen s);
void noop() {}

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
    lcd.drawString("Up/down: move   Press: open", kW / 2, kH - 4);
}

void menuEvent(Event ev) {
    const int old = gMenuSel;
    if (ev == Event::Up) gMenuSel = (gMenuSel + kMenuCount - 1) % kMenuCount;
    if (ev == Event::Down) gMenuSel = (gMenuSel + 1) % kMenuCount;
    if (ev == Event::Select || ev == Event::Right) {
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
    if (!link().connected()) {
        gLiveOk = false;
        message(link().lastError(), "SELECT: reconnect", kWarn);
        return;
    }
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
    if (!gLiveOk) {
        if (ev == Event::Select) liveEnter();
        return;
    }
    if (livePages() <= 1) return;
    if (ev == Event::Down || ev == Event::Right || ev == Event::Select) {
        gLivePage = (gLivePage + 1) % livePages();
    } else if (ev == Event::Up || ev == Event::Left) {
        gLivePage = (gLivePage + livePages() - 1) % livePages();
    }
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
// Frames handled per loop, so a busy bus can't starve the buttons and display.
constexpr int kSnifferFramesPerTick = 200;

SnifferEntry gEntries[config::kSnifferMaxIds];
size_t gEntryCount = 0;
SnifferRow gRows[kSnifferRows];
size_t gSnifferTop = 0;
bool gSnifferPaused = false;
bool gSnifferOk = false;
uint32_t gLastRateCalc = 0;
uint32_t gLastSnifferDraw = 0;
uint32_t gSnifferFrames = 0;
uint32_t gFramesAtLastRate = 0;
uint32_t gBusRate = 0;  // total frames/s seen

void snifferIngest(const CanFrame& f, uint32_t now) {
    const uint32_t key = f.id | (f.extended ? 0x80000000u : 0);
    // Binary search for the insertion point.
    size_t lo = 0, hi = gEntryCount;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (gEntries[mid].key < key) lo = mid + 1;
        else hi = mid;
    }
    if (lo == gEntryCount || gEntries[lo].key != key) {
        if (gEntryCount >= config::kSnifferMaxIds) return;
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
    char buf[48];
    snprintf(buf, sizeof(buf), "%u ids  %lu/s  dropped %lu", unsigned(gEntryCount),
             static_cast<unsigned long>(gBusRate), static_cast<unsigned long>(link().monitorDropped()));
    const int y = kH - kSnifferFooterH;
    lcd.fillRect(0, y, kW, kSnifferFooterH, kHeaderBg);
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setTextColor(link().monitorDropped() ? kWarn : kDim, kHeaderBg);
    lcd.drawString(buf, 4, y + kSnifferFooterH / 2);
}

void snifferNote() { setHeaderNote(gSnifferPaused ? "PAUSED" : link().monitorIsLossless() ? "" : "lossy"); }

void snifferEnter() {
    gTitle = "Sniffer";
    gHeaderNote[0] = '\0';
    drawHeader();
    gSnifferOk = openLink() && link().startMonitor();
    if (!gSnifferOk) {
        if (link().connected()) message("Could not start monitor mode", nullptr, kBad);
        return;
    }
    clearBody();
    snifferNote();
    for (SnifferRow& r : gRows) r = SnifferRow();
    drawSnifferFooter();
}

void snifferLeave() {
    if (gSnifferOk) link().stopMonitor();
    gSnifferOk = false;
}

void snifferTick() {
    if (!gSnifferOk) return;
    if (!link().connected()) {
        gSnifferOk = false;
        message(link().lastError(), "SELECT: reconnect", kWarn);
        return;
    }
    const uint32_t now = millis();
    CanFrame f;
    for (int i = 0; i < kSnifferFramesPerTick && link().readMonitor(f); ++i) {
        ++gSnifferFrames;
        if (!gSnifferPaused) snifferIngest(f, now);
    }

    if (now - gLastRateCalc >= 1000) {
        gBusRate = gSnifferFrames - gFramesAtLastRate;
        gFramesAtLastRate = gSnifferFrames;
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
    if (!gSnifferOk) {
        if (ev == Event::Select) snifferEnter();
        return;
    }
    const size_t maxTop = gEntryCount > size_t(kSnifferRows) ? gEntryCount - kSnifferRows : 0;
    if (ev == Event::Up && gSnifferTop > 0) --gSnifferTop;
    if (ev == Event::Down && gSnifferTop < maxTop) ++gSnifferTop;
    // Left/right jump a screenful.
    if (ev == Event::Left) gSnifferTop = gSnifferTop > size_t(kSnifferRows) ? gSnifferTop - kSnifferRows : 0;
    if (ev == Event::Right) gSnifferTop = min(gSnifferTop + kSnifferRows, maxTop);
    if (ev == Event::Select) {
        gSnifferPaused = !gSnifferPaused;
        snifferNote();
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
        char ecu[12], buf[28];
        ecuLabel(d.ecuId, ecu, sizeof(ecu));
        snprintf(buf, sizeof(buf), "%s  %s", d.pending ? "pending" : "stored", ecu);
        lcd.drawString(buf, 100, y + 6);
        y += kLineH;
    }
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(textdatum_t::bottom_center);
    lcd.setTextColor(kDim, kBg);
    lcd.drawString("Press: re-read   Left/hold: back", kW / 2, kH - 4);
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
    y += 16;
    lcd.setTextColor(color, kBg);
    // Values may span several '\n'-separated lines.
    char buf[128];
    strlcpy(buf, value, sizeof(buf));
    for (char* line = strtok(buf, "\n"); line; line = strtok(nullptr, "\n")) {
        lcd.drawString(line, 8, y);
        y += 16;
    }
    y += 8;
}

void infoEnter() {
    gTitle = "Vehicle info";
    gHeaderNote[0] = '\0';
    drawHeader();
    if (!openObd()) return;
    message("Reading VIN...");
    char vin[18] = "";
    const bool haveVin = obd::readVin(vin, sizeof(vin));

    clearBody();
    int y = kHeaderH + 8;
    char buf[128];
    infoLine(y, "VIN", haveVin ? vin : "not reported", haveVin ? kFg : kDim);
    link().describe(buf, sizeof(buf));
    infoLine(y, "Connection", buf);
    snprintf(buf, sizeof(buf), "%u", unsigned(obd::ecuCount()));
    infoLine(y, "OBD ECUs answering", buf);
    size_t total = 0, supported = 0;
    const obd::PidInfo* pids = obd::livePids(total);
    for (size_t i = 0; i < total; ++i) supported += obd::isSupported(pids[i].pid);
    snprintf(buf, sizeof(buf), "%u of %u", unsigned(supported), unsigned(total));
    infoLine(y, "Live PIDs supported", buf);
    const float v = headerVolts();
    if (v > 0) snprintf(buf, sizeof(buf), "%.2f V", v);
    else strlcpy(buf, "unknown", sizeof(buf));
    infoLine(y, "Battery at OBD port", buf);
}

void infoEvent(Event ev) {
    if (ev == Event::Select) infoEnter();
}

// ---- Settings ----

constexpr uint32_t kRateChoices[] = {0, 500000, 250000, 125000};  // 0 = auto
constexpr uint8_t kBrightChoices[] = {40, 100, 160, 200, 255};

enum SettingItem { kSetLink, kSetWifi, kSetRate, kSetBright, kSetReconnect, kSetCount };
int gSetSel = 0;
constexpr int kSetRowH = 52;

void drawSettings() {
    clearBody();
    const settings::Settings& s = settings::get();
    const bool elm = s.link == settings::LinkKind::Elm327Wifi;
    static const char* const kLabels[kSetCount] = {"Connection", "Dongle WiFi network", "CAN bitrate (direct)",
                                                   "Brightness", "Reconnect"};
    char value[64];
    for (int i = 0; i < kSetCount; ++i) {
        const int y = kHeaderH + 6 + i * kSetRowH;
        const bool sel = i == gSetSel;
        // Settings that don't apply to the current link are greyed out.
        const bool applies = !((i == kSetWifi && !elm) || (i == kSetRate && elm) ||
                               (i == kSetLink && !config::kDirectCanAvailable));
        const uint16_t bg = sel ? kSelectBg : kBg;
        lcd.fillRoundRect(8, y, kW - 16, kSetRowH - 6, 6, bg);
        if (sel) lcd.drawRoundRect(8, y, kW - 16, kSetRowH - 6, 6, kAccent);
        lcd.setFont(&fonts::Font2);
        lcd.setTextDatum(textdatum_t::top_left);
        lcd.setTextColor(kDim, bg);
        lcd.drawString(kLabels[i], 18, y + 5);
        switch (i) {
            case kSetLink:
                strlcpy(value, elm ? "ELM327 over WiFi" : "Direct CAN transceiver", sizeof(value));
                if (!config::kDirectCanAvailable) strlcat(value, " (only)", sizeof(value));
                break;
            case kSetWifi:
                snprintf(value, sizeof(value), "%s%s", s.wifiSsid, s.wifiPass[0] ? " (pw set)" : "");
                break;
            case kSetRate: rateLabel(s.canBitrate, value, sizeof(value)); break;
            case kSetBright: snprintf(value, sizeof(value), "%u%%", unsigned(s.brightness * 100 / 255)); break;
            default: strlcpy(value, link().connected() ? "connected - press to redo" : "press SELECT", sizeof(value));
        }
        lcd.setTextColor(applies ? kFg : kDim, bg);
        lcd.drawString(value, 18, y + 23);
    }
}

void settingsEnter() {
    gTitle = "Settings";
    gHeaderNote[0] = '\0';
    drawHeader();
    drawSettings();
}

// Left/right step a value back/forward; press does the same as right.
void settingsEvent(Event ev) {
    settings::Settings& s = settings::get();
    if (ev == Event::Up) gSetSel = (gSetSel + kSetCount - 1) % kSetCount;
    if (ev == Event::Down) gSetSel = (gSetSel + 1) % kSetCount;
    const int step = ev == Event::Left ? -1 : (ev == Event::Right || ev == Event::Select) ? 1 : 0;
    if (step != 0) {
        switch (gSetSel) {
            case kSetLink:
                if (!config::kDirectCanAvailable) break;  // only one choice
                s.link = s.link == settings::LinkKind::Elm327Wifi ? settings::LinkKind::DirectCan
                                                                    : settings::LinkKind::Elm327Wifi;
                obdlink::select(s.link);
                gDiscovered = false;
                break;
            case kSetWifi:
                if (step > 0 && s.link == settings::LinkKind::Elm327Wifi) {
                    go(Screen::WifiPick);
                    return;
                }
                break;
            case kSetRate: {
                constexpr size_t n = sizeof(kRateChoices) / sizeof(kRateChoices[0]);
                size_t i = 0;
                while (i < n && kRateChoices[i] != s.canBitrate) ++i;
                s.canBitrate = kRateChoices[(i + n + step) % n];
                if (s.link == settings::LinkKind::DirectCan) link().disconnect();
                break;
            }
            case kSetBright: {
                constexpr int n = sizeof(kBrightChoices) / sizeof(kBrightChoices[0]);
                int i = 0;
                while (i < n - 1 && kBrightChoices[i] < s.brightness) ++i;
                i = constrain(i + step, 0, n - 1);  // no wrap: dim stays dim
                s.brightness = kBrightChoices[i];
                lcd.setBrightness(s.brightness);
                break;
            }
            default:
                if (step < 0) break;
                link().disconnect();
                if (openLink()) {
                    message("Connected", link().shortName(), kGood);
                    delay(800);
                } else {
                    delay(2000);  // leave the failure reason up for a moment
                }
                break;
        }
        settings::save();
    }
    drawSettings();
}

// ---- WiFi network picker ----

struct Network {
    char ssid[33];
    int32_t rssi;
    bool open;
    bool likelyObd;
};

constexpr size_t kMaxNetworks = 16;
constexpr int kNetRowH = 30;
constexpr int kNetRows = (kH - kHeaderH - 24) / kNetRowH;
Network gNets[kMaxNetworks];
size_t gNetCount = 0;
size_t gNetSel = 0;

// Names the common ELM327 WiFi clones use for their access point.
bool looksLikeObd(const char* ssid) {
    static const char* const kHints[] = {"OBD", "ELM", "V-LINK", "VLINK", "VGATE", "ICAR", "KONNWEI", "CAR"};
    char upper[33];
    size_t n = 0;
    for (; ssid[n] && n < 32; ++n) upper[n] = static_cast<char>(toupper(static_cast<unsigned char>(ssid[n])));
    upper[n] = '\0';
    for (const char* hint : kHints) {
        if (strstr(upper, hint)) return true;
    }
    return false;
}

void drawNetworks() {
    clearBody();
    if (gNetCount == 0) {
        message("No WiFi networks found", "SELECT: scan again", kWarn);
        return;
    }
    const size_t top = gNetSel >= size_t(kNetRows) ? gNetSel - kNetRows + 1 : 0;
    for (size_t i = top; i < gNetCount && i < top + kNetRows; ++i) {
        const Network& n = gNets[i];
        const int y = kHeaderH + 4 + (i - top) * kNetRowH;
        const bool sel = i == gNetSel;
        const uint16_t bg = sel ? kSelectBg : kBg;
        lcd.fillRoundRect(4, y, kW - 8, kNetRowH - 3, 4, bg);
        lcd.setFont(&fonts::Font2);
        lcd.setTextDatum(textdatum_t::middle_left);
        lcd.setTextColor(n.likelyObd ? kAccent : kFg, bg);
        lcd.drawString(n.ssid, 10, y + (kNetRowH - 3) / 2);
        char meta[16];
        snprintf(meta, sizeof(meta), "%s%ld", n.open ? "" : "lock ", static_cast<long>(n.rssi));
        lcd.setTextDatum(textdatum_t::middle_right);
        lcd.setTextColor(kDim, bg);
        lcd.drawString(meta, kW - 10, y + (kNetRowH - 3) / 2);
    }
    lcd.setTextDatum(textdatum_t::bottom_center);
    lcd.setTextColor(kDim, kBg);
    lcd.drawString("Press: use   Left/hold: back", kW / 2, kH - 4);
}

void scanNetworks() {
    message("Scanning for WiFi...");
    link().disconnect();
    WiFi.mode(WIFI_STA);
    const int found = WiFi.scanNetworks();
    gNetCount = 0;
    for (int i = 0; i < found && gNetCount < kMaxNetworks; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;  // hidden network
        bool dup = false;
        for (size_t j = 0; j < gNetCount; ++j) dup |= strcmp(gNets[j].ssid, ssid.c_str()) == 0;
        if (dup) continue;
        Network& n = gNets[gNetCount++];
        strlcpy(n.ssid, ssid.c_str(), sizeof(n.ssid));
        n.rssi = WiFi.RSSI(i);
        n.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        n.likelyObd = looksLikeObd(n.ssid);
    }
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    // Likely dongles first, then strongest signal.
    std::sort(gNets, gNets + gNetCount, [](const Network& a, const Network& b) {
        if (a.likelyObd != b.likelyObd) return a.likelyObd;
        return a.rssi > b.rssi;
    });
    gNetSel = 0;
}

void wifiEnter() {
    gTitle = "Pick dongle";
    gHeaderNote[0] = '\0';
    drawHeader();
    scanNetworks();
    drawNetworks();
}

void wifiEvent(Event ev) {
    if (gNetCount == 0) {
        if (ev == Event::Select) wifiEnter();
        return;
    }
    if (ev == Event::Up && gNetSel > 0) --gNetSel;
    if (ev == Event::Down && gNetSel + 1 < gNetCount) ++gNetSel;
    if (ev == Event::Select) {
        settings::Settings& s = settings::get();
        const Network& n = gNets[gNetSel];
        if (strcmp(s.wifiSsid, n.ssid) != 0) s.wifiPass[0] = '\0';  // old password was for another network
        strlcpy(s.wifiSsid, n.ssid, sizeof(s.wifiSsid));
        settings::save();
        if (!n.open && !s.wifiPass[0]) {
            message("This network needs a password", "Set it over USB: pass <password>", kWarn);
            delay(2500);
        }
        go(Screen::Settings);
        return;
    }
    drawNetworks();
}

// ---- Dispatch ----

constexpr ScreenDef kScreens[] = {
    {menuEnter, noop, menuEvent, noop},
    {liveEnter, liveTick, liveEvent, noop},
    {snifferEnter, snifferTick, snifferEvent, snifferLeave},
    {dtcEnter, noop, dtcEvent, noop},
    {infoEnter, noop, infoEvent, noop},
    {settingsEnter, noop, settingsEvent, noop},
    {wifiEnter, noop, wifiEvent, noop},
};

Screen gScreen = Screen::Menu;

void go(Screen s) {
    kScreens[static_cast<int>(gScreen)].leave();
    gScreen = s;
    kScreens[static_cast<int>(s)].enter();
    updateHeaderStatus();
}

}  // namespace

void begin() {
    lcd.init();
    lcd.setRotation(0);  // portrait, connector at the top
    lcd.setBrightness(settings::get().brightness);
    lcd.fillScreen(kBg);
    gRow.setColorDepth(16);
    gRow.createSprite(kW, kSnifferRowH);
    gTile.setColorDepth(16);
    gTile.createSprite(kTileW, kTileH);
    go(Screen::Menu);
}

void loop() {
    const Event ev = buttons::poll();
    // Screens that have no sideways action treat LEFT as back.
    const bool leftIsBack = gScreen == Screen::Dtc || gScreen == Screen::Info || gScreen == Screen::WifiPick;
    const Event action = (ev == Event::Left && leftIsBack) ? Event::Back : ev;
    if (action == Event::Back && gScreen == Screen::WifiPick) {
        go(Screen::Settings);
    } else if (action == Event::Back && gScreen != Screen::Menu) {
        go(Screen::Menu);
    } else if (ev != Event::None) {
        kScreens[static_cast<int>(gScreen)].event(ev);
    }
    kScreens[static_cast<int>(gScreen)].tick();
    updateHeaderStatus();
}

void refresh() { go(gScreen); }

}  // namespace ui
