"""Host-side service for the ESP32 recon scanner.

Reads the board's JSON lines over USB serial, records them in SQLite, and serves
web/ plus a small JSON API on localhost. It only reads from the serial port and
never sends anything to the board.

    python host/recon_service.py [--port COM8] [--db data/recon.db] [--http-port 8000]

API (localhost only):
    GET  /api/state[?after=SWEEP_ID]  every recorded device, or only those
                                     updated after the given sweep; plus the
                                     latest monitor sweep, annotations, and
                                     serial status
    PUT  /api/annotations/<addr>     update alias/note/pinned/hidden for an address
    POST /api/annotations/import     merge the page's old localStorage data
"""

from __future__ import annotations

import argparse
import json
import logging
import socket
import sqlite3
import threading
import time
from contextlib import contextmanager
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlsplit

import serial
from serial.tools import list_ports

log = logging.getLogger("recon")

ROOT = Path(__file__).resolve().parent.parent
WEB_DIR = ROOT / "web"
DEFAULT_DB = ROOT / "data" / "recon.db"

# USB IDs of the supported boards.
BOARD_USB_IDS = {
    (0x2341, 0x0070),  # Arduino Nano ESP32 (native USB)
    (0x1A86, 0x55D3),  # ESP32-C5-DevKitC-1 UART port (CH343)
    (0x303A, 0x1001),  # ESP32-C5 native USB Serial/JTAG port
}
BAUD_RATE = 115200
MAX_LINE_BYTES = 256 * 1024  # A monitor line is ~10 KB; anything huge is garbage.
RETRY_S = 2

HISTORY_POINTS = 40        # RSSI samples returned per device for the sparkline.
MAX_BODY_BYTES = 1024 * 1024
MAX_ADDR = 64
MAX_ALIAS = 100
MAX_NOTE = 2000

# The page's tables -> (board message type, list field, address field).
TABLES = {
    "wifi": ("wifi", "items", "bssid"),
    "ble": ("ble", "items", "addr"),
    "clients": ("monitor", "clients", "mac"),
    "aps": ("monitor", "aps", "bssid"),
}
SWEEP_KINDS = ("wifi", "ble", "monitor")

SCHEMA_VERSION = 2
SCHEMA = """
PRAGMA journal_mode = WAL;

-- One row per message from the board. Timestamps are host time, ms since the
-- Unix epoch; the board only knows its uptime.
CREATE TABLE IF NOT EXISTS sweeps (
    id    INTEGER PRIMARY KEY,
    kind  TEXT    NOT NULL,          -- wifi | ble | monitor
    at    INTEGER NOT NULL,
    cycle INTEGER,                   -- board's sweep counter (resets on reboot)
    up    INTEGER                    -- board uptime, seconds
);
CREATE INDEX IF NOT EXISTS sweeps_kind ON sweeps(kind, id);

-- Latest known state of each device, one row per table and address.
CREATE TABLE IF NOT EXISTS devices (
    kind       TEXT    NOT NULL,     -- wifi | ble | clients | aps
    addr       TEXT    NOT NULL,     -- BSSID / MAC / BLE address, uppercased
    first_seen INTEGER NOT NULL,
    last_seen  INTEGER NOT NULL,
    last_sweep INTEGER NOT NULL REFERENCES sweeps(id),
    data       TEXT    NOT NULL,     -- latest record from the board, as JSON
    PRIMARY KEY (kind, addr)
);
-- The page polls for devices updated after the last sweep it has seen.
DROP INDEX IF EXISTS devices_recent;
CREATE INDEX IF NOT EXISTS devices_updated ON devices(kind, last_sweep);

-- Every time a device appeared in a sweep. Kept forever.
CREATE TABLE IF NOT EXISTS sightings (
    sweep INTEGER NOT NULL REFERENCES sweeps(id),
    kind  TEXT    NOT NULL,
    addr  TEXT    NOT NULL,
    at    INTEGER NOT NULL,
    rssi  INTEGER,                   -- NULL for AP-traffic rows, which carry none
    ch    INTEGER                    -- WiFi scan rows only
);
CREATE INDEX IF NOT EXISTS sightings_device ON sightings(kind, addr, at);

CREATE TABLE IF NOT EXISTS channel_activity (
    sweep INTEGER NOT NULL REFERENCES sweeps(id),
    ch    INTEGER NOT NULL,
    pkts  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS channel_activity_sweep ON channel_activity(sweep);

CREATE TABLE IF NOT EXISTS alerts (
    sweep INTEGER NOT NULL REFERENCES sweeps(id),
    bssid TEXT    NOT NULL,
    kind  TEXT    NOT NULL,
    count INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS alerts_sweep ON alerts(sweep);

-- User investigation state, keyed by address across all tables.
CREATE TABLE IF NOT EXISTS annotations (
    addr    TEXT    PRIMARY KEY,
    alias   TEXT    NOT NULL DEFAULT '',
    note    TEXT    NOT NULL DEFAULT '',
    pinned  INTEGER NOT NULL DEFAULT 0,
    hidden  INTEGER NOT NULL DEFAULT 0,
    updated INTEGER NOT NULL
);
"""

