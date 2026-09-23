# nano-esp32

Firmware base for the **Arduino Nano ESP32** (ESP32-S3 / u-blox NORA-W106),
built with PlatformIO.

| | |
|---|---|
| Board ID | `arduino_nano_esp32` |
| MCU | ESP32-S3 @ 240 MHz, dual core |
| Flash / RAM / PSRAM | 16 MB / 320 KB / 8 MB |
| Upload | DFU over native USB |
| Monitor | 115200 baud, USB CDC |

## Layout

```
platformio.ini          build environments
include/config.h        pins, baud rates, task intervals
src/main.cpp            setup() / loop(), task table, board bring-up
lib/app_core/           hardware-free logic (compiles on host + target)
test/test_scheduler/    Unity tests for lib/app_core
.github/workflows/ci.yml
```

The split matters: anything in `lib/app_core/` must not `#include <Arduino.h>`.
That is what lets the same code be unit-tested on a laptop and compiled into
firmware, and it is why `AppScheduler` takes `nowMs` as an argument instead of
calling `millis()` itself.

## Pipeline

PlatformIO is installed in an isolated venv and is not on `PATH` by default.
Either add `C:\Users\jerre\.platformio\penv\Scripts` to `PATH`, or prefix:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

& $pio run                       # compile debug firmware
& $pio run -e nano_esp32_release # compile optimised firmware
& $pio run --target upload       # flash over DFU
& $pio device monitor            # serial console (Ctrl+C to exit)
& $pio run -t upload -t monitor  # flash then immediately attach
& $pio test -e native            # host unit tests
& $pio run -t size               # flash/RAM usage report
& $pio check                     # static analysis
```

### Flashing notes

The Nano ESP32 has no USB-serial bridge; the ESP32-S3 drives USB directly.
Consequences:

- The COM port disappears and re-enumerates on every reset. A monitor session
  will drop when you flash — reattach after.
- If an upload fails with "no DFU device found", force the bootloader:
  **double-tap the RESET button**. The green LED breathes when it is waiting.
- A sketch that crashes on boot can make the port vanish before the uploader
  sees it. The double-tap recovery above always works.

### Unit tests need a host compiler

`pio test -e native` compiles for the laptop, not the board, so it needs g++ or
MSVC on `PATH`. Neither is installed on this machine yet, so **the native test
environment is untested here** — it runs in CI (ubuntu-latest) but not locally
until you install MinGW-w64 or the MSVC Build Tools.

## Serial output

```
=== Arduino Nano ESP32 ===
build   : Sep 23 2026 12:00:00 (dev)
chip    : ESP32-S3 rev 0, 2 core(s) @ 240 MHz
flash   : 16777216 bytes
reset   : 1

[    5000 ms] env=dev heap=274512/342100 psram=8386access rssi=n/a
```
