# Demo 1 — Display Bringup + Idle Face

Dev rig: ESP32-S3-DevKit N16R8 + 2.8" SPI TFT (ILI9341 + XPT2046 resistive touch).
Goal: LVGL running, procedural face blinking at randomized 3–8 s, saccade micro-motion.

## Wiring (display → devkit)

| Display pin | DevKit GPIO | Note |
|---|---|---|
| VCC | 5V | module has onboard 3.3V LDO |
| GND | GND | |
| CS | 10 | |
| RESET | 8 | |
| DC | 9 | |
| SDI (MOSI) | 11 | |
| SCK | 12 | |
| LED | 3V3 | backlight full-on; GPIO drive needs transistor — later |
| SDO (MISO) | 13 | needed by touch reads |
| T_CLK | 12 | shared with SCK |
| T_CS | 14 | wire now, used in demo 2 |
| T_DIN | 11 | shared with MOSI |
| T_DO | 13 | shared with MISO |
| T_IRQ | 21 | wire now, used in demo 2 |

All pins are Kconfig options under `MooBot Board` (`idf.py menuconfig`).

Audio module (MAX98357A I2S amp + ICS-43434 I2S mic): not used in demo 1. Park it.
Wiring in README when demo 2 starts.

## Build & flash

```bash
# once: install ESP-IDF v5.3+ (https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
. $HOME/esp/esp-idf/export.sh

cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # native USB port, not the UART one
```

`dependencies.lock` + `managed_components/` are generated on first build
(pulls lvgl, esp_lvgl_port, esp_lcd_ili9341 from the component registry).

## Expected

Dark screen, two rounded white eyes, smile arc. Eyes blink at random 3–8 s,
occasionally dart sideways and return.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Colors wrong (red/blue swapped) | `rgb_ele_order` BGR→RGB in `hal_board.c` |
| Image inverted / washed out | some clones need `esp_lcd_panel_invert_color(panel, true)` |
| Mirrored | flip `esp_lcd_panel_mirror()` args, or rotation flags in `main.c` |
| Noise / tearing on jumpers | drop `MOO_LCD_PCLK_MHZ` 40 → 20 in menuconfig |
| No flash port | use the USB-OTG connector; hold BOOT + tap RESET for download mode |
| White screen | check DC/RST swap first — most common miswire |