UPSERT_DEVICE = """
INSERT INTO devices (kind, addr, first_seen, last_seen, last_sweep, data)
VALUES (?, ?, ?, ?, ?, ?)
ON CONFLICT (kind, addr) DO UPDATE SET
    last_seen = excluded.last_seen,
    last_sweep = excluded.last_sweep,
    data = excluded.data
"""


def now_ms() -> int:
    return int(time.time() * 1000)


def norm_addr(addr: str) -> str:
    return addr.strip().upper()


def _int(value) -> int | None:
    """The board's integers, or None for anything else (bool is not an int here)."""
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def _list(value) -> list:
    return value if isinstance(value, list) else []


def _empty_annotation() -> dict:
    return {"alias": "", "note": "", "pinned": False, "hidden": False}


def validate_annotation(body) -> dict:
    """Returns the recognised, cleaned fields of a PUT body. Raises ValueError."""
    if not isinstance(body, dict):
        raise ValueError("expected a JSON object")
    changes = {}
    for key, limit in (("alias", MAX_ALIAS), ("note", MAX_NOTE)):
        if key in body:
            value = body[key]
            if not isinstance(value, str):
                raise ValueError(f"{key} must be a string")
            value = value.strip()
            if len(value) > limit:
                raise ValueError(f"{key} is longer than {limit} characters")
            changes[key] = value
    for key in ("pinned", "hidden"):
        if key in body:
            if not isinstance(body[key], bool):
                raise ValueError(f"{key} must be true or false")
            changes[key] = body[key]
    if not changes:
        raise ValueError("nothing to update")
    return changes


