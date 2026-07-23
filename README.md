# Moo_Bot

A small desk companion device with an animated character face on an LCD, functioning as a habit tracker, task tracker, reminder device, and clock/calendar.

Two SKUs (Robot / Cow), one PCB, one firmware. Persona selected by build flag / NVS — never forked.

## Repo layout

```
firmware/     ESP-IDF v5.x project (LVGL 9, component architecture)
docs/         bringup guides, animation specs
backend/      FastAPI + PostgreSQL + EMQX      (later)
webapp/       React PWA                        (later)
hardware/     KiCad + enclosure CAD            (later)
```

## Dev rig (current)

| Item | Part |
|---|---|
| MCU board | ESP32-S3 DevKit, WROOM-1-**N16R8** (16 MB flash, 8 MB octal PSRAM) |
| Display | 2.8" SPI TFT 240×320, **ILI9341** + **XPT2046** resistive touch |
| Audio | Audio integration module: MAX98357A I2S amp + ICS-43434 I2S mic |
| Later | DS3231 RTC, TTP223 touch, WS2812B LED |

Production display target is ST7789 2.4" — dev rig differs; `hal_board` isolates the driver.

## Hardware connections

All GPIOs are Kconfig options (`idf.py menuconfig` → *MooBot Board*). Table = defaults.

### Display — ILI9341 (SPI2, demo 1 ✅)

| Display pin | GPIO | Note |
|---|---|---|
| VCC | 5V | onboard LDO |
| GND | GND | |
| CS | 10 | |
| RESET | 8 | |
| DC | 9 | |
| SDI (MOSI) | 11 | |
| SCK | 12 | |
| SDO (MISO) | 13 | required for touch reads |
| LED | 3V3 | backlight full-on; GPIO+transistor later |

### Touch — XPT2046 (shares SPI2, demo 2)

| Touch pin | GPIO | Note |
|---|---|---|
| T_CLK | 12 | shared SCK |
| T_DIN | 11 | shared MOSI |
| T_DO | 13 | shared MISO |
| T_CS | 14 | |
| T_IRQ | 21 | pen-down interrupt |

### Audio module — MAX98357A amp + ICS-43434 mic (demo 2)

Module exposes **two independent I2S buses** (verified from board silkscreen):
mic trio `SCK/WS/SD`, amp trio `BCLK/LRCLK/DIN`. `SD` = mic serial data out,
not shutdown. Speaker is mounted on the module (`SPK1`).

Wire amp → I2S0, mic → I2S1. Independent sample rates (e.g. 44.1 kHz playback,
16 kHz capture), no full-duplex config.

| Module pin | Chip | GPIO | Note |
|---|---|---|---|
| VCC | — | 5V | louder amp output than 3V3 |
| GND | — | GND | one pin from left GND header |
| BCLK | amp | 40 | I2S0 |
| LRCLK | amp | 41 | I2S0 |
| DIN | amp | 42 | I2S0, audio data → amp |
| SCK | mic | 38 | I2S1 |
| WS | mic | 39 | I2S1 |
| SD | mic | 47 | I2S1, audio data ← mic |

### Pat sensor + LED (demo 2 ✅)

| Part | GPIO | Note |
|---|---|---|
| TTP223 OUT | 6 | head-pat, posedge interrupt; VCC=3V3 |
| WS2812B ×4 DIN | 16 | RMT; strip on 5V, common GND |

### Future peripherals (per production plan)

| Part | GPIO | Note |
|---|---|---|
| DS3231 SDA | 4 | I2C |
| DS3231 SCL | 5 | I2C |
| Buzzer | 15 | LEDC tone (production BOM; dev rig uses I2S amp) |

Avoid strapping pins GPIO0/3/45/46. GPIO48 often has onboard RGB LED — check your devkit.

## Build

```bash
. $HOME/esp/esp-idf/export.sh        # ESP-IDF v5.3+
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor # native USB port
```

Full wiring + troubleshooting: [docs/demo1-bringup.md](docs/demo1-bringup.md)
