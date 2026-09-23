"use strict";

// Reads JSON lines from the scanner over Web Serial and renders them, plus a
// client-side investigation layer: aliases, notes, pins, hide, filter, signal
// sparklines, and export. All investigation state lives in localStorage; the
// board stays stateless.
//
// Every broadcast string (SSIDs, BLE names, probed network names) reaches the
// DOM via textContent. User-entered aliases/notes are also set via textContent.
// Never introduce innerHTML.

const NANO_ESP32 = { usbVendorId: 0x2341, usbProductId: 0x0070 };
const BAUD_RATE = 115200;
const FORGET_AFTER_MS = 60_000;
const SILENCE_WARNING_MS = 20_000;
const NEW_DEVICE_MS = 20_000;        // First-seen within this window gets a NEW badge.
const HISTORY_POINTS = 40;           // RSSI samples kept per address for the sparkline.
const HISTORY_ADDRS = 600;           // Cap on tracked addresses (bounds memory).
const SIG_BASE_UUID = /^0000([0-9a-f]{4})-0000-1000-8000-00805f9b34fb$/i;

const seen = { wifi: new Map(), ble: new Map(), clients: new Map(), aps: new Map() };
const sweepAt = { wifi: 0, ble: 0, clients: 0, aps: 0 };
const expanded = { wifi: new Set(), ble: new Set(), clients: new Set(), aps: new Set() };
const history = new Map();  // normAddr -> [{ t, rssi }]
let channels = [];
let alerts = [];
let lastMessage = null;

let activePort = null;
let activeReader = null;
let connectedAt = 0;
let bytesSinceConnect = 0;

// Investigation state, persisted.
const store = {
  alias: loadObj("recon.alias"),
  note: loadObj("recon.note"),
  pin: loadObj("recon.pin"),
  ignore: loadObj("recon.ignore"),
};
let filterText = "";
let frozen = false;
let showHidden = false;

const $ = (id) => document.getElementById(id);
const normAddr = (a) => String(a ?? "").toUpperCase();

function loadObj(key) {
  try {
    return JSON.parse(localStorage.getItem(key) || "{}");
  } catch {
    return {};
  }
}
function persist(key, obj) {
  try {
    localStorage.setItem(key, JSON.stringify(obj));
  } catch {
    /* private mode / quota: aliases just won't persist. */
  }
}

// --- investigation accessors (also the test API on window.recon) -----------

function setAlias(addr, name) {
  const key = normAddr(addr);
  if (name && name.trim()) store.alias[key] = name.trim();
  else delete store.alias[key];
  persist("recon.alias", store.alias);
  render();
}
function setNote(addr, text) {
  const key = normAddr(addr);
  if (text && text.trim()) store.note[key] = text.trim();
  else delete store.note[key];
  persist("recon.note", store.note);
  render();
}
function togglePin(addr) {
  const key = normAddr(addr);
  if (store.pin[key]) delete store.pin[key];
  else store.pin[key] = true;
  persist("recon.pin", store.pin);
  render();
}
function toggleIgnore(addr) {
  const key = normAddr(addr);
  if (store.ignore[key]) delete store.ignore[key];
  else store.ignore[key] = true;
  persist("recon.ignore", store.ignore);
  render();
}
const aliasOf = (addr) => store.alias[normAddr(addr)] || "";
const noteOf = (addr) => store.note[normAddr(addr)] || "";
const isPinned = (addr) => !!store.pin[normAddr(addr)];
const isIgnored = (addr) => !!store.ignore[normAddr(addr)];

// ---------------------------------------------------------------- serial ---

async function connect() {
  if (!("serial" in navigator)) {
    setStatus("Web Serial unavailable. Use Chrome or Edge on http://localhost.", false);
    return;
  }
  try {
    const port = await navigator.serial.requestPort({ filters: [NANO_ESP32] });
    await readFrom(port);
  } catch (err) {
    if (err.name !== "NotFoundError") setStatus(err.message, false);
  }
}

async function disconnect() {
  if (activeReader) await activeReader.cancel().catch(() => {});
}

