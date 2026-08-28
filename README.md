# Moo_Bot

A small desk companion device with an animated character face on an LCD.

**v0.1.0** — a talking toy for a toddler: touch the cow and it reacts, or ask
it out loud for a story and it tells one in Marathi.

Two SKUs (Robot / Cow), one PCB, one firmware. Persona selected by build flag
/ NVS — never forked.

## What works today

- **11 animated cow expressions** (sprite sheets → RGB565, played by `anim_engine`)
- **Voice stories via the Gemini API** — hold the AI screen, speak, and the cow
  answers with a short Marathi story. One round trip hears the request and
  writes the story; a second speaks it. No separate speech-to-text service.
- **Real PCM audio out of a single-wire analog speaker** — the ESP32-S3 has no
  DAC, so the LEDC backend runs an 8-bit PWM-DAC at a 312.5 kHz carrier
- **Synthesized creature sounds** (moo, munch, giggle, yawn) — generated from
  oscillators at boot, no audio assets, no flash cost
- **INMP441 mic** with noise-floor-relative voice detection and endpointing
- **Resistive touchscreen** + **head-button gestures**, **WS2812 LED** feedback
- **Clock** screen, **WiFi + SNTP** time

### Interaction model

Deliberately split: **the touchscreen never waits.** Only the head button keeps
multi-tap grouping, because only it needs a double-tap. A toddler mashes the
screen and gets one response per touch, every time — at 18 months a response
later than ~300 ms stops reading as something he caused.

| Gesture | Action |
|---|---|
| Touch the **face** screen | next play card — animation + sound, **immediate** |
| Touch the **AI** screen | ask for a story |
| Head button ×1 | next play card |
| Head button ×2 | next screen: face → clock → AI → face |
| Head button hold | volume ramp (bar on left); direction flips each hold |

The AI screen shows status in **English only**. LVGL ships Montserrat, which
has no Devanagari glyphs, and Devanagari needs conjunct/matra shaping LVGL
does not do — Marathi text would render as broken boxes. The story is audio.

## Repo layout

```
firmware/     ESP-IDF v5.3 project (LVGL 9, component architecture)
  components/
    anim_engine/     sprite-sheet player
    audio_in/        INMP441 mic on I2S1; int16 PCM, level, VAD + record
    audio_out/       tones + streaming PCM; LEDC PWM-DAC / PDM / I2S backends
    face_ui/         LVGL screens: face, clock, AI, volume overlay
    gemini/          streamed audio upload + TTS decoded into the speaker
    hal_board/       display, touch, RTC, pat button, LED bringup
    net_sync/        WiFi STA + SNTP (+ on-got-IP hook)
    persona_assets/  generated sprite C arrays (tools/sprites2lvgl.py)
    sfx/             synthesized creature sounds (no assets)
  components_disabled/
                     habit tracking, Google Calendar/Tasks sync, on-device
                     web UI. Working code, cut from v0.1.0 to make room for
                     the voice toy. Outside the build path; move a directory
                     back into components/ to revive it.
  main/assets/       moo_test.pcm — the "hello Shivaansh" greeting clip
docs/         bringup guides, datasheets, animation specs
tools/        sprites2lvgl.py, sfx_preview.py (render sounds on the laptop)
character.md  sprite generation prompts (Nano Banana / Gemini)
backend/      FastAPI + PostgreSQL + EMQX      (milestone 4-5)
webapp/       React PWA                        (milestone 4-5)
hardware/     KiCad + enclosure CAD            (later)
```

## Gemini setup