class Store:
    """All database access. Each call opens its own connection, so the serial
    thread and HTTP request threads never share one."""

    def __init__(self, path: Path | str):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.session() as db:
            db.executescript(SCHEMA)
            db.execute(f"PRAGMA user_version = {SCHEMA_VERSION}")

    @contextmanager
    def session(self):
        db = sqlite3.connect(self.path, timeout=5)
        db.row_factory = sqlite3.Row
        try:
            with db:  # Commit on success, roll back on error.
                yield db
        finally:
            db.close()

    # --- writes from the board ----------------------------------------------

    def ingest(self, msg: dict, at: int | None = None) -> bool:
        """Records one JSON message from the board. Returns False if ignored."""
        kind = msg.get("t")
        if kind not in SWEEP_KINDS:
            return False
        at = now_ms() if at is None else at

        with self.session() as db:
            sweep = db.execute(
                "INSERT INTO sweeps (kind, at, cycle, up) VALUES (?, ?, ?, ?)",
                (kind, at, _int(msg.get("cycle")), _int(msg.get("up"))),
            ).lastrowid

            for table, (source, field, key) in TABLES.items():
                if source != kind:
                    continue
                for item in _list(msg.get(field)):
                    if not isinstance(item, dict) or not isinstance(item.get(key), str):
                        continue
                    addr = norm_addr(item[key])
                    if not addr:
                        continue
                    db.execute(UPSERT_DEVICE, (table, addr, at, at, sweep, json.dumps(item)))
                    db.execute(
                        "INSERT INTO sightings (sweep, kind, addr, at, rssi, ch)"
                        " VALUES (?, ?, ?, ?, ?, ?)",
                        (sweep, table, addr, at, _int(item.get("rssi")), _int(item.get("ch"))),
                    )

            if kind == "monitor":
                for c in _list(msg.get("channels")):
                    if isinstance(c, dict) and _int(c.get("ch")) and _int(c.get("pkts")) is not None:
                        db.execute(
                            "INSERT INTO channel_activity (sweep, ch, pkts) VALUES (?, ?, ?)",
                            (sweep, c["ch"], c["pkts"]),
                        )
                for a in _list(msg.get("alerts")):
                    if (isinstance(a, dict) and isinstance(a.get("bssid"), str)
                            and isinstance(a.get("kind"), str) and _int(a.get("count")) is not None):
                        db.execute(
                            "INSERT INTO alerts (sweep, bssid, kind, count) VALUES (?, ?, ?, ?)",
                            (sweep, a["bssid"], a["kind"], a["count"]),
                        )
        return True

    # --- reads for the page -------------------------------------------------

    def state(self, after: int | None = None, at: int | None = None) -> dict:
        """Every recorded device, or with `after`, only devices updated in a
        sweep newer than that sweep id. `cursor` in the result is the value to
        pass as `after` next time. Sweep ids are used rather than timestamps
        because they are assigned inside the writing transaction, so a sweep
        committed while a poll is running is picked up by the next poll."""
        at = now_ms() if at is None else at

        with self.session() as db:
            db.execute("BEGIN")  # One snapshot for all the reads below.
            cursor = db.execute("SELECT COALESCE(MAX(id), 0) FROM sweeps").fetchone()[0]
            latest = {
                kind: db.execute(
                    "SELECT id, at, cycle, up FROM sweeps WHERE kind = ? ORDER BY id DESC LIMIT 1",
                    (kind,),
                ).fetchone()
                for kind in SWEEP_KINDS
            }

            tables = {}
            for table, (source, _, _) in TABLES.items():
                sweep = latest[source]
                rows = []
                for r in db.execute(
                    "SELECT addr, first_seen, last_seen, last_sweep, data FROM devices"
                    " WHERE kind = ? AND last_sweep > ?",
                    (table, after or 0),
                ):
                    entry = json.loads(r["data"])
                    entry["firstSeen"] = r["first_seen"]
                    entry["lastSeen"] = r["last_seen"]
                    entry["lastSweep"] = r["last_sweep"]
                    entry["history"] = self._history(db, table, r["addr"])
                    rows.append(entry)
                tables[table] = {"latestSweep": sweep["id"] if sweep else 0, "rows": rows}

            channels, alerts = [], []
            if monitor := latest["monitor"]:
                channels = [
                    {"ch": r["ch"], "pkts": r["pkts"]}
                    for r in db.execute(
                        "SELECT ch, pkts FROM channel_activity WHERE sweep = ? ORDER BY ch",
                        (monitor["id"],),
                    )
                ]
                alerts = [
                    {"bssid": r["bssid"], "kind": r["kind"], "count": r["count"]}
                    for r in db.execute(
                        "SELECT bssid, kind, count FROM alerts WHERE sweep = ?", (monitor["id"],)
                    )
                ]

            newest = max((s for s in latest.values() if s), key=lambda s: s["id"], default=None)
            annotations = {
                r["addr"]: {
                    "alias": r["alias"], "note": r["note"],
                    "pinned": bool(r["pinned"]), "hidden": bool(r["hidden"]),
                }
                for r in db.execute("SELECT * FROM annotations")
            }

        return {
            "now": at,
            "cursor": cursor,
            "last": {"cycle": newest["cycle"], "up": newest["up"], "at": newest["at"]} if newest else None,
            "tables": tables,
            "channels": channels,
            "alerts": alerts,
            "annotations": annotations,
        }

    @staticmethod
    def _history(db, table: str, addr: str) -> list[dict]:
        rows = db.execute(
            "SELECT at, rssi FROM sightings WHERE kind = ? AND addr = ? AND rssi IS NOT NULL"
            " ORDER BY at DESC LIMIT ?",
            (table, addr, HISTORY_POINTS),
        ).fetchall()
        return [{"t": r["at"], "rssi": r["rssi"]} for r in reversed(rows)]

    # --- annotations ----------------------------------------------------------

    def annotate(self, addr: str, changes: dict) -> dict:
        """Applies validated changes to one address and returns its annotation."""
        addr = norm_addr(addr)
        with self.session() as db:
            current = self._annotation(db, addr)
            current.update(changes)
            self._write_annotation(db, addr, current)
        return current

    def import_legacy(self, body) -> int:
        """Merges the page's old localStorage maps ({alias, note, pin, ignore},
        each keyed by address). Fills only fields that are still empty, so it
        never overwrites anything already in the database. Returns the number
        of addresses changed."""
        if not isinstance(body, dict):
            raise ValueError("expected a JSON object")
        incoming: dict[str, dict] = {}
        for src, dst in (("alias", "alias"), ("note", "note"), ("pin", "pinned"), ("ignore", "hidden")):
            mapping = body.get(src, {})
            if not isinstance(mapping, dict):
                raise ValueError(f"{src} must be an object")
            for addr, value in mapping.items():
                if not isinstance(addr, str) or not norm_addr(addr) or len(addr) > MAX_ADDR:
                    continue
                if dst in ("alias", "note"):
                    if not isinstance(value, str) or not value.strip():
                        continue
                    value = value.strip()[: MAX_ALIAS if dst == "alias" else MAX_NOTE]
                else:
                    value = bool(value)
                    if not value:
                        continue
                incoming.setdefault(norm_addr(addr), {})[dst] = value

        changed = 0
        with self.session() as db:
            for addr, fields in incoming.items():
                current = self._annotation(db, addr)
                merged = dict(current)
                for key, value in fields.items():
                    if not current[key]:
                        merged[key] = value
                if merged != current:
                    self._write_annotation(db, addr, merged)
                    changed += 1
        return changed

    @staticmethod
    def _annotation(db, addr: str) -> dict:
        r = db.execute(
            "SELECT alias, note, pinned, hidden FROM annotations WHERE addr = ?", (addr,)
        ).fetchone()
        if r is None:
            return _empty_annotation()
        return {"alias": r["alias"], "note": r["note"],
                "pinned": bool(r["pinned"]), "hidden": bool(r["hidden"])}

    @staticmethod
    def _write_annotation(db, addr: str, a: dict) -> None:
        if a == _empty_annotation():
            db.execute("DELETE FROM annotations WHERE addr = ?", (addr,))
            return
        db.execute(
            "INSERT OR REPLACE INTO annotations (addr, alias, note, pinned, hidden, updated)"
            " VALUES (?, ?, ?, ?, ?, ?)",
            (addr, a["alias"], a["note"], int(a["pinned"]), int(a["hidden"]), now_ms()),
        )


