<div align="center">

# cocktail-machine

**Автоматический коктейльный диспенсер на ESP32-S2 с сенсорным экраном, Telegram-ботом и QR-заказом**


</div>

Автоматический коктейльный аппарат на базе ESP32-S2 Mini с 6 перистальтическими насосами, сенсорным экраном 3.5" ILI9488 с контроллером XPT2046, датчиками воздушных пузырей и наличия стакана, камерой для QR-кодов, LED-эффектами WS2812B и полнофункциональным Telegram-ботом для удалённого управления рецептами, калибровки насосов и заказа.

## ■ Возможности

- ❖ **6 перистальтических насосов** — управление LEDC PWM с калибровкой скорости потока (мл/сек) для каждого насоса, обнаружение воздушных пузырей через ADC-датчики
- ❖ **Сенсорный экран 3.5"** — дисплей ILI9488 с тачем XPT2046 (LovyanGFX), отображает выбор рецепта, прогресс налива и регулировку крепости
- ❖ **Telegram-бот** — полный CRUD рецептов, управление содержимым насосов, калибровка налива/холостого хода, автокалибровка воздушных датчиков, управление LED-эффектами, живая статистика
- ❖ **Заказ по QR-коду** — модуль ESP32-CAM сканирует QR-коды через SPI-мост для запуска приготовления коктейля без рук
- ❖ **Система рецептов** — до 30 рецептов по 8 ингредиентов каждый, настраиваемые очки крепости на ингредиент, хранение в SPIFFS
- ❖ **Определение стакана** — ADC-датчик наличия стакана приостанавливает насосы при убирании стакана и возобновляет при возврате
- ❖ **LED-эффекты** — адресные светодиоды WS2812B с 16 эффектами (радуга, огонь, аврора, свеча и др.) через RMT DMA
- ❖ **OTA + веб-логи** — HTTP-сервер для удалённого доступа к логам, обновление прошивки по воздуху

## ■ Стек

<div align="center">

| Компонент | Технология |
|-----------|-----------|
| МК (основной) | ESP32-S2 Mini |
| МК (камера) | ESP32-CAM (AI-Thinker) |
| Фреймворк | ESP-IDF + FreeRTOS |
| Сборка | PlatformIO |
| Дисплей | ILI9488 320x480 (SPI, LovyanGFX) |
| Тач | XPT2046 (standalone SPI driver) |
| Насосы | 6x peristaltic, LEDC PWM |
| Датчики | 6x air (ADC1) + 1x cup presence (ADC1) |
| Камера | ESP32-CAM + quirc (QR decoding) |
| Связь | SPI slave bridge (CAM to S2) |
| Бот | Telegram Bot API via HTTP proxy |
| Светодиоды | WS2812B (RMT DMA) |
| Хранилище | SPIFFS (recipes, calibration, stats) |
| PCB | Custom PCB (MCU board + sensor board) |

</div>

## ■ Как это работает

```
1. Пользователь выбирает рецепт на сенсорном экране, сканирует QR-код (ESP32-CAM → SPI-мост) или отправляет заказ через интерфейс инлайн-клавиатуры Telegram-бота.
2. Если стакан убирают в процессе налива, все насосы автоматически останавливаются и возобновляют работу при его возврате — определяется ADC-датчиком наличия стакана.
3. Каждый из 6 перистальтических насосов работает на откалиброванной скорости потока (мл/сек) под управлением LEDC PWM; поканальные датчики воздушных пузырей останавливают насос при пересыхании магистрали.
4. Светодиоды WS2812B анимируются через RMT DMA в процессе налива; рецепты, данные калибровки и статистика хранятся в SPIFFS и управляются удалённо через Telegram-бот или HTTP-сервер OTA.
```

## ■ Использование

### Основная прошивка

```bash
cd firmware-idf
pio run                       # сборка (env:esp32s2)
pio run --target upload       # прошивка по USB (1200bps touch reset)
pio run --target uploadfs     # прошивка образа SPIFFS (data/: recipes, fonts, wifi.txt, bot.txt)
```

Сборки для первоначальной проверки железа: `pio run -e touch_test` (тач XPT2046) и `pio run -e pump_test` (диагностика насосов/датчиков).

### Прошивка камеры

```bash
cd cam-firmware
pio run --target upload       # прошивка по USB (env:esp32cam)
pio run --target uploadfs     # прошивка образа SPIFFS (data/wifi.txt)
```

### Telegram-бот

Учётные данные Wi-Fi и токен Telegram-бота считываются во время выполнения из SPIFFS — поместите SSID/пароль (две строки) в `firmware-idf/data/wifi.txt`, а токен бота в `firmware-idf/data/bot.txt`, затем прошейте образ файловой системы (`pio run -t uploadfs`). Бот обращается к Telegram API через HTTP-обратный прокси (`TG_PROXY_URL`) и управляется полностью через интерфейс с инлайн-клавиатурой.

## ■ Структура репозитория

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

## ■ Лицензия

MIT © [pluttan](https://github.com/pluttan)
