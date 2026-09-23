"use strict";

// Reads JSON lines from the scanner over Web Serial and renders them.
//
// Every string shown here (SSIDs, BLE names, probed network names) is chosen by
// whoever is broadcasting nearby, so all text reaches the DOM via textContent.
// Never switch this to innerHTML.

const NANO_ESP32 = { usbVendorId: 0x2341, usbProductId: 0x0070 };
const BAUD_RATE = 115200;
const FORGET_AFTER_MS = 60_000;      // Keep a missed entry on screen, dimmed, this long.
const SILENCE_WARNING_MS = 20_000;   // A full cycle is ~15 s; warn past this.
const SIG_BASE_UUID = /^0000([0-9a-f]{4})-0000-1000-8000-00805f9b34fb$/i;

// Live tables merge repeated sightings, keyed by a stable id, tracking when each
// was first and last heard.
const seen = { wifi: new Map(), ble: new Map(), clients: new Map(), aps: new Map() };
const sweepAt = { wifi: 0, ble: 0, clients: 0, aps: 0 };
const expanded = { wifi: new Set(), ble: new Set(), clients: new Set(), aps: new Set() };
let channels = [];
let alerts = [];
let lastMessage = null;

let activePort = null;
let activeReader = null;
let connectedAt = 0;
let bytesSinceConnect = 0;

const $ = (id) => document.getElementById(id);

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

  // The ESP32 USB serial stack discards output until the host asserts DTR, and
  // Web Serial does not promise to assert it on open.
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
    // The board was unplugged or reset (for example while flashing).
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

function mergeInto(type, items, idKey) {
  const now = Date.now();
  const entries = seen[type];
  for (const item of items) {
    const key = item[idKey];
    if (key == null) continue;
    const previous = entries.get(key);
    entries.set(key, { ...item, firstSeen: previous?.firstSeen ?? now, lastSeen: now });
  }
  sweepAt[type] = now;
}

function handleLine(line) {
  if (!line.startsWith("{")) return;  // Boot banner or plain-text errors.
  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    return;  // The first line after connecting is often cut off mid-way.
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
  render();
}

function forgetOld(now) {
  for (const entries of Object.values(seen)) {
    for (const [key, entry] of entries) {
      if (now - entry.lastSeen > FORGET_AFTER_MS) entries.delete(key);
    }
  }
}

// Latest-sweep entries first, each group strongest first.
function ordered(type) {
  const fresh = sweepAt[type];
  return [...seen[type].values()].sort((a, b) => {
    const af = a.lastSeen === fresh, bf = b.lastSeen === fresh;
    if (af !== bf) return af ? -1 : 1;
    return (b.rssi ?? -999) - (a.rssi ?? -999);
  });
}

// --------------------------------------------------------------- columns ---
// Each column is data: a label, the entry field it reads (so it can also be
// hidden from the auto-details row), and how to render it. Adding a firmware
// field means adding one row here — or nothing, and it shows up in details.

const text = (v) => (v == null || v === "" ? null : String(v));
const tags = (list) => (Array.isArray(list) && list.length ? list : null);

const WIFI_COLUMNS = [
  { label: "SSID", key: "ssid", get: (e) => e.ssid, placeholder: "hidden" },
  { label: "BSSID", key: "bssid", cls: "mono", get: (e) => e.bssid },
  { label: "Signal", key: "rssi", node: (e) => signalNode(e.rssi) },
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
  { label: "Address", key: "addr", cls: "mono", get: (e) => e.addr },
  { label: "Type", key: "type", get: (e) => e.type },
  { label: "Signal", key: "rssi", node: (e) => signalNode(e.rssi) },
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
  { label: "Device MAC", key: "mac", cls: "mono", get: (e) => e.mac },
  { label: "Vendor", key: "vendor", get: (e) => (e.rand ? "randomized" : text(e.vendor)), placeholder: "—" },
  { label: "Signal", key: "rssi", node: (e) => signalNode(e.rssi) },
  { label: "Frames", key: "frames", get: (e) => e.frames },
  { label: "Looking for", key: "probes", cls: "wrap", node: (e) => tagsNode(e.probes) },
  { label: "Last seen", age: true },
];

const AP_COLUMNS = [
  { label: "BSSID", key: "bssid", cls: "mono", get: (e) => e.bssid },
  { label: "SSID", key: "ssid", get: (e) => text(e.ssid), placeholder: "hidden" },
  { label: "Vendor", key: "vendor", get: (e) => text(e.vendor), placeholder: "—" },
  { label: "Clients", key: "clients", get: (e) => e.clients },
  { label: "Frames", key: "frames", get: (e) => e.frames },
  { label: "Last seen", age: true },
];

// Internal bookkeeping fields never shown as data.
const INTERNAL = new Set(["firstSeen", "lastSeen"]);