async function readFrom(port) {
  if (activePort) return;
  try {
    await port.open({ baudRate: BAUD_RATE });
  } catch (err) {
    setStatus(`Could not open the port (${err.message}). Close pio device monitor if it is running.`, false);
    return;
  }

  await port.setSignals({ dataTerminalReady: true, requestToSend: true }).catch(() => {});
  activePort = port;
  connectedAt = Date.now();
  bytesSinceConnect = 0;
  setStatus("Connected. Waiting for the next sweep…", true);

  const decoder = new TextDecoderStream();
  const pipe = port.readable.pipeTo(decoder.writable).catch(() => {});
  activeReader = decoder.readable.getReader();

  let buffer = "";
  try {
    for (;;) {
      const { value, done } = await activeReader.read();
      if (done) break;
      bytesSinceConnect += value.length;
      buffer += value;
      let newline;
      while ((newline = buffer.indexOf("\n")) >= 0) {
        handleLine(buffer.slice(0, newline).trim());
        buffer = buffer.slice(newline + 1);
      }
    }
  } catch {
    // Board unplugged or reset (e.g. while flashing).
  } finally {
    activeReader.releaseLock();
    activeReader = null;
    await pipe;
    await port.close().catch(() => {});
    activePort = null;
    setStatus("Disconnected", false);
  }
}

// ---------------------------------------------------------------- data -----

const ADDR_KEY = { wifi: "bssid", ble: "addr", clients: "mac", aps: "bssid" };
const addrOf = (type, e) => e[ADDR_KEY[type]];

function recordHistory(addr, rssi) {
  if (rssi == null) return;
  const key = normAddr(addr);
  let arr = history.get(key);
  if (!arr) {
    if (history.size >= HISTORY_ADDRS) history.delete(history.keys().next().value);
    arr = [];
    history.set(key, arr);
  }
  arr.push({ t: Date.now(), rssi });
  if (arr.length > HISTORY_POINTS) arr.shift();
}

function mergeInto(type, items, idKey) {
  const now = Date.now();
  const entries = seen[type];
  for (const item of items) {
    const key = item[idKey];
    if (key == null) continue;
    const previous = entries.get(key);
    entries.set(key, { ...item, firstSeen: previous?.firstSeen ?? now, lastSeen: now });
    recordHistory(key, item.rssi);
  }
  sweepAt[type] = now;
}

function handleLine(line) {
  if (!line.startsWith("{")) return;
  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    return;
  }

  if (msg.t === "wifi" && Array.isArray(msg.items)) {
    mergeInto("wifi", msg.items, "bssid");
  } else if (msg.t === "ble" && Array.isArray(msg.items)) {
    mergeInto("ble", msg.items, "addr");
  } else if (msg.t === "monitor") {
    mergeInto("clients", msg.clients ?? [], "mac");
    mergeInto("aps", msg.aps ?? [], "bssid");
    channels = msg.channels ?? [];
    alerts = msg.alerts ?? [];
  } else {
    return;
  }

  lastMessage = { cycle: msg.cycle, up: msg.up, at: Date.now() };
  if (activePort) setStatus("Connected", true);
  if (!frozen) render();
}

