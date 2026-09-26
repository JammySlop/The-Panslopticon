# esp32-fileshare

A small-file share for ESP32 boards, built for the ESP32-C5. The board joins a
WiFi network and serves a folder over HTTP. People on the same network open a
web page to upload, download and delete files. The code is a self-contained
library (`lib/FileShare`) so you can drop it into other firmware, for example a
Meshtastic node.

Status: compiles for `esp32c5` and `esp32dev`. It has **not been tested on
hardware yet**.

## Using the example firmware

```powershell
cd esp32-fileshare
pio run -e esp32c5 -t upload
pio device monitor
```

1. On first boot there's no saved network, so the board starts a setup hotspot
   called `FileShare-XXXX`. Its password is printed on the serial console. It is
   random, generated once and kept in NVS.
2. Join the hotspot and open `http://192.168.4.1/wifi`. Enter your network's
   SSID and password.
3. The board joins that network. Open `http://fileshare.local/` (or the IP
   printed on the serial console).

If the saved network can't be reached within 20 s, the setup hotspot comes back.
While the hotspot has no one connected, the board retries the saved network
every 5 minutes. Files can also be shared while the board is in hotspot mode.

To skip the setup hotspot, pass the network details in as build flags through
the environment. Keep them out of `platformio.ini`:

```powershell
$env:FILESHARE_BUILD_FLAGS = '-DFILESHARE_SSID=\"MyWifi\" -DFILESHARE_PASS=\"secret\"'
```

`-DFILESHARE_USER` / `-DFILESHARE_PASSWORD` turn on HTTP basic auth.

## HTTP API

| Method | Path | |
|---|---|---|
| GET | `/` | browser UI |
| GET | `/api/files` | JSON: files, bytes used, limits |
| GET | `/api/file?name=X` | download |
| POST | `/api/file` | multipart upload, field `file` |
| DELETE | `/api/file?name=X` | delete |
| GET/POST | `/wifi` | network setup (setup hotspot only) |

```sh
curl -F file=@notes.txt http://fileshare.local/api/file
curl -O -J "http://fileshare.local/api/file?name=notes.txt"
```

## Library

Two classes that work independently:

- **`FileShareServer`** serves one flat directory on any mounted `fs::FS`
  (LittleFS, SD, SD_MMC, FFat). It has no WiFi logic.
- **`FileShareWifi`** (optional) joins a network, runs the setup hotspot, and
  starts mDNS. Leave it out if the host firmware already handles WiFi.

```cpp
FileShareServer share;

FileShareConfig cfg;
cfg.rootDir = "/share";
cfg.port = 8080;
cfg.maxFileBytes = 256 * 1024;   // per file
cfg.maxTotalBytes = 1024 * 1024; // whole share
share.begin(LittleFS, cfg);
share.onEvent([](FileShareServer::Event e, const String& name, size_t size) { /* announce */ });

// in a loop or periodic task:
share.loop();
```

Behaviour worth knowing:

- Uploaded filenames are sanitised to `[A-Za-z0-9._-]` and capped at
  `maxNameLen` (48). Keep `rootDir` + name within the filesystem's path limit.
  LittleFS allows 64 by default. SPIFFS allows only 32, so it needs shorter
  settings.
- An upload goes to a temporary file first and is renamed when it completes, so
  an interrupted upload never leaves a half-written file behind. If a file with
  the same name already exists, the upload gets `409` instead of overwriting it.
- The size limits are enforced as the upload streams in. Clients get `413` or
  `507` if a limit is hit.
- Basic auth over plain HTTP keeps casual visitors out but isn't encrypted.
- `loop()` handles one request at a time. A long download blocks the caller
  until it finishes.

To use it in another PlatformIO project, copy `lib/FileShare` into that
project's `lib/`, or point `lib_extra_dirs` at it. It depends only on libraries
bundled with arduino-esp32: WebServer, WiFi, ESPmDNS, Preferences and FS.

## Meshtastic

The intended use is a Meshtastic node that also runs a file share, so people who
can reach the node over WiFi can swap small files. The files travel over WiFi,
not over LoRa. The mesh can only carry the announcement, for example "new file
map.png at http://…".

Rough wiring. This has not been built against the Meshtastic source yet:

- Meshtastic manages WiFi itself (`network.wifi_ssid`) and already runs its own
  web server on port 80. Use `FileShareServer` alone on another port such as
  8080, and **don't** use `FileShareWifi`.
- Point it at Meshtastic's already-mounted filesystem (`FSCom`) with its own
  directory, such as `/share`, so it stays apart from Meshtastic's config files.
- Call `share.begin()` once the network is up, and call `share.loop()` from a
  periodic `OSThread` (every ~5–10 ms).
- Use `onEvent` to send a text message when a file is uploaded.
- Check that Meshtastic supports your board. As of writing, ESP32-C5 support in
  Meshtastic firmware isn't confirmed. It also needs arduino-esp32 3.x for the
  C5, and Meshtastic's ESP32 builds have used 2.x. The library avoids 3.x-only
  APIs, but it has only been compiled against 3.3.