# --- serial -------------------------------------------------------------------


class SerialStatus:
    """What the page shows about the board connection. Shared across threads."""

    def __init__(self):
        self._lock = threading.Lock()
        self._s = {"connected": False, "port": None, "since": 0, "lastLineAt": 0,
                   "banner": "", "error": ""}

    def update(self, **fields) -> None:
        with self._lock:
            self._s.update(fields)

    def snapshot(self) -> dict:
        with self._lock:
            return dict(self._s)


def find_board_port() -> str | None:
    for p in list_ports.comports():
        if (p.vid, p.pid) in BOARD_USB_IDS:
            return p.device
    return None


def handle_line(store: Store, status: SerialStatus, raw: bytes, at: int | None = None) -> None:
    at = now_ms() if at is None else at
    text = raw.decode("utf-8", "replace").strip()
    status.update(lastLineAt=at)
    if text.startswith("{"):
        try:
            msg = json.loads(text)
        except ValueError:
            return  # Line garbled by a reset mid-transmission.
        if isinstance(msg, dict):
            store.ingest(msg, at)
    elif text.startswith("==="):
        status.update(banner=text.strip("= "))


def serial_loop(store: Store, status: SerialStatus, port_arg: str | None,
                stop: threading.Event) -> None:
    """Finds the board, reads lines until it disappears, and repeats."""
    while not stop.is_set():
        port = port_arg or find_board_port()
        if port is None:
            status.update(connected=False, port=None, error="No scanner board found on USB")
            stop.wait(RETRY_S)
            continue
        try:
            # Opening asserts DTR/RTS (pyserial's default). The Nano's USB CDC
            # only sends once DTR is set; on the C5 DevKit the pair leaves the
            # chip running.
            with serial.Serial(port, BAUD_RATE, timeout=1) as ser:
                log.info("Reading from %s", port)
                status.update(connected=True, port=port, since=now_ms(), error="")
                pending = b""
                while not stop.is_set():
                    chunk = ser.readline()  # May return a partial line on timeout.
                    if not chunk:
                        continue
                    pending += chunk
                    if not pending.endswith(b"\n"):
                        if len(pending) > MAX_LINE_BYTES:
                            pending = b""
                        continue
                    line, pending = pending, b""
                    try:
                        handle_line(store, status, line)
                    except sqlite3.Error:
                        log.exception("Could not record a line")
        except (serial.SerialException, OSError) as err:
            log.warning("Serial %s: %s", port, err)
            status.update(connected=False, error=str(err))
            stop.wait(RETRY_S)


# --- HTTP ---------------------------------------------------------------------


class ReconServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, store: Store, status: SerialStatus):
        # "localhost" resolves to ::1 before 127.0.0.1 on Windows, and a client
        # that finds nothing on ::1 waits ~2 s before falling back, so main()
        # runs one server per loopback address.
        if ":" in address[0]:
            self.address_family = socket.AF_INET6
        super().__init__(address, Handler)
        self.store = store
        self.status = status
        port = self.server_address[1]
        # Rejecting other Host headers stops DNS-rebinding pages from reading
        # the API through the browser.
        self.allowed_hosts = {f"localhost:{port}", f"127.0.0.1:{port}", f"[::1]:{port}"}


