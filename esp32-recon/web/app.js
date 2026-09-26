"use strict";

// Renders everything the host recon service (host/recon_service.py) has
// recorded, plus the investigation layer: aliases, notes, pins, hide, filter,
// signal sparklines, and export. The service owns the serial port and the
// SQLite database. This page loads every device once, then polls for devices
// updated since the last sweep it has, and writes annotations back.
//
// Every broadcast string (SSIDs, BLE names, probed network names) reaches the
// DOM via textContent. User-entered aliases/notes are also set via textContent.
// Never introduce innerHTML.

const POLL_MS = 2000;
// A dual-band WiFi scan (ESP32-C5) prints nothing for ~17 s, so allow longer.
const SILENCE_WARNING_MS = 30_000;
const NEW_DEVICE_MS = 20_000;        // First-seen within this window gets a NEW badge.
const SIG_BASE_UUID = /^0000([0-9a-f]{4})-0000-1000-8000-00805f9b34fb$/i;

// Before the database existed, investigation state lived in localStorage.
const LEGACY_KEYS = { alias: "recon.alias", note: "recon.note", pin: "recon.pin", ignore: "recon.ignore" };
const MIGRATED_FLAG = "recon.migratedToDb";

// Mirror of the database's devices table, per page table: normAddr -> record.
const seen = { wifi: new Map(), ble: new Map(), clients: new Map(), aps: new Map() };
const latestSweep = { wifi: 0, ble: 0, clients: 0, aps: 0 };
const expanded = { wifi: new Set(), ble: new Set(), clients: new Set(), aps: new Set() };
let cursor = null;          // Newest sweep id merged into `seen`; null = load everything.
let channels = [];
let alerts = [];
let lastMessage = null;

let serial = null;          // Board connection status reported by the service.
let serviceUp = false;
let migrationChecked = false;

// Investigation state, from the database: normAddr -> {alias, note, pinned, hidden}.
let notes = {};
let filterText = "";
let frozen = false;
let showHidden = false;

const $ = (id) => document.getElementById(id);
const normAddr = (a) => String(a ?? "").toUpperCase();

// --- investigation accessors (also the test API on window.recon) -----------

// Applies the change locally at once, then saves it and adopts the stored result.
async function annotate(addr, changes) {
  const key = normAddr(addr);
  notes[key] = { alias: "", note: "", pinned: false, hidden: false, ...notes[key], ...changes };
  render();
  try {
    const res = await fetch(`/api/annotations/${encodeURIComponent(key)}`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(changes),
    });
    if (!res.ok) throw new Error((await res.json().catch(() => ({}))).error ?? `HTTP ${res.status}`);
    notes[key] = await res.json();
  } catch (err) {
    setStatus(`Could not save to the database: ${err.message}`, false);
  }
  render();
}
const setAlias = (addr, name) => annotate(addr, { alias: String(name ?? "").trim() });
const setNote = (addr, text) => annotate(addr, { note: String(text ?? "").trim() });
const togglePin = (addr) => annotate(addr, { pinned: !isPinned(addr) });
const toggleIgnore = (addr) => annotate(addr, { hidden: !isIgnored(addr) });
const aliasOf = (addr) => notes[normAddr(addr)]?.alias || "";
const noteOf = (addr) => notes[normAddr(addr)]?.note || "";
const isPinned = (addr) => !!notes[normAddr(addr)]?.pinned;
const isIgnored = (addr) => !!notes[normAddr(addr)]?.hidden;

// ---------------------------------------------------------------- service --

const ADDR_KEY = { wifi: "bssid", ble: "addr", clients: "mac", aps: "bssid" };
const addrOf = (type, e) => e[ADDR_KEY[type]];
// In the table's most recent sweep. Computed here, not by the service, because
// a cached record goes stale when a newer sweep arrives without it.
const isFresh = (type, e) => e.lastSweep === latestSweep[type];

