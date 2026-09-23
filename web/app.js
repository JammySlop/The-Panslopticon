"use strict";

// Reads JSON lines from the scanner over Web Serial and renders them.
//
// Every string shown here (SSIDs, BLE names) is chosen by whoever is
// broadcasting nearby, so all text goes into the DOM via textContent. Never
// switch this to innerHTML.

const NANO_ESP32 = { usbVendorId: 0x2341, usbProductId: 0x0070 };
const BAUD_RATE = 115200;
// Sweeps miss weak transmitters now and then. Keep an entry on screen (dimmed)
// until it has been absent this long.
const FORGET_AFTER_MS = 60_000;
// 16-bit Bluetooth SIG UUIDs are sent in their full 128-bit form.
const SIG_BASE_UUID = /^0000([0-9a-f]{4})-0000-1000-8000-00805f9b34fb$/i;

const seen = { wifi: new Map(), ble: new Map() };
const lastSweepAt = { wifi: 0, ble: 0 };
let lastMessage = null;
let activePort = null;
let activeReader = null;
let connectedAt = 0;
let bytesSinceConnect = 0;
// A full WiFi + BLE cycle takes ~8 s. Silence well past that means trouble.
const SILENCE_WARNING_MS = 20_000;

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
    // NotFoundError means the user closed the port picker without choosing.
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

  // The ESP32's USB serial stack discards output until the host asserts DTR,
  // and Web Serial does not promise to assert it on open.
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

function handleLine(line) {
  if (!line.startsWith("{")) return;  // Boot banner or plain-text errors.

  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    return;  // The first line after connecting is often cut off mid-way.
  }
  if ((msg.t !== "wifi" && msg.t !== "ble") || !Array.isArray(msg.items)) return;

  const now = Date.now();
  const entries = seen[msg.t];
  for (const item of msg.items) {
    const key = msg.t === "wifi" ? item.bssid : item.addr;
    const previous = entries.get(key);
    entries.set(key, { ...item, firstSeen: previous?.firstSeen ?? now, lastSeen: now });
  }
  lastSweepAt[msg.t] = now;
  lastMessage = { cycle: msg.cycle, up: msg.up, at: now };

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

// Entries from the latest sweep first, each group strongest first.
function ordered(type) {
  const sweep = lastSweepAt[type];
  return [...seen[type].values()].sort((a, b) => {
    const aFresh = a.lastSeen === sweep;
    const bFresh = b.lastSeen === sweep;
    if (aFresh !== bFresh) return aFresh ? -1 : 1;
    return b.rssi - a.rssi;
  });
}

// ---------------------------------------------------------------- render ---

function render() {
  const now = Date.now();
  forgetOld(now);
  renderTable("wifi", now, wifiRow, 7);
  renderTable("ble", now, bleRow, 8);

  if (activePort && bytesSinceConnect === 0 && now - connectedAt > SILENCE_WARNING_MS) {
    setStatus("Connected, but the board has sent nothing. Try unplugging it and reconnecting.", true);
  }

  $("meta").textContent = lastMessage
    ? `sweep #${lastMessage.cycle} · board up ${lastMessage.up}s · updated ${ago(now - lastMessage.at)}`
    : "";
}

function renderTable(type, now, makeRow, columns) {
  const rows = ordered(type);
  const fresh = rows.filter((r) => r.lastSeen === lastSweepAt[type]).length;
  $(`${type}-count`).textContent = rows.length
    ? `${fresh} in last sweep, ${rows.length} total`
    : "";

  const body = $(`${type}-body`);
  if (!rows.length) {
    const tr = document.createElement("tr");
    const td = cell(tr, activePort ? "Waiting for data…" : "Connect the scanner to begin.", "empty");
    td.colSpan = columns;
    body.replaceChildren(tr);
    return;
  }
  body.replaceChildren(...rows.map((entry) => {
    const tr = makeRow(entry, now);
    if (entry.lastSeen !== lastSweepAt[type]) tr.className = "stale";
    return tr;
  }));
}

function wifiRow(n, now) {
  const tr = document.createElement("tr");
  optionalCell(tr, n.ssid, "hidden");
  cell(tr, n.bssid, "mono");
  signalCell(tr, n.rssi);
  cell(tr, n.ch);
  cell(tr, n.auth);
  cell(tr, n.flag, "flag");
  cell(tr, ago(now - n.lastSeen), "age");
  return tr;
}

function bleRow(d, now) {
  const tr = document.createElement("tr");
  cell(tr, d.addr, "mono");
  cell(tr, d.type);
  signalCell(tr, d.rssi);
  optionalCell(tr, d.name, "—");
  optionalCell(tr, d.mfg, "—");
  optionalCell(tr, d.tx == null ? "" : `${d.tx} dBm`, "—");
  optionalCell(tr, (d.svc ?? []).map(shortUuid).join("\n"), "—").classList.add("wrap", "mono");
  cell(tr, ago(now - d.lastSeen), "age");
  return tr;
}

function cell(tr, text, className) {
  const td = document.createElement("td");
  td.textContent = text ?? "";
  if (className) td.className = className;
  tr.appendChild(td);
  return td;
}

function optionalCell(tr, text, placeholder) {
  if (text) return cell(tr, text);
  const td = cell(tr, "");
  const span = document.createElement("span");
  span.className = "none";
  span.textContent = placeholder;
  td.appendChild(span);
  return td;
}

function signalCell(tr, rssi) {
  const td = document.createElement("td");
  const bar = document.createElement("span");
  const fill = document.createElement("i");
  // Map roughly -100 dBm (barely there) .. -30 dBm (right next to it) to 0..100%.
  const percent = Math.max(0, Math.min(100, ((rssi + 100) / 70) * 100));
  fill.style.width = `${percent}%`;
  bar.className = "bar";
  bar.appendChild(fill);
  td.append(bar, `${rssi} dBm`);
  tr.appendChild(td);
}

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
  // Reconnect automatically when a previously approved board reappears, such as
  // after flashing new firmware.
  navigator.serial.addEventListener("connect", (event) => readFrom(event.target));
  navigator.serial.getPorts().then((ports) => {
    if (ports.length) readFrom(ports[0]);
  });
}

setInterval(render, 1000);  // Keep the "last seen" column ticking.
render();