1. Get a key from [aistudio.google.com/apikey](https://aistudio.google.com/apikey).
   **Not** the Google Cloud console — that is Vertex AI, which needs OAuth
   tokens rather than a key, and an org policy may block key creation there
   entirely.
2. `idf.py menuconfig` → *MooBot Gemini* → paste. It lands in `sdkconfig`,
   which is gitignored.
3. Without a key, `gemini_configured()` folds to a compile-time false and the
   linker drops the whole story path — "no key" and "broken" look identical
   from the outside. The boot log says `no API key — stories disabled`.

The key is readable by anyone who dumps the flash. Fine for a unit on a desk;
for anything handed out, proxy Gemini behind your own backend and give devices
a revocable token.

## Dev rig (current)

| Item | Part |
|---|---|
| MCU board | ESP32-S3 DevKit, WROOM-1-**N16R8** (16 MB flash, 8 MB octal PSRAM) |
| Display | 2.8" SPI TFT 320×240, **ILI9341/9342-class** + **XPT2046** resistive touch |
| Audio out | SmartElex analog speaker module, LEDC PWM on GPIO 7 |
| Mic | INMP441 I2S MEMS, 24-bit, on I2S1 |
| LED | WS2812B ×4 |
| Input | TTP223 capacitive pad (head pat) |
| Not yet wired | DS3231 RTC (WiFi/SNTP covers time for now) |

Production display target is ST7789 2.4" — dev rig differs; `hal_board` isolates the driver.

## Hardware connections

All GPIOs are Kconfig options (`idf.py menuconfig` → *MooBot Board*). Table = defaults.

### Display + touch — shared SPI2 bus

| Pin | GPIO | Note |
|---|---|---|
| VCC | 5V | onboard LDO |
| GND | GND | common ground with everything |
| CS (display) | 10 | |
| RESET | 8 | |
| DC | 9 | |
| SDI / MOSI | 11 | shared with touch T_DIN |
| SCK | 12 | shared with touch T_CLK |
| SDO / MISO | 13 | shared with touch T_DO |
| LED (backlight) | 3V3 | full-on; GPIO+transistor later |
| T_CS (touch) | 14 | |
| T_IRQ (touch) | 21 | pen-down interrupt |

Touch orientation vs. the panel is set by Kconfig (*Touch (XPT2046)* → swap/mirror);
calibrate by tapping corners and watching the `touch @ (x,y)` log.

### Audio out — three interchangeable backends

Pick one under *MooBot Board → Audio out → Output backend*. `audio_out`'s API
is identical for all three: tones, melodies, volume, and streaming PCM.

The ESP32-S3 has **no DAC**. That does not mean no voice — it means the analog
waveform has to be synthesised on a digital pin, which the first two backends
do in different ways.

**LEDC PWM-DAC → analog speaker module** (default and shipping config;
SmartElex-class "Digital Speaker Module", 3 wires)

| Module pin | GPIO | Note |
|---|---|---|
| VCC | 3V3, then 5V | see caution below |
| GND | GND | |
| SIGNAL | 7 | Not 15 — keeps 15/17/18 free for a future SDMMC trio |

Tones are square waves at the note frequency. **PCM is played as a PWM-DAC**:
8-bit duty updated per sample from a GPTimer ISR at a 312.5 kHz carrier
(80 MHz APB ÷ 2⁸). The carrier is ~20× above the audio band and the module's
own amplifier bandwidth does the reconstruction filtering, so **no external
filter is needed** — which is the whole reason to use this backend on this
module. Cost: 8 bits (~48 dB) instead of 16, and an interrupt per sample.

The ISR reads duty bytes from a ring in internal DRAM and writes LEDC
registers via `ledc_ll_*`. It must never touch flash (a concurrent NVS write
disables the cache) and cannot call `ledc_set_duty()`, which takes a semaphore.

Loudness comes mostly from the module's trimpot. Start at VCC=3V3 with the pot
low: if the module DC-couples SIGNAL, a 3.3 V 50 %-duty square sits at 1.65 V
DC and can saturate the input stage. Once it sounds clean, move to 5 V. Backing
the pot off slightly from maximum removes the amp's own hiss.

**PDM sigma-delta → the same analog speaker module** (same single wire)

16-bit PCM via the S3's hardware sigma-delta modulator, DMA-fed, no per-sample
interrupt. Better resolution than the PWM-DAC, but the carrier is 6.14 MHz and
**requires an RC low-pass on SIGNAL** — 1 kΩ in series, 10 nF to GND, ≈16 kHz
corner, about −52 dB at the carrier. Without it the output is recognisable
audio buried in hiss. PDM is I2S0-only on the S3.

**I2S → MAX98357A** — *not currently wired.* The combo amp+mic module was
removed from the dev rig, so 40/41/42 are free. Cleanest of the three: true
16-bit PCM into a real DAC+amp. Re-select it if an amp goes back on the board.

| Module pin | GPIO | Note |
|---|---|---|
| VCC | 5V | louder than 3V3 |
| GND | GND | |
| BCLK | 40 | |
| LRCLK | 41 | |
| DIN | 42 | audio → amp |

Pinned to I2S0 — `audio_in` owns I2S1.

### Microphone — INMP441 (I2S)

| Module pin | GPIO | Note |
|---|---|---|
| VDD | **3V3** | abs max 3.63 V; 5 V destroys it |
| GND | GND | |
| SCK | 38 | I2S1 BCLK |
| WS | 39 | I2S1 WS |
| SD | 47 | I2S1 DIN |
| L/R | **GND** | selects the LEFT slot — must be tied, floating is undefined |

24-bit samples arrive MSB-aligned in 32-bit slots. `audio_in` sign-extends,
DC-blocks (the part has a real DC offset), applies
*MOO_MIC_GAIN_SHIFT* and returns int16 mono PCM at 16 kHz.

Bringup: turn *MOO_MIC_MONITOR* on (off in the release config) and watch the
log.

```
I (5231) audio_in: level  -58.3 dBFS   peak  1204
```

Silence should read near −80 dBFS, speech −45 to −25. A dead −90.0 usually
means the L/R slot is wrong — flip *MOO_MIC_SLOT_RIGHT* before re-checking
wiring. Peak pinned at 32767 means *MOO_MIC_GAIN_SHIFT* is too high (each
bit is +6 dB).

The AI screen's level bar is the same signal, so the mic can also be checked
without the log.

**Recording.** `audio_in_record()` measures the room's noise floor first, then
treats anything *MOO_MIC_VAD_MARGIN_DB* above it as speech — a fixed threshold
is useless when the same margin has to work on a quiet desk and next to a fan.
It stops after ~900 ms of quiet and keeps ~200 ms of lead-in before the onset,
without which the first syllable of a short word is clipped off.

The monitor task and the recorder are both plain readers of the same DMA
stream, so whoever calls first gets those samples. The recorder claims
exclusivity while it runs; otherwise the monitor silently steals about half of
every recording.

> **Known issue in v0.1.0:** the shipped *MOO_MIC_GAIN_SHIFT=4* clips on close
> speech — peaks pin at 32767 and Gemini transcribes distorted audio. It works,
> but **2** is the better value. Left at the tested setting for the release;
> retest and lower it.

### Pat sensor + LED

| Part | GPIO | Note |
|---|---|---|
| TTP223 OUT | 6 | head pat; VCC = **3V3** (5V would over-drive the S3 GPIO) |
| WS2812B ×4 DIN | 16 | RMT; strip on 5V, common GND |

### Future peripherals

| Part | GPIO | Note |
|---|---|---|
| DS3231 SDA | 4 | I2C; SNTP auto-writes it once wired |
| DS3231 SCL | 5 | I2C |

### Free GPIOs

`1, 2, 7*, 15, 17, 18, 40, 41, 42` — 40/41/42 freed when the combo audio
module came off. `48` is usable but drives the onboard RGB LED.
(*7 is taken by the PWM speaker when the LEDC backend is selected.*)

Unavailable on N16R8: `26–32` SPI flash, `33–37` octal PSRAM, `19/20`
native USB, `43/44` UART0, `0/3/45/46` strapping.

Power the whole rig from one **external 5V supply** (≥2 A), all grounds common.
TTP223 stays on 3V3.

## Build

```bash
. $HOME/esp/esp-idf/export.sh          # ESP-IDF v5.3+
cd firmware
idf.py set-target esp32s3              # first time only
idf.py menuconfig                      # MooBot Network -> WiFi SSID/pass
                                       # MooBot Gemini  -> API key
idf.py build
idf.py -p /dev/ttyUSB0 -b 115200 flash monitor
```

`sdkconfig` is gitignored because it holds the WiFi and Gemini secrets, so
**`sdkconfig.defaults` is the only config in the repo** — it encodes the
verified hardware configuration and a fresh clone depends on it being right.

The app image is ~13 MB (uncompressed sprites), so a full flash takes minutes.
Keep the baud at 115200 on a CH340 adapter; 460800 drops the link mid-write on
a marginal cable, and a `-71` (EPROTO) in `dmesg` means the cable or the
adapter, not the baud rate.

First build pulls managed components (LVGL, esp_lvgl_port, esp_lcd_ili9341,
`atanisoft/esp_lcd_touch_xpt2046`, led_strip).

Preview the synthesized sounds on the laptop, without flashing:

```bash
python3 tools/sfx_preview.py --play
```

It mirrors `components/sfx/sfx.c` exactly, so a moo can be tuned by ear in
seconds instead of a four-minute reflash.

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| **White screen while flashing** | Normal — the MCU is halted in the bootloader, so the LCD isn't driven; backlight on = white. It goes to the face once firmware boots. |
| **Lines/noise on screen after boot** | SPI signal integrity on the shared display+touch bus. Lower *LCD → SPI clock MHz* to 10 (or less); enable touch **interrupt mode** so it only reads on touch; keep wires short with a solid common ground. |
| Colors negative (white bg) | Toggle *LCD → Invert panel colors*. |
| Red/blue swapped | `rgb_ele_order` BGR↔RGB in `hal_board.c`. |
| Touch coordinates wrong | *Touch (XPT2046)* swap/mirror; calibrate by corner taps. |
| Sound too quiet | Volume is NVS-persisted — hold the button to raise it; default is now 100%. On the PWM backend the trimpot is the coarse control, turn it up first. On MAX98357A the ceiling is the amp's fixed 9 dB gain (GAIN pin not broken out). |
| Speaker buzzes/distorts on PWM | Module is DC-coupling the 1.65 V mean of the square wave. Drop VCC to 3V3, back the trimpot off, then raise. |
| Mic reads a flat −90.0 dBFS | Wrong I2S slot. Toggle *MOO_MIC_SLOT_RIGHT* — INMP441 L/R must also be physically tied (GND = left). |
| Mic peak pinned at 32767 | *MOO_MIC_GAIN_SHIFT* too high; each bit is +6 dB. |
| No WiFi (reason 201) | AP not found — 2.4 GHz only; check SSID; keep hotspot awake. |
| Speaker plays voice as pure noise | PDM backend without the RC low-pass on SIGNAL. Fit 1 kΩ + 10 nF, or select the LEDC PWM-DAC backend which needs no filter. Pitched-but-noisy means the modulator is fine and only the carrier is unfiltered. |
| `ESP_ERR_NOT_SUPPORTED` from a PCM call | Build is on a backend with no PCM path. Check *Audio out → Output backend*. |
| Story does nothing, no log | No API key — the linker drops the whole path, so it looks identical to broken. Boot log says `no API key — stories disabled`. |
| `understand HTTP 404` | Model retired. Google removes pinned versions from new users; *MOO_GEMINI_MODEL* defaults to the `-latest` alias for this reason. `GET /v1beta/models?key=…` lists what your key can reach. |
| `tts HTTP 4xx` but the first call worked | The TTS model is not available on that tier. Try `gemini-3.1-flash-tts-preview`. |
| VAD never logs `onset` | Speech never crossed the threshold. Lower *MOO_MIC_VAD_MARGIN_DB* or raise *MOO_MIC_GAIN_SHIFT*. |
| `Stack overflow in task story` | Raise the `xTaskCreate` stack in `gemini.c`. Every story logs its remaining headroom. |

Full bringup notes: [docs/demo1-bringup.md](docs/demo1-bringup.md)