// ---------------------------------------------------------------- render ---

function render() {
  const now = Date.now();
  forgetOld(now);

  renderTable("wifi", WIFI_COLUMNS, now);
  renderTable("ble", BLE_COLUMNS, now);
  renderTable("clients", CLIENT_COLUMNS, now);
  renderTable("aps", AP_COLUMNS, now);
  renderChannels();
  renderAlerts();

  if (activePort && bytesSinceConnect === 0 && now - connectedAt > SILENCE_WARNING_MS) {
    setStatus("Connected, but the board has sent nothing. Try unplugging it and reconnecting.", true);
  }
  $("meta").textContent = lastMessage
    ? `sweep #${lastMessage.cycle} · board up ${lastMessage.up}s · updated ${ago(now - lastMessage.at)}`
    : "";
}

function renderTable(type, columns, now) {
  $(`${type}-head`).replaceChildren(...columns.map((c) => {
    const th = document.createElement("th");
    th.textContent = c.label;
    return th;
  }));

  const rows = ordered(type);
  const fresh = rows.filter((r) => r.lastSeen === sweepAt[type]).length;
  const countEl = $(`${type}-count`);
  if (countEl) countEl.textContent = rows.length ? `${fresh} in last sweep, ${rows.length} total` : "";

  const columnKeys = new Set(columns.map((c) => c.key).filter(Boolean));
  const body = $(`${type}-body`);

  if (!rows.length) {
    const tr = document.createElement("tr");
    const td = document.createElement("td");
    td.className = "empty";
    td.colSpan = columns.length;
    td.textContent = activePort ? "Waiting for data…" : "Connect the scanner to begin.";
    tr.appendChild(td);
    body.replaceChildren(tr);
    return;
  }

  const out = [];
  for (const entry of rows) {
    const idKey = columns[0].key;
    const id = entry[idKey];
    const extras = Object.keys(entry).filter((k) => !columnKeys.has(k) && !INTERNAL.has(k));
    const tr = buildRow(type, columns, entry, now, id, extras);
    if (entry.lastSeen !== sweepAt[type]) tr.className = "stale";
    out.push(tr);
    if (extras.length && expanded[type].has(id)) out.push(detailRow(entry, extras, columns.length));
  }
  body.replaceChildren(...out);
}

function buildRow(type, columns, entry, now, id, extras) {
  const tr = document.createElement("tr");
  columns.forEach((col, i) => {
    const td = document.createElement("td");
    if (col.cls) td.className = col.cls;

    if (i === 0 && extras.length) {
      const toggle = document.createElement("span");
      toggle.className = "toggle";
      toggle.textContent = expanded[type].has(id) ? "▾" : "▸";
      toggle.title = `${extras.length} more field(s)`;
      toggle.addEventListener("click", () => {
        expanded[type].has(id) ? expanded[type].delete(id) : expanded[type].add(id);
        render();
      });
      td.appendChild(toggle);
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
    tr.appendChild(td);
  });
  return tr;
}

function detailRow(entry, extras, span) {
  const tr = document.createElement("tr");
  tr.className = "detail";
  const td = document.createElement("td");
  td.colSpan = span;
  const dl = document.createElement("dl");
  for (const key of extras) {
    const dt = document.createElement("dt");
    dt.textContent = key;
    const dd = document.createElement("dd");
    const v = entry[key];
    dd.textContent = Array.isArray(v) ? (v.length ? v.join(", ") : "—") : String(v);
    dl.append(dt, dd);
  }
  td.appendChild(dl);
  tr.appendChild(td);
  return tr;
}

function placeholder(td, label) {
  const span = document.createElement("span");
  span.className = "none";
  span.textContent = label ?? "";
  td.appendChild(span);
}

function signalNode(rssi) {
  if (rssi == null) return null;
  const frag = document.createDocumentFragment();
  const bar = document.createElement("span");
  const fill = document.createElement("i");
  const percent = Math.max(0, Math.min(100, ((rssi + 100) / 70) * 100));
  fill.style.width = `${percent}%`;
  bar.className = "bar";
  bar.appendChild(fill);
  frag.append(bar, document.createTextNode(`${rssi} dBm`));
  return frag;
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
    host.replaceChildren(Object.assign(document.createElement("span"), {
      className: "none", textContent: "No channel activity captured yet.",
    }));
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
    mac.textContent = a.bssid;
    line.append(mac);
    out.push(line);
  }
  host.replaceChildren(...out);
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

if ("serial" in navigator) {
  navigator.serial.addEventListener("connect", (event) => readFrom(event.target));
  navigator.serial.getPorts().then((ports) => {
    if (ports.length) readFrom(ports[0]);
  });
}

setInterval(render, 1000);  // Keep the "last seen" column ticking.
render();