async function poll() {
  try {
    const url = cursor == null ? "/api/state" : `/api/state?after=${cursor}`;
    const res = await fetch(url, { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const state = await res.json();
    serviceUp = true;
    serial = state.serial;
    // While frozen, nothing is merged and the cursor stays put, so the first
    // poll after unfreezing fetches everything that changed meanwhile.
    if (!frozen && apply(state)) render();
  } catch {
    serviceUp = false;
  }
  updateStatus();
  if (serviceUp && !migrationChecked) {
    migrationChecked = true;
    offerLegacyImport();
  }
}

// Merges one /api/state response. Returns true if anything visible changed.
function apply(state) {
  if (cursor != null && state.cursor < cursor) {
    // The database was replaced (sweep ids went backwards): start over.
    for (const map of Object.values(seen)) map.clear();
    cursor = null;
    poll();
    return false;
  }
  let changed = cursor == null || state.cursor !== cursor;
  for (const type of Object.keys(seen)) {
    const table = state.tables[type];
    for (const e of table.rows) seen[type].set(normAddr(addrOf(type, e)), e);
    latestSweep[type] = table.latestSweep;
  }
  const annotations = JSON.stringify(state.annotations);
  if (annotations !== JSON.stringify(notes)) changed = true;
  cursor = state.cursor;
  channels = state.channels;
  alerts = state.alerts;
  notes = state.annotations;
  lastMessage = state.last;
  return changed;
}

function updateStatus() {
  if (!serviceUp) {
    setStatus("Can't reach the recon service. Start it with: python host/recon_service.py", false);
  } else if (!serial.connected) {
    setStatus(`Board not connected${serial.error ? ` (${serial.error})` : ""}. Showing recorded data.`, false);
  } else {
    const quiet = Date.now() - Math.max(serial.since, serial.lastLineAt);
    setStatus(
      quiet > SILENCE_WARNING_MS
        ? `Connected to ${serial.port}, but the board has sent nothing for ${Math.round(quiet / 1000)}s. Try unplugging it and reconnecting.`
        : `Recording from ${serial.port}${serial.banner ? ` · ${serial.banner}` : ""}`,
      true,
    );
  }
}

// One-time offer to move aliases/notes/pins/hidden flags saved by the old,
// localStorage-only page into the database. localStorage is left untouched.
async function offerLegacyImport() {
  if (localStorage.getItem(MIGRATED_FLAG)) return;
  const legacy = {};
  for (const [field, key] of Object.entries(LEGACY_KEYS)) {
    try {
      legacy[field] = JSON.parse(localStorage.getItem(key) || "{}");
    } catch {
      legacy[field] = {};
    }
  }
  const count = new Set(Object.values(legacy).flatMap((m) => Object.keys(m))).size;
  if (!count) {
    localStorage.setItem(MIGRATED_FLAG, "nothing");
    return;
  }
  if (!confirm(`Import aliases, notes, pins and hidden flags for ${count} device(s) saved in this browser into the recon database?\n\nExisting database entries are never overwritten.`)) {
    localStorage.setItem(MIGRATED_FLAG, "declined");
    return;
  }
  try {
    const res = await fetch("/api/annotations/import", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(legacy),
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    localStorage.setItem(MIGRATED_FLAG, "imported");
    poll();
  } catch (err) {
    // Flag not set, so the offer comes back on the next page load.
    setStatus(`Import failed: ${err.message}`, false);
  }
}

// --------------------------------------------------------------- filter ----

function matchesFilter(type, entry) {
  if (!filterText) return true;
  const addr = addrOf(type, entry);
  const parts = [
    addr, aliasOf(addr), noteOf(addr),
    entry.ssid, entry.name, entry.vendor, entry.mfg, entry.product,
    ...(entry.probes ?? []),
  ];
  return parts.some((p) => p && String(p).toLowerCase().includes(filterText));
}

// Pinned first, then latest-sweep, then strongest signal.
function ordered(type) {
  return [...seen[type].values()].sort((a, b) => {
    const ap = isPinned(addrOf(type, a)), bp = isPinned(addrOf(type, b));
    if (ap !== bp) return ap ? -1 : 1;
    if (isFresh(type, a) !== isFresh(type, b)) return isFresh(type, a) ? -1 : 1;
    return (b.rssi ?? -999) - (a.rssi ?? -999);
  });
}

// --------------------------------------------------------------- columns ---

const text = (v) => (v == null || v === "" ? null : String(v));
// 2.4 GHz uses channels 1-14; every 5 GHz channel number is 32 or higher.
const bandOf = (ch) => (ch == null ? null : ch > 14 ? "5 GHz" : "2.4 GHz");

const WIFI_COLUMNS = [
  { label: "SSID", key: "ssid", get: (e) => e.ssid, placeholder: "hidden" },
  { label: "BSSID", key: "bssid", cls: "mono", addr: true, get: (e) => e.bssid },
  { label: "Signal", key: "rssi", node: (e) => signalNode(e) },
  { label: "Ch", key: "ch", get: (e) => e.ch },
  { label: "Band", key: "ch", get: (e) => bandOf(e.ch) },
  { label: "Security", key: "auth", get: (e) => e.auth },
  { label: "Cipher", key: "cipher", get: (e) => text(e.cipher), placeholder: "—" },
  { label: "WPS", key: "wps", get: (e) => (e.wps ? "on" : null), placeholder: "—" },
  { label: "Vendor", key: "vendor", get: (e) => text(e.vendor), placeholder: "—" },
  { label: "Country", key: "country", get: (e) => text(e.country), placeholder: "—" },
  { label: "PHY", key: "phy", get: (e) => text(e.phy), placeholder: "—" },
  { label: "Flag", key: "flag", cls: "flag", get: (e) => text(e.flag) },
  { label: "Last seen", age: true },
];

const BLE_COLUMNS = [
  { label: "Address", key: "addr", cls: "mono", addr: true, get: (e) => e.addr },
  { label: "Type", key: "type", get: (e) => e.type },
  { label: "Signal", key: "rssi", node: (e) => signalNode(e) },
  { label: "Name", key: "name", get: (e) => text(e.name), placeholder: "—" },
  { label: "Product", key: "product", get: (e) => text(e.product), placeholder: "—" },
  { label: "Appearance", key: "appearance", get: (e) => text(e.appearance), placeholder: "—" },
  { label: "Manufacturer", key: "mfg", get: (e) => text(e.mfg), placeholder: "—" },
  { label: "Dist", key: "dist", get: (e) => (e.dist == null ? null : `${e.dist} m`), placeholder: "—" },
  { label: "TX", key: "tx", get: (e) => (e.tx == null ? null : `${e.tx} dBm`), placeholder: "—" },
  { label: "Services", key: "svc", cls: "wrap mono", node: (e) => tagsNode((e.svc ?? []).map(shortUuid)) },
  { label: "Last seen", age: true },
];

const CLIENT_COLUMNS = [
  { label: "Device MAC", key: "mac", cls: "mono", addr: true, get: (e) => e.mac },
  { label: "Vendor", key: "vendor", get: (e) => (e.rand ? "randomized" : text(e.vendor)), placeholder: "—" },
  { label: "Signal", key: "rssi", node: (e) => signalNode(e) },
  { label: "Frames", key: "frames", get: (e) => e.frames },
  { label: "Looking for", key: "probes", cls: "wrap", node: (e) => tagsNode(e.probes) },
  { label: "Last seen", age: true },
];

const AP_COLUMNS = [
  { label: "BSSID", key: "bssid", cls: "mono", addr: true, get: (e) => e.bssid },
  { label: "SSID", key: "ssid", get: (e) => text(e.ssid), placeholder: "hidden" },
  { label: "Vendor", key: "vendor", get: (e) => text(e.vendor), placeholder: "—" },
  { label: "Clients", key: "clients", get: (e) => e.clients },
  { label: "Frames", key: "frames", get: (e) => e.frames },
  { label: "Last seen", age: true },
];

const TABLES = {
  wifi: WIFI_COLUMNS, ble: BLE_COLUMNS, clients: CLIENT_COLUMNS, aps: AP_COLUMNS,
};
// Fields the service adds to each record; not shown in the details panel.
const INTERNAL = new Set(["firstSeen", "lastSeen", "lastSweep", "history"]);

// ---------------------------------------------------------------- render ---

function render() {
  const now = Date.now();

  let hiddenTotal = 0;
  for (const [type, columns] of Object.entries(TABLES)) {
    hiddenTotal += renderTable(type, columns, now);
  }
  renderChannels();
  renderAlerts();

  $("hidden-count").textContent = hiddenTotal ? `(${hiddenTotal})` : "";
  $("filter-info").textContent = filterText ? `filtering: “${filterText}”` : "";
  tick();
}

// Updates the relative times in place once a second. Re-rendering thousands of
// rows just to advance "Ns ago" would be wasteful.
function tick() {
  const now = Date.now();
  for (const td of document.querySelectorAll("td.age[data-ts]")) {
    td.textContent = ago(now - Number(td.dataset.ts));
  }
  $("meta").textContent = lastMessage
    ? `sweep #${lastMessage.cycle} · board up ${lastMessage.up}s · updated ${ago(now - lastMessage.at)}`
    : "";
}

// Returns how many rows were hidden by the ignore list (for the counter).
function renderTable(type, columns, now) {
  $(`${type}-head`).replaceChildren(...columns.map((c) => {
    const th = document.createElement("th");
    th.textContent = c.label;
    return th;
  }));

  const all = ordered(type);
  let hidden = 0;
  const rows = all.filter((e) => {
    const ignored = isIgnored(addrOf(type, e));
    if (ignored && !showHidden) { hidden++; return false; }
    return matchesFilter(type, e);
  });

  const fresh = rows.filter((r) => isFresh(type, r)).length;
  const countEl = $(`${type}-count`);
  if (countEl) countEl.textContent = rows.length ? `${fresh} in last sweep, ${rows.length} shown` : "";

  const columnKeys = new Set(columns.map((c) => c.key).filter(Boolean));
  const body = $(`${type}-body`);

  if (!rows.length) {
    const tr = document.createElement("tr");
    const td = document.createElement("td");
    td.className = "empty";
    td.colSpan = columns.length;
    td.textContent = filterText || hidden
      ? "No matching devices."
      : serial?.connected ? "Waiting for data…" : "Nothing recorded yet.";
    tr.appendChild(td);
    body.replaceChildren(tr);
    return hidden;
  }

  const out = [];
  for (const entry of rows) {
    const addr = addrOf(type, entry);
    const id = normAddr(addr);
    const extras = Object.keys(entry).filter((k) => !columnKeys.has(k) && !INTERNAL.has(k));
    const tr = buildRow(type, columns, entry, now, addr, id);
    const cls = [];
    if (isPinned(addr)) cls.push("pinned");
    if (isIgnored(addr)) cls.push("ignored");
    if (!isFresh(type, entry)) cls.push("stale");
    tr.className = cls.join(" ");
    out.push(tr);
    if (expanded[type].has(id)) out.push(detailRow(type, entry, addr, extras, columns.length, now));
  }
  body.replaceChildren(...out);
  return hidden;
}

function buildRow(type, columns, entry, now, addr, id) {
  const tr = document.createElement("tr");
  columns.forEach((col, i) => {
    const td = document.createElement("td");
    if (col.cls) td.className = col.cls;

    if (i === 0) {
      td.prepend(pinControl(addr), toggleControl(type, id));
    }
    if (col.addr) {
      const name = aliasOf(addr);
      if (name) td.appendChild(aliasBadge(name));
    }

    if (col.age) {
      td.className = "age";
      td.dataset.ts = entry.lastSeen;
      td.append(ago(now - entry.lastSeen));
    } else if (col.node) {
      const n = col.node(entry);
      if (n) td.append(n);
      else placeholder(td, col.placeholder);
    } else {
      const v = col.get(entry);
      if (v == null || v === "") placeholder(td, col.placeholder);
      else td.append(document.createTextNode(String(v)));
    }

    if (col.addr && isNew(entry, now)) td.appendChild(newBadge());
    tr.appendChild(td);
  });
  return tr;
}

function detailRow(type, entry, addr, extras, span, now) {
  const tr = document.createElement("tr");
  tr.className = "detail";
  const td = document.createElement("td");
  td.colSpan = span;

  const panel = document.createElement("div");
  panel.className = "panel";

  const add = (label, valueNode) => {
    const dt = document.createElement("dt");
    dt.textContent = label;
    const dd = document.createElement("dd");
    if (valueNode instanceof Node) dd.appendChild(valueNode);
    else dd.textContent = valueNode;
    panel.append(dt, dd);
  };

  add("Alias", aliasOf(addr) || "—");
  add("Note", noteOf(addr) || "—");
  add("First seen", `${new Date(entry.firstSeen).toLocaleString()} (${ago(now - entry.firstSeen)})`);
  for (const key of extras) {
    const v = entry[key];
    add(key, Array.isArray(v) ? (v.length ? v.join(", ") : "—") : String(v));
  }

  const actions = document.createElement("div");
  actions.className = "row";
  actions.append(
    actionButton("Alias…", () => {
      const v = prompt(`Alias for ${addr}`, aliasOf(addr));
      if (v !== null) setAlias(addr, v);
    }),
    actionButton("Note…", () => {
      const v = prompt(`Note for ${addr}`, noteOf(addr));
      if (v !== null) setNote(addr, v);
    }),
    actionButton(isPinned(addr) ? "Unpin" : "Pin", () => togglePin(addr)),
    actionButton(isIgnored(addr) ? "Unhide" : "Hide", () => toggleIgnore(addr)),
    actionButton("Copy address", () => navigator.clipboard?.writeText(addr).catch(() => {})),
  );
  panel.appendChild(actions);

  td.appendChild(panel);
  tr.appendChild(td);
  return tr;
}

// ------------------------------------------------------------ small nodes --

function pinControl(addr) {
  const span = document.createElement("span");
  span.className = "ctrl pin" + (isPinned(addr) ? " on" : "");
  span.textContent = isPinned(addr) ? "★" : "☆";
  span.title = isPinned(addr) ? "Unpin" : "Pin to top";
  span.addEventListener("click", () => togglePin(addr));
  return span;
}

function toggleControl(type, id) {
  const span = document.createElement("span");
  span.className = "ctrl toggle";
  span.textContent = expanded[type].has(id) ? "▾" : "▸";
  span.title = "Details and actions";
  span.addEventListener("click", () => {
    expanded[type].has(id) ? expanded[type].delete(id) : expanded[type].add(id);
    render();
  });
  return span;
}

function aliasBadge(name) {
  const span = document.createElement("span");
  span.className = "alias";
  span.textContent = name;
  return span;
}
function newBadge() {
  const span = document.createElement("span");
  span.className = "newbadge";
  span.textContent = "NEW";
  return span;
}
function actionButton(label, onClick) {
  const b = document.createElement("button");
  b.type = "button";
  b.textContent = label;
  b.addEventListener("click", onClick);
  return b;
}

const isNew = (entry, now) => now - entry.firstSeen < NEW_DEVICE_MS;

const TREND_WINDOW_MS = 5 * 60_000;  // Only recent readings say where a device is now.
const TREND_MIN_SAMPLES = 4;
const TREND_MIN_DB = 6;              // Smaller swings are ordinary multipath noise.

// Given samples [{t, rssi}] oldest -> newest for one device, returns "closer"
// or "farther" when its signal has clearly moved over the last few minutes,
// and "" when it is steady, the data is too thin, or the device has gone quiet.
function signalTrend(samples) {
  if (!samples.length) return "";
  const newest = samples[samples.length - 1].t;
  if (Date.now() - newest > TREND_WINDOW_MS) return "";
  const recent = samples.filter((s) => newest - s.t <= TREND_WINDOW_MS);
  if (recent.length < TREND_MIN_SAMPLES) return "";

  // Compare the average of the oldest and newest few readings rather than two
  // single readings, which can differ by several dB with nothing moving.
  const k = Math.min(3, Math.floor(recent.length / 2));
  const mean = (list) => list.reduce((sum, s) => sum + s.rssi, 0) / list.length;
  const delta = mean(recent.slice(-k)) - mean(recent.slice(0, k));
  if (delta >= TREND_MIN_DB) return "closer";
  if (delta <= -TREND_MIN_DB) return "farther";
  return "";
}

function signalNode(entry) {
  const rssi = entry.rssi;
  if (rssi == null) return null;
  const frag = document.createDocumentFragment();

  const samples = entry.history ?? [];
  if (samples.length >= 2) frag.append(sparkline(samples));

  const bar = document.createElement("span");
  const fill = document.createElement("i");
  const percent = Math.max(0, Math.min(100, ((rssi + 100) / 70) * 100));
  fill.style.width = `${percent}%`;
  bar.className = "bar";
  bar.appendChild(fill);
  frag.append(bar, document.createTextNode(`${rssi} dBm`));

  const trend = signalTrend(samples);
  if (trend) {
    const rising = trend === "closer";
    const badge = document.createElement("span");
    badge.className = "trend " + (rising ? "up" : "down");
    badge.textContent = (rising ? "▲ " : "▼ ") + trend;
    frag.append(badge);
  }
  return frag;
}

function sparkline(samples) {
  const W = 52, H = 16;
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("class", "spark");
  svg.setAttribute("width", W);
  svg.setAttribute("height", H);
  // RSSI roughly -100..-30 mapped to the box, newest on the right.
  const n = samples.length;
  const pts = samples.map((s, i) => {
    const x = (i / (n - 1)) * (W - 2) + 1;
    const norm = Math.max(0, Math.min(1, (s.rssi + 100) / 70));
    const y = H - 1 - norm * (H - 2);
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  });
  const line = document.createElementNS(svg.namespaceURI, "polyline");
  line.setAttribute("points", pts.join(" "));
  line.setAttribute("fill", "none");
  line.setAttribute("stroke", "#5b9dd9");
  line.setAttribute("stroke-width", "1");
  svg.appendChild(line);
  return svg;
}

function placeholder(td, label) {
  const span = document.createElement("span");
  span.className = "none";
  span.textContent = label ?? "";
  td.appendChild(span);
}

function tagsNode(list) {
  if (!Array.isArray(list) || !list.length) return null;
  const frag = document.createDocumentFragment();
  for (const item of list) {
    const span = document.createElement("span");
    span.className = "tag";
    span.textContent = item;
    frag.appendChild(span);
  }
  return frag;
}

function renderChannels() {
  const host = $("channels");
  if (!channels.length) {
    const span = document.createElement("span");
    span.className = "none";
    span.textContent = "No channel activity captured yet.";
    host.replaceChildren(span);
    return;
  }
  const max = Math.max(...channels.map((c) => c.pkts), 1);
  const out = [];
  for (const c of channels) {
    const label = document.createElement("span");
    label.textContent = c.ch;
    const track = document.createElement("div");
    track.className = "track";
    const fill = document.createElement("i");
    fill.style.width = `${(c.pkts / max) * 100}%`;
    track.appendChild(fill);
    const n = document.createElement("span");
    n.className = "n";
    n.textContent = `${c.pkts} pkts`;
    out.push(label, track, n);
  }
  host.replaceChildren(...out);
}

function renderAlerts() {
  const host = $("alerts");
  if (!alerts.length) {
    host.className = "";
    host.replaceChildren();
    return;
  }
  host.className = "show";
  const out = [];
  for (const a of alerts) {
    const line = document.createElement("div");
    const b = document.createElement("b");
    b.textContent = `⚠ ${a.kind}`;
    line.append(b, document.createTextNode(` — ${a.count} deauth/disassoc frames targeting `));
    const mac = document.createElement("b");
    const name = aliasOf(a.bssid);
    mac.textContent = name ? `${name} (${a.bssid})` : a.bssid;
    line.append(mac);
    out.push(line);
  }
  host.replaceChildren(...out);
}

// ---------------------------------------------------------------- export ---

function snapshot() {
  const at = new Date().toISOString();
  const dump = (type) => [...seen[type].values()].map((e) => ({
    ...e,
    alias: aliasOf(addrOf(type, e)) || undefined,
    note: noteOf(addrOf(type, e)) || undefined,
  }));
  return {
    exportedAt: at,
    wifi: dump("wifi"),
    ble: dump("ble"),
    monitor: { clients: dump("clients"), aps: dump("aps"), channels, alerts },
    annotations: notes,
  };
}

function downloadSnapshot() {
  const blob = new Blob([JSON.stringify(snapshot(), null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `recon-${new Date().toISOString().replace(/[:.]/g, "-")}.json`;
  a.click();
  URL.revokeObjectURL(url);
}

// ---------------------------------------------------------------- utils ----

function shortUuid(uuid) {
  const match = SIG_BASE_UUID.exec(uuid);
  return match ? `0x${match[1].toUpperCase()}` : uuid;
}
// First-seen times now span days, so scale the unit.
function ago(ms) {
  const s = Math.round(ms / 1000);
  if (s < 2) return "now";
  if (s < 120) return `${s}s ago`;
  const m = Math.round(s / 60);
  if (m < 120) return `${m}m ago`;
  const h = Math.round(m / 60);
  return h < 48 ? `${h}h ago` : `${Math.round(h / 24)}d ago`;
}
function setStatus(text, live) {
  $("status").textContent = text;
  $("dot").classList.toggle("live", live);
}

// ---------------------------------------------------------------- wiring ---

$("filter").addEventListener("input", (e) => {
  filterText = e.target.value.trim().toLowerCase();
  render();
});
$("freeze").addEventListener("click", () => {
  frozen = !frozen;
  $("freeze").textContent = frozen ? "Frozen — resume" : "Freeze";
  $("freeze").classList.toggle("on", frozen);
  if (!frozen) render();
});
$("show-hidden").addEventListener("change", (e) => {
  showHidden = e.target.checked;
  render();
});
$("export").addEventListener("click", downloadSnapshot);

// Test/automation hook: drive investigation state without the DOM prompts.
window.recon = {
  setAlias, setNote, togglePin, toggleIgnore, snapshot, poll,
  setFilter: (t) => { filterText = String(t).toLowerCase(); render(); },
  setFrozen: (f) => { frozen = f; if (!f) render(); },
  setShowHidden: (v) => { showHidden = v; render(); },
  get state() { return { seen, notes, serial, serviceUp }; },
};

setInterval(poll, POLL_MS);
setInterval(tick, 1000);
poll();