class Handler(SimpleHTTPRequestHandler):
    server: ReconServer

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB_DIR), **kwargs)

    def log_message(self, format, *args):  # noqa: A002 (signature is inherited)
        log.debug("%s %s", self.address_string(), format % args)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    # --- routing ---

    def do_GET(self):
        if not self._host_ok():
            return
        url = urlsplit(self.path)
        if url.path == "/api/state":
            after = parse_qs(url.query).get("after", [None])[0]
            if after is not None and not after.isdigit():
                return self._json(HTTPStatus.BAD_REQUEST, {"error": "after must be a sweep id"})
            body = self.server.store.state(int(after) if after is not None else None)
            body["serial"] = self.server.status.snapshot()
            return self._json(HTTPStatus.OK, body)
        if url.path.startswith("/api/"):
            return self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})
        super().do_GET()

    def do_HEAD(self):
        if self._host_ok():
            super().do_HEAD()

    def do_PUT(self):
        if not self._host_ok():
            return
        path = urlsplit(self.path).path
        prefix = "/api/annotations/"
        if not path.startswith(prefix):
            return self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})
        addr = unquote(path[len(prefix):])
        if not addr.strip() or len(addr) > MAX_ADDR:
            return self._json(HTTPStatus.BAD_REQUEST, {"error": "bad address"})
        body = self._read_json()
        if body is None:
            return
        try:
            changes = validate_annotation(body)
        except ValueError as err:
            return self._json(HTTPStatus.BAD_REQUEST, {"error": str(err)})
        self._json(HTTPStatus.OK, self.server.store.annotate(addr, changes))

    def do_POST(self):
        if not self._host_ok():
            return
        if urlsplit(self.path).path != "/api/annotations/import":
            return self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})
        body = self._read_json()
        if body is None:
            return
        try:
            changed = self.server.store.import_legacy(body)
        except ValueError as err:
            return self._json(HTTPStatus.BAD_REQUEST, {"error": str(err)})
        self._json(HTTPStatus.OK, {"imported": changed})

    # --- helpers ---

    def _host_ok(self) -> bool:
        if (self.headers.get("Host") or "").lower() in self.server.allowed_hosts:
            return True
        self._json(HTTPStatus.FORBIDDEN, {"error": "open this page via http://localhost"})
        return False

    def _read_json(self):
        """Returns the parsed body, or None after sending an error response."""
        # Requiring a JSON content type forces a CORS preflight, which this
        # server never approves, so other sites can't write here.
        if not (self.headers.get("Content-Type") or "").startswith("application/json"):
            self._json(HTTPStatus.UNSUPPORTED_MEDIA_TYPE, {"error": "expected application/json"})
            return None
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            length = -1
        if length < 0 or length > MAX_BODY_BYTES:
            self._json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {"error": "body too large"})
            return None
        try:
            return json.loads(self.rfile.read(length) or b"null")
        except ValueError:
            self._json(HTTPStatus.BAD_REQUEST, {"error": "invalid JSON"})
            return None

    def _json(self, code: HTTPStatus, body) -> None:
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--port", help="serial port (default: find the board by USB ID)")
    parser.add_argument("--db", type=Path, default=DEFAULT_DB, help=f"SQLite file (default: {DEFAULT_DB})")
    parser.add_argument("--http-port", type=int, default=8000, help="web page port (default: 8000)")
    parser.add_argument("-v", "--verbose", action="store_true", help="log every HTTP request")
    args = parser.parse_args(argv)

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(message)s", datefmt="%H:%M:%S")

    store = Store(args.db)
    status = SerialStatus()
    stop = threading.Event()
    reader = threading.Thread(target=serial_loop, args=(store, status, args.port, stop),
                              name="serial", daemon=True)
    reader.start()

    servers = [ReconServer(("127.0.0.1", args.http_port), store, status)]
    try:
        servers.append(ReconServer(("::1", args.http_port), store, status))
    except OSError as err:  # IPv6 disabled: IPv4 alone still works, just slower via "localhost".
        log.warning("Not listening on [::1]: %s", err)
    for extra in servers[1:]:
        threading.Thread(target=extra.serve_forever, name="http-v6", daemon=True).start()

    log.info("Open http://localhost:%d  (database: %s)", args.http_port, args.db)
    try:
        servers[0].serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        for server in servers[1:]:
            server.shutdown()
        for server in servers:
            server.server_close()
        reader.join(timeout=3)


if __name__ == "__main__":
    main()
