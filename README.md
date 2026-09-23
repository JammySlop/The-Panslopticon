# nano-esp32

PlatformIO project for the **Arduino Nano ESP32** (ESP32-S3). Currently a blink
sketch that confirms the toolchain, USB, and upload path work.

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
