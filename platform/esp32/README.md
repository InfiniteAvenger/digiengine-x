# ESP32-S3 frontend

Target board: **Waveshare ESP32-S3-LCD-1.69** (the Touch variant works too; touch
is ignored). ESP32-S3R8, 16 MB flash, 8 MB PSRAM, 240x280 ST7789V2, PCF85063 RTC,
LiPo charger.

The engine in `engine/` is compiled unchanged as an ESP-IDF component; this
folder only implements `vpet::Platform`:

| File          | Job                                                          |
|---------------|--------------------------------------------------------------|
| `main.cpp`    | Power latch, buttons, littlefs, clock, 30 fps game loop      |
| `display.cpp` | 120x120 canvas -> 2x -> panel over SPI DMA (40 MHz)          |
| `rtc.cpp`     | PCF85063 over I2C; holds UTC across power-off                |
| `board.h`     | Pin map (from Waveshare's `pin_config.h` and wiki)           |

## Buttons

| Button | Default pin                 | Wiring                         |
|--------|-----------------------------|--------------------------------|
| A      | GPIO0, onboard **BOOT**     | none                           |
| B      | onboard **PWR** (SYS_OUT)   | none                           |
| C      | GPIO18                      | tactile switch from GPIO18 to GND |

So the board is playable with one added switch. All three pins and the board
revision (V1/V2, printed on the back) are set in `idf.py menuconfig` under
**vpet board**. Check Waveshare's pinout image that your chosen pins are on
the header before wiring.

## Build and flash

From an ESP-IDF PowerShell (installed with `eim`), in this folder:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

Replace `COM5` with the board's port (Device Manager, "USB Serial Device").
`flash` writes the firmware and the demo pack. Saves live in their own
partition, so reflashing keeps your pet.

The monitor prints a catch-up benchmark at boot and the slowest frame time
every 10 seconds.

## Clock

The RTC stores UTC. When it has never been set (or its backup battery died),
the firmware sets it to the time it was **built**, and uses the build PC's UTC
offset for bedtimes. Flash soon after building, and rebuild after a daylight
saving change. An on-device clock screen will replace this.

## Not done yet

- Clock / time zone setting screen
- Battery level, sleep and screen-off when idle
- Buzzer sounds
- Baked binary packs (packs load from JSON on flash for now)