function forgetOld(now) {
  for (const entries of Object.values(seen)) {
    for (const [key, entry] of entries) {
      if (now - entry.lastSeen > FORGET_AFTER_MS) entries.delete(key);
    }
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
  const fresh = sweepAt[type];
  return [...seen[type].values()].sort((a, b) => {
    const ap = isPinned(addrOf(type, a)), bp = isPinned(addrOf(type, b));
    if (ap !== bp) return ap ? -1 : 1;
    const af = a.lastSeen === fresh, bf = b.lastSeen === fresh;
    if (af !== bf) return af ? -1 : 1;
    return (b.rssi ?? -999) - (a.rssi ?? -999);
  });
}

// --------------------------------------------------------------- columns ---

const text = (v) => (v == null || v === "" ? null : String(v));

const WIFI_COLUMNS = [
  { label: "SSID", key: "ssid", get: (e) => e.ssid, placeholder: "hidden" },
  { label: "BSSID", key: "bssid", cls: "mono", addr: true, get: (e) => e.bssid },
  { label: "Signal", key: "rssi", node: (e) => signalNode(e) },
  { label: "Ch", key: "ch", get: (e) => e.ch },
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
const INTERNAL = new Set(["firstSeen", "lastSeen"]);

// ---------------------------------------------------------------- render ---

function render() {
  const now = Date.now();
  forgetOld(now);

  let hiddenTotal = 0;
  for (const [type, columns] of Object.entries(TABLES)) {
    hiddenTotal += renderTable(type, columns, now);
  }
  renderChannels();
  renderAlerts();

  $("hidden-count").textContent = hiddenTotal ? `(${hiddenTotal})` : "";
  $("filter-info").textContent = filterText ? `filtering: “${filterText}”` : "";

  if (activePort && bytesSinceConnect === 0 && now - connectedAt > SILENCE_WARNING_MS) {
    setStatus("Connected, but the board has sent nothing. Try unplugging it and reconnecting.", true);
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

  const fresh = rows.filter((r) => r.lastSeen === sweepAt[type]).length;
  const countEl = $(`${type}-count`);
  if (countEl) countEl.textContent = rows.length ? `${fresh} in last sweep, ${rows.length} shown` : "";

  const columnKeys = new Set(columns.map((c) => c.key).filter(Boolean));
  const body = $(`${type}-body`);

  if (!rows.length) {
    const tr = document.createElement("tr");
    const td = document.createElement("td");
    td.className = "empty";
    td.colSpan = columns.length;
    td.textContent = activePort
      ? (filterText || hidden ? "No matching devices." : "Waiting for data…")
      : "Connect the scanner to begin.";
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
    if (entry.lastSeen !== sweepAt[type]) cls.push("stale");
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
  add("First seen", `${ago(now - entry.firstSeen)} (tracked)`);
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

// Given samples [{t, rssi}] oldest -> newest for one device, return a short
// label describing whether the device is getting closer, moving away, or
// holding steady (shown next to the signal bar to help physically locate it).
// Return "" when there is not enough movement or data to say.
function signalTrend(samples) {
  // TODO(human)
  return "";
}

function signalNode(entry) {
  const rssi = entry.rssi;
  if (rssi == null) return null;
  const frag = document.createDocumentFragment();

  const samples = history.get(normAddr(addrOfAny(entry))) || [];
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
    const rising = samples[samples.length - 1].rssi >= samples[0].rssi;
    const badge = document.createElement("span");
    badge.className = "trend " + (rising ? "up" : "down");
    badge.textContent = (rising ? "▲ " : "▼ ") + trend;
    frag.append(badge);
  }
  return frag;
}

// The entry could come from any table; find whichever address field it carries.
function addrOfAny(entry) {
  return entry.bssid ?? entry.addr ?? entry.mac;
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
    aliases: store.alias,
    notes: store.note,
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
function ago(ms) {
  const s = Math.round(ms / 1000);
  return s < 2 ? "now" : `${s}s ago`;
}
function setStatus(text, live) {
  $("status").textContent = text;
  $("dot").classList.toggle("live", live);
  $("connect").textContent = live ? "Disconnect" : "Connect";
}

// ---------------------------------------------------------------- wiring ---

$("connect").addEventListener("click", () => (activePort ? disconnect() : connect()));
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

if ("serial" in navigator) {
  navigator.serial.addEventListener("connect", (event) => readFrom(event.target));
  navigator.serial.getPorts().then((ports) => {
    if (ports.length) readFrom(ports[0]);
  });
}

// Test/automation hook: drive investigation state without the DOM prompts.
window.recon = {
  setAlias, setNote, togglePin, toggleIgnore, snapshot,
  setFilter: (t) => { filterText = String(t).toLowerCase(); render(); },
  setFrozen: (f) => { frozen = f; if (!f) render(); },
  setShowHidden: (v) => { showHidden = v; render(); },
  feed: handleLine, state: { seen, store, history },
};

setInterval(() => { if (!frozen) render(); }, 1000);
render();
