# nano-esp32

PlatformIO project for the **Arduino Nano ESP32** (ESP32-S3): a passive WiFi and
Bluetooth LE survey tool. It alternates a WiFi sweep and a BLE sweep and prints
what it finds to the serial monitor. It never joins a network or connects to a
device.

- **WiFi:** SSID, BSSID, signal (RSSI), channel, security type. Listen-only
  (passive) scan by default.
- **BLE:** address and address type, signal, name, manufacturer, TX power,
  advertised services.

Tuning (scan mode, dwell time, BLE window) lives in `include/config.h`.

## Commands

PlatformIO lives in an isolated venv and is not on `PATH` by default. Either add
`C:\Users\jerre\.platformio\penv\Scripts` to `PATH`, or use the full path:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

& $pio run                  # compile
& $pio run -t upload        # flash over USB (DFU)
& $pio device monitor       # serial console, Ctrl+C to exit
```

## If upload fails

The Nano ESP32 uses native USB, so the COM port drops and re-appears on reset.
If upload reports "no DFU device found", **double-tap the RESET button** to force
the bootloader (the LED pulses green), then upload again.
