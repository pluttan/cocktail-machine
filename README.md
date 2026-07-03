<div align="center">

# cocktail-machine

**ESP32-S2 automated cocktail dispenser with touchscreen, Telegram bot, and QR ordering**


</div>

Automated cocktail machine powered by an ESP32-S2 Mini with 6 peristaltic pumps, a 3.5" ILI9488 touchscreen with XPT2046 touch controller, air bubble and cup presence sensors, a QR-code camera module, WS2812B LED effects, and a full-featured Telegram bot for remote recipe management, pump calibration, and ordering.

## ■ Features

- ❖ **6 peristaltic pumps** — LEDC PWM-driven with per-pump flow rate calibration (ml/sec), air bubble detection via ADC sensors
- ❖ **3.5" touchscreen** — ILI9488 display with XPT2046 touch (LovyanGFX), shows recipe selection, dispensing progress, and strength adjustment
- ❖ **Telegram bot** — full recipe CRUD, pump content management, pour/dry-run calibration, auto-calibration of air sensors, LED effect control, live statistics
- ❖ **QR code ordering** — ESP32-CAM module scans QR codes via SPI bridge to start cocktail preparation hands-free
- ❖ **Recipe system** — up to 30 recipes with 8 ingredients each, configurable strength points per ingredient, stored in SPIFFS
- ❖ **Cup detection** — ADC-based cup presence sensor pauses pumps on cup removal and resumes on return
- ❖ **LED effects** — WS2812B addressable LED with 16 effects (rainbow, fire, aurora, candle, and more) via RMT DMA
- ❖ **OTA + web logs** — HTTP server for remote log access, OTA firmware updates

## ■ Stack

<div align="center">

| Component | Technology |
|-----------|-----------|
| MCU (main) | ESP32-S2 Mini |
| MCU (camera) | ESP32-CAM (AI-Thinker) |
| Framework | ESP-IDF + FreeRTOS |
| Build | PlatformIO |
| Display | ILI9488 320x480 (SPI, LovyanGFX) |
| Touch | XPT2046 (standalone SPI driver) |
| Pumps | 6x peristaltic, LEDC PWM |
| Sensors | 6x air (ADC1) + 1x cup presence (ADC1) |
| Camera | ESP32-CAM + quirc (QR decoding) |
| Communication | SPI slave bridge (CAM to S2) |
| Bot | Telegram Bot API via HTTP proxy |
| LEDs | WS2812B (RMT DMA) |
| Storage | SPIFFS (recipes, calibration, stats) |
| PCB | Custom PCB (MCU board + sensor board) |

</div>

## ■ How It Works

```
1. User picks a recipe on the touchscreen, scans a QR code (ESP32-CAM → SPI bridge), or sends an order through the Telegram bot inline-keyboard UI.
2. If the cup is removed mid-pour, all pumps pause automatically and resume when the cup is returned — detected via the ADC cup presence sensor.
3. Each of the 6 peristaltic pumps runs at its calibrated flow rate (ml/sec) under LEDC PWM; per-channel air bubble sensors stop the pump if the line runs dry.
4. WS2812B LEDs animate via RMT DMA during dispensing; recipes, calibration data, and statistics persist in SPIFFS and are manageable remotely via the Telegram bot or OTA HTTP server.
```

## ■ Usage

### Main Firmware

```bash
cd firmware-idf
pio run                       # build (env:esp32s2)
pio run --target upload       # flash via USB (1200bps touch reset)
pio run --target uploadfs     # flash SPIFFS image (data/: recipes, fonts, wifi.txt, bot.txt)
```

Hardware bring-up builds: `pio run -e touch_test` (XPT2046 touch) and `pio run -e pump_test` (pump/sensor diagnostics).

### Camera Firmware

```bash
cd cam-firmware
pio run --target upload       # flash via USB (env:esp32cam)
pio run --target uploadfs     # flash SPIFFS image (data/wifi.txt)
```

### Telegram Bot

Wi-Fi credentials and the Telegram bot token are read at runtime from SPIFFS — put the SSID/password (two lines) in `firmware-idf/data/wifi.txt` and the bot token in `firmware-idf/data/bot.txt`, then flash the filesystem image (`pio run -t uploadfs`). The bot reaches the Telegram API through an HTTP reverse proxy (`TG_PROXY_URL`) and is driven entirely through an inline-keyboard UI.

## ■ Repository Structure

```
cocktail-machine/
├── firmware-idf/        # Main ESP32-S2 firmware (PlatformIO + ESP-IDF)
│   ├── src/
│   │   ├── app_main.cpp     # State machine + HTTP server (OTA/logs)
│   │   ├── display.*        # ILI9488 + UI screens (LovyanGFX)
│   │   ├── pumps.*          # Pump control + calibration + ADC sensors
│   │   ├── recipes.*        # Recipe data model + persistence
│   │   ├── telegram_bot.h   # Telegram bot (header-only, inline keyboard UI)
│   │   ├── led_effects.*    # WS2812B 16-effect engine
│   │   ├── qr_serial.*      # SPI master bridge to ESP32-CAM
│   │   └── xpt2046.*        # Touch controller driver
│   └── data/                # SPIFFS image: recipes.json, fonts, wifi.txt, bot.txt
├── cam-firmware/        # ESP32-CAM QR scanner firmware (SPI slave, quirc)
├── bot-desktop-test/    # Desktop bot test harness (libcurl + cJSON)
├── PCB/                 # Gerber files for custom boards (MCU + sensors)
├── docs/                # Flashing guides
└── SCH_*.svg            # Circuit schematics
```

## ■ License

MIT © [pluttan](https://github.com/pluttan)
