"""Tests for recon_service. Run: python -m unittest discover -s host -v"""

import json
import tempfile
import threading
import unittest
from http.client import HTTPConnection
from pathlib import Path

import recon_service as rs

WIFI = {"t": "wifi", "cycle": 1, "up": 20, "items": [
    {"ssid": "Home", "bssid": "aa:bb:cc:00:00:01", "rssi": -50, "ch": 36, "auth": "WPA2"},
    {"ssid": "", "bssid": "AA:BB:CC:00:00:02", "rssi": -80, "ch": 6, "auth": "OPEN"},
]}
BLE = {"t": "ble", "cycle": 1, "up": 25, "items": [
    {"addr": "11:22:33:44:55:66", "type": "random", "rssi": -70, "name": "Tag"},
]}
MONITOR = {"t": "monitor", "cycle": 1, "up": 30,
           "clients": [{"mac": "de:ad:be:ef:00:01", "rssi": -60, "frames": 3, "probes": ["Cafe"]}],
           "aps": [{"bssid": "AA:BB:CC:00:00:01", "ssid": "Home", "clients": 2, "frames": 40}],
           "alerts": [{"bssid": "AA:BB:CC:00:00:09", "kind": "deauth-flood", "count": 12}],
           "channels": [{"ch": 36, "pkts": 29}, {"ch": 1, "pkts": 42}]}


class StoreTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.store = rs.Store(Path(self.tmp.name) / "t.db")

    def tearDown(self):
        self.tmp.cleanup()

    def test_first_seen_survives_later_sweeps(self):
        self.store.ingest(WIFI, at=1_000)
        self.store.ingest({**WIFI, "cycle": 2, "items": WIFI["items"][:1]}, at=5_000)
        wifi = self.store.state(at=6_000)["tables"]["wifi"]
        rows = {r["bssid"].upper(): r for r in wifi["rows"]}

        home = rows["AA:BB:CC:00:00:01"]
        self.assertEqual((home["firstSeen"], home["lastSeen"]), (1_000, 5_000))
        self.assertEqual(home["lastSweep"], wifi["latestSweep"])
        self.assertEqual([h["rssi"] for h in home["history"]], [-50, -50])
        # Seen in the earlier sweep only: still returned, but not in the latest sweep.
        self.assertLess(rows["AA:BB:CC:00:00:02"]["lastSweep"], wifi["latestSweep"])

    def test_everything_is_returned_however_old(self):
        self.store.ingest(WIFI, at=1_000)
        rows = self.store.state(at=1_000 + 365 * 24 * 3600 * 1000)["tables"]["wifi"]["rows"]
        self.assertEqual(len(rows), 2)

    def test_after_returns_only_devices_updated_since_cursor(self):
        self.store.ingest(WIFI, at=1_000)
        first = self.store.state()
        self.store.ingest({**WIFI, "items": WIFI["items"][:1]}, at=2_000)
        self.store.ingest(BLE, at=3_000)
        delta = self.store.state(after=first["cursor"])

        self.assertEqual([r["bssid"] for r in delta["tables"]["wifi"]["rows"]], ["aa:bb:cc:00:00:01"])
        self.assertEqual(len(delta["tables"]["ble"]["rows"]), 1)
        self.assertGreater(delta["cursor"], first["cursor"])
        self.assertEqual(self.store.state(after=delta["cursor"])["tables"]["wifi"]["rows"], [])

    def test_monitor_message_fills_clients_aps_channels_alerts(self):
        self.store.ingest(MONITOR, at=1_000)
        s = self.store.state(at=2_000)
        self.assertEqual(len(s["tables"]["clients"]["rows"]), 1)
        self.assertEqual(s["tables"]["aps"]["rows"][0]["clients"], 2)
        self.assertEqual([c["ch"] for c in s["channels"]], [1, 36])
        self.assertEqual(s["alerts"][0]["count"], 12)
        self.assertEqual(s["last"], {"cycle": 1, "up": 30, "at": 1_000})

    def test_history_is_capped_and_oldest_first(self):
        for i in range(rs.HISTORY_POINTS + 5):
            item = {**BLE["items"][0], "rssi": -90 + i}
            self.store.ingest({**BLE, "items": [item]}, at=1_000 + i)
        row = self.store.state(at=2_000)["tables"]["ble"]["rows"][0]
        self.assertEqual(len(row["history"]), rs.HISTORY_POINTS)
        self.assertEqual(row["history"][-1]["rssi"], -90 + rs.HISTORY_POINTS + 4)

    def test_malformed_messages_are_ignored(self):
        self.assertFalse(self.store.ingest({"t": "bogus"}))
        self.store.ingest({"t": "wifi", "items": [{"ssid": "no bssid"}, "junk", {"bssid": 5}]}, at=1)
        self.assertEqual(self.store.state(at=2)["tables"]["wifi"]["rows"], [])

    def test_annotations_update_and_clear(self):
        self.store.annotate("aa:bb:cc:00:00:01", {"alias": "Router", "pinned": True})
        a = self.store.state()["annotations"]["AA:BB:CC:00:00:01"]
        self.assertEqual((a["alias"], a["pinned"], a["hidden"]), ("Router", True, False))
        self.store.annotate("AA:BB:CC:00:00:01", {"alias": "", "pinned": False})
        self.assertEqual(self.store.state()["annotations"], {})

    def test_legacy_import_never_overwrites(self):
        self.store.annotate("AA:BB:CC:00:00:01", {"alias": "Kept"})
        changed = self.store.import_legacy({
            "alias": {"AA:BB:CC:00:00:01": "Replaced?", "11:22:33:44:55:66": "Tag"},
            "note": {"AA:BB:CC:00:00:01": "a note"},
            "pin": {"11:22:33:44:55:66": True},
            "ignore": {},
        })
        notes = self.store.state()["annotations"]
        self.assertEqual(changed, 2)
        self.assertEqual(notes["AA:BB:CC:00:00:01"]["alias"], "Kept")
        self.assertEqual(notes["AA:BB:CC:00:00:01"]["note"], "a note")
        self.assertTrue(notes["11:22:33:44:55:66"]["pinned"])

    def test_validate_annotation(self):
        self.assertEqual(rs.validate_annotation({"alias": " x ", "extra": 1}), {"alias": "x"})
        for bad in ({}, {"pinned": "yes"}, {"alias": 3}, {"note": "n" * (rs.MAX_NOTE + 1)}, []):
            with self.assertRaises(ValueError):
                rs.validate_annotation(bad)

    def test_handle_line_ingests_json_and_tracks_banner(self):
        status = rs.SerialStatus()
        rs.handle_line(self.store, status, b"=== ESP32-C5 WiFi/BLE recon (2.4 + 5 GHz) ===\r\n", at=1)
        rs.handle_line(self.store, status, json.dumps(BLE).encode() + b"\n", at=2)
        rs.handle_line(self.store, status, b'{"t":"wifi","items":[{"bss\n', at=3)  # Truncated.
        self.assertEqual(status.snapshot()["banner"], "ESP32-C5 WiFi/BLE recon (2.4 + 5 GHz)")
        self.assertEqual(status.snapshot()["lastLineAt"], 3)
        self.assertEqual(len(self.store.state(at=4)["tables"]["ble"]["rows"]), 1)


class HttpTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.store = rs.Store(Path(self.tmp.name) / "t.db")
        self.server = rs.ReconServer(("127.0.0.1", 0), self.store, rs.SerialStatus())
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.tmp.cleanup()

    def request(self, method, path, body=None, host=None, ctype="application/json"):
        conn = HTTPConnection("127.0.0.1", self.port, timeout=5)
        headers = {"Host": host or f"localhost:{self.port}"}
        data = None
        if body is not None:
            data = json.dumps(body).encode()
            headers["Content-Type"] = ctype
        conn.request(method, path, body=data, headers=headers)
        res = conn.getresponse()
        payload = res.read()
        conn.close()
        return res.status, payload

    def test_state_and_page(self):
        self.store.ingest(WIFI)
        code, body = self.request("GET", "/api/state?after=0")
        self.assertEqual(code, 200)
        state = json.loads(body)
        self.assertEqual(len(state["tables"]["wifi"]["rows"]), 2)
        self.assertIn("serial", state)
        code, _ = self.request("GET", "/index.html")
        self.assertEqual(code, 200)

    def test_foreign_host_is_rejected(self):
        code, _ = self.request("GET", "/api/state", host="evil.example:8000")
        self.assertEqual(code, 403)

    def test_writes_require_json_content_type(self):
        code, _ = self.request("PUT", "/api/annotations/AA", {"alias": "x"}, ctype="text/plain")
        self.assertEqual(code, 415)

    def test_put_annotation_round_trip(self):
        code, body = self.request("PUT", "/api/annotations/aa%3Abb", {"note": "hello"})
        self.assertEqual((code, json.loads(body)["note"]), (200, "hello"))
        code, body = self.request("PUT", "/api/annotations/aa%3Abb", {"pinned": "no"})
        self.assertEqual(code, 400)
        self.assertEqual(self.store.state()["annotations"]["AA:BB"]["note"], "hello")

    def test_import_endpoint(self):
        code, body = self.request("POST", "/api/annotations/import", {"alias": {"aa": "A"}})
        self.assertEqual((code, json.loads(body)), (200, {"imported": 1}))


if __name__ == "__main__":
    unittest.main()
