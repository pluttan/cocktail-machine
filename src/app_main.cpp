#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "esp_spiffs.h"
#include <sys/stat.h>
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"

#include "display.h"
#include "recipes.h"
#include "pumps.h"
#include "qr_serial.h"
#include "cam_bridge.h"
#include "telegram_bot.h"
#include "strings.h"

static const char* TAG = "COCKTAIL";

// ══════════════════════════════════════
// ── Inter-task communication ──
// ══════════════════════════════════════

// Touch events: display_task → fsm_task
enum TouchEventType : uint8_t {
    TOUCH_NONE,
    TOUCH_TOP_TAP,          // debug tap
    TOUCH_STRENGTH_DOT,     // (ingredIdx<<8 | pointIdx)
    TOUCH_STRENGTH_TOGGLE,  // (ingredIdx<<8 | 0xFF)
    TOUCH_STRENGTH_GO,
    TOUCH_STRENGTH_CANCEL,
    TOUCH_STOP_BUTTON,
    TOUCH_BOTTLE_CONTINUE,
    TOUCH_BOTTLE_CANCEL,
    TOUCH_SCREEN,           // generic touch (for DONE screen)
};

struct TouchEvent {
    TouchEventType type;
    int value;  // encoded tap value
};

// Display commands: fsm_task → display_task
enum DisplayCmd : uint8_t {
    DCMD_IDLE,
    DCMD_SELECT_STRENGTH,
    DCMD_DISPENSING_STATIC,
    DCMD_DISPENSING_UPDATE,
    DCMD_BOTTLE_EMPTY,
    DCMD_DONE,
};

struct DisplayMsg {
    DisplayCmd cmd;
    int recipeIdx;
    float* mults;           // for SELECT_STRENGTH
    const char* text;       // cocktail/ingredient name
    float progressPct;      // for DISPENSING_UPDATE
    int activePump;
    char ingredText[60];
    int cocktailsToday;
};

static QueueHandle_t q_touch = NULL;      // TouchEvent, display→fsm
static QueueHandle_t q_display = NULL;    // DisplayMsg, fsm→display
static QueueHandle_t q_qr = NULL;         // int (recipe ID), qr→fsm
static SemaphoreHandle_t mtx_pumps = NULL; // pump state access

static int64_t millis_now() { return esp_timer_get_time() / 1000; }

// ══════════════════════════════════════
// ── Pumps Task (highest priority) ──
// ══════════════════════════════════════

static void pumps_task(void* arg) {
    ESP_LOGI(TAG, "pumps_task started");
    while (1) {
        xSemaphoreTake(mtx_pumps, portMAX_DELAY);
        pumps_update();
        xSemaphoreGive(mtx_pumps);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ══════════════════════════════════════
// ── Display Task ──
// ══════════════════════════════════════

// Current screen state for touch detection
static volatile DisplayCmd disp_current_screen = DCMD_IDLE;
static int disp_recipe_idx = -1;

static void display_task(void* arg) {
    ESP_LOGI(TAG, "display_task started");

    // Touch polling + command processing
    while (1) {
        // Check for display commands from FSM
        DisplayMsg msg;
        while (xQueueReceive(q_display, &msg, 0) == pdTRUE) {
            switch (msg.cmd) {
            case DCMD_IDLE:
                display_drawIdle(msg.cocktailsToday);
                disp_current_screen = DCMD_IDLE;
                break;
            case DCMD_SELECT_STRENGTH:
                disp_recipe_idx = msg.recipeIdx;
                display_drawSelectStrength(RECIPES[msg.recipeIdx], msg.mults);
                disp_current_screen = DCMD_SELECT_STRENGTH;
                break;
            case DCMD_DISPENSING_STATIC:
                display_drawDispensingStatic(msg.text);
                disp_current_screen = DCMD_DISPENSING_UPDATE;
                break;
            case DCMD_DISPENSING_UPDATE:
                display_updateDispensing(msg.progressPct, msg.activePump, msg.ingredText);
                break;
            case DCMD_BOTTLE_EMPTY:
                display_drawBottleEmpty(msg.text);
                disp_current_screen = DCMD_BOTTLE_EMPTY;
                break;
            case DCMD_DONE:
                display_drawDone();
                disp_current_screen = DCMD_DONE;
                break;
            }
        }

        // Poll touch and send events to FSM
        TouchEvent evt = {TOUCH_NONE, 0};

#if DEBUG_SCREEN_TAP
        if (display_checkTopTap()) {
            evt.type = TOUCH_TOP_TAP;
            xQueueSend(q_touch, &evt, 0);
            vTaskDelay(pdMS_TO_TICKS(300)); // debounce
            continue;
        }
#endif

        switch (disp_current_screen) {
        case DCMD_SELECT_STRENGTH:
            if (disp_recipe_idx >= 0) {
                int tap = display_checkStrengthTap(RECIPES[disp_recipe_idx]);
                if (tap == -3)      { evt.type = TOUCH_STRENGTH_GO; }
                else if (tap == -2) { evt.type = TOUCH_STRENGTH_CANCEL; }
                else if (tap >= 0) {
                    int pointIdx = tap & 0xFF;
                    if (pointIdx == 0xFF) evt.type = TOUCH_STRENGTH_TOGGLE;
                    else                  evt.type = TOUCH_STRENGTH_DOT;
                    evt.value = tap;
                }
            }
            break;
        case DCMD_DISPENSING_UPDATE:
            if (display_checkStopButton()) evt.type = TOUCH_STOP_BUTTON;
            break;
        case DCMD_BOTTLE_EMPTY: {
            int btn = display_checkBottleEmptyButton();
            if (btn == 0) evt.type = TOUCH_BOTTLE_CONTINUE;
            if (btn == 1) evt.type = TOUCH_BOTTLE_CANCEL;
            break;
        }
        case DCMD_DONE:
            if (display_isTouched()) evt.type = TOUCH_SCREEN;
            break;
        default: break;
        }

        if (evt.type != TOUCH_NONE) {
            xQueueSend(q_touch, &evt, 0);
            vTaskDelay(pdMS_TO_TICKS(300)); // debounce
        } else {
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }
}

// ══════════════════════════════════════
// ── QR Task ──
// ══════════════════════════════════════

static void qr_task(void* arg) {
    ESP_LOGI(TAG, "qr_task started");
    while (1) {
        int id = qr_check();
        if (id >= 0) {
            xQueueSend(q_qr, &id, 0);
        }

        // Also check CAM_FLASH command
        static char cmd_buf[32];
        static int cmd_pos = 0;
        size_t avail = 0;
        uart_get_buffered_data_len(UART_NUM_0, &avail);
        uint8_t byte;
        while (avail-- > 0 && uart_read_bytes(UART_NUM_0, &byte, 1, 0) > 0) {
            if (byte == '\n' || byte == '\r') {
                if (cmd_pos > 0) {
                    cmd_buf[cmd_pos] = 0;
                    if (strcmp(cmd_buf, "CAM_FLASH") == 0) cam_bridge_enter();
                    cmd_pos = 0;
                }
            } else if (cmd_pos < 30) {
                cmd_buf[cmd_pos++] = (char)byte;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ══════════════════════════════════════
// ── Bot Task ──
// ══════════════════════════════════════

static void bot_task(void* arg) {
    ESP_LOGI(TAG, "bot_task started");
    while (1) {
        bot_update();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ══════════════════════════════════════
// ── FSM Task ──
// ══════════════════════════════════════

enum AppState : uint8_t {
    STATE_IDLE,
    STATE_SELECT_STRENGTH,
    STATE_DISPENSING,
    STATE_BOTTLE_EMPTY,
    STATE_DONE,
};

static AppState state = STATE_IDLE;
static int currentRecipe = -1;
static int cocktailsToday = 0;
static int64_t stateEnterTime = 0;
static int emptyPump = -1;
static float ingredMult[MAX_INGR];
static float totalVolume = 0;

// Bot interface
const char* bot_state_name() {
    static const char* names[] = {"idle", "select", "dispensing", "empty", "done"};
    return names[state < 5 ? state : 0];
}
int bot_cocktails_today() { return cocktailsToday; }

static void send_display(DisplayMsg& msg) {
    xQueueSend(q_display, &msg, pdMS_TO_TICKS(100));
}

static void enterState(AppState newState) {
    state = newState;
    stateEnterTime = millis_now();
    ESP_LOGI(TAG, "[STATE] -> %d", (int)newState);
}

static void initIngredMults() {
    if (currentRecipe < 0) return;
    const Recipe& r = RECIPES[currentRecipe];
    for (int i = 0; i < r.numIngredients; i++) {
        const StrengthOpt* opt = findStrengthOpt(r, i);
        ingredMult[i] = opt ? opt->points[opt->defaultIdx] : 1.0f;
    }
}

static void startDispensing() {
    if (currentRecipe < 0) return;
    const Recipe& r = RECIPES[currentRecipe];
    totalVolume = 0;
    xSemaphoreTake(mtx_pumps, portMAX_DELAY);
    for (int i = 0; i < r.numIngredients; i++) {
        uint16_t ml = (uint16_t)(r.ingredients[i].baseMl * ingredMult[i]);
        totalVolume += ml;
        int pump = findPump(r.ingredients[i].name);
        if (pump < 0 || ml == 0) continue;
        pumps_startTask(pump, ml);
    }
    xSemaphoreGive(mtx_pumps);
}

static float calcProgress() {
    if (totalVolume <= 0) return 100.0f;
    float done = 0;
    xSemaphoreTake(mtx_pumps, portMAX_DELAY);
    for (int i = 0; i < MAX_PUMPS; i++) done += pumps_getDispensed(i);
    xSemaphoreGive(mtx_pumps);
    float pct = done / totalVolume * 100.0f;
    return pct > 100.0f ? 100.0f : pct;
}

static int findActivePump() {
    xSemaphoreTake(mtx_pumps, portMAX_DELAY);
    int result = -1;
    for (int i = 0; i < MAX_PUMPS; i++) {
        if (pumps_getState(i) == PUMP_RUNNING) { result = i; break; }
    }
    xSemaphoreGive(mtx_pumps);
    return result;
}

static void fsm_task(void* arg) {
    ESP_LOGI(TAG, "fsm_task started");

    // Initial screen
    DisplayMsg dmsg = {};
    dmsg.cmd = DCMD_IDLE;
    dmsg.cocktailsToday = cocktailsToday;
    send_display(dmsg);

    while (1) {
        int64_t elapsed = millis_now() - stateEnterTime;

        // Check QR
        int qr_id;
        if (xQueueReceive(q_qr, &qr_id, 0) == pdTRUE && state == STATE_IDLE) {
            currentRecipe = qr_id;
            ESP_LOGI(TAG, "[QR] Recipe %d: %s", qr_id, RECIPES[qr_id].name);
            initIngredMults();
            enterState(STATE_SELECT_STRENGTH);

            dmsg = {};
            dmsg.cmd = DCMD_SELECT_STRENGTH;
            dmsg.recipeIdx = currentRecipe;
            dmsg.mults = ingredMult;
            send_display(dmsg);
        }

        // Check touch events
        TouchEvent tevt;
        while (xQueueReceive(q_touch, &tevt, 0) == pdTRUE) {
#if DEBUG_SCREEN_TAP
            if (tevt.type == TOUCH_TOP_TAP) {
                AppState next;
                switch (state) {
                    case STATE_IDLE:             next = STATE_SELECT_STRENGTH; break;
                    case STATE_SELECT_STRENGTH:  next = STATE_DISPENSING;      break;
                    case STATE_DISPENSING:       next = STATE_BOTTLE_EMPTY;    break;
                    case STATE_BOTTLE_EMPTY:     next = STATE_DONE;            break;
                    default:                     next = STATE_IDLE;            break;
                }
                if (currentRecipe < 0) currentRecipe = 0;
                if (next == STATE_SELECT_STRENGTH) initIngredMults();
                enterState(next);

                dmsg = {};
                switch (next) {
                case STATE_IDLE:
                    dmsg.cmd = DCMD_IDLE;
                    dmsg.cocktailsToday = cocktailsToday;
                    break;
                case STATE_SELECT_STRENGTH:
                    dmsg.cmd = DCMD_SELECT_STRENGTH;
                    dmsg.recipeIdx = currentRecipe;
                    dmsg.mults = ingredMult;
                    break;
                case STATE_DISPENSING:
                    dmsg.cmd = DCMD_DISPENSING_STATIC;
                    dmsg.text = RECIPES[currentRecipe].name;
                    break;
                case STATE_BOTTLE_EMPTY:
                    dmsg.cmd = DCMD_BOTTLE_EMPTY;
                    dmsg.text = "???";
                    break;
                case STATE_DONE:
                    dmsg.cmd = DCMD_DONE;
                    break;
                }
                send_display(dmsg);
                continue;
            }
#endif

            switch (state) {
            case STATE_SELECT_STRENGTH:
                if (tevt.type == TOUCH_STRENGTH_GO) {
                    startDispensing();
                    enterState(STATE_DISPENSING);
                    dmsg = {};
                    dmsg.cmd = DCMD_DISPENSING_STATIC;
                    dmsg.text = RECIPES[currentRecipe].name;
                    send_display(dmsg);
                } else if (tevt.type == TOUCH_STRENGTH_CANCEL) {
                    enterState(STATE_IDLE);
                    dmsg = {};
                    dmsg.cmd = DCMD_IDLE;
                    dmsg.cocktailsToday = cocktailsToday;
                    send_display(dmsg);
                } else if (tevt.type == TOUCH_STRENGTH_DOT || tevt.type == TOUCH_STRENGTH_TOGGLE) {
                    int idx = (tevt.value >> 8) & 0xFF;
                    int pointIdx = tevt.value & 0xFF;
                    const Recipe& r = RECIPES[currentRecipe];
                    if (pointIdx == 0xFF) {
                        ingredMult[idx] = (ingredMult[idx] > 0.01f) ? 0.0f : 1.0f;
                    } else {
                        const StrengthOpt* opt = findStrengthOpt(r, idx);
                        if (opt && pointIdx < opt->numPoints)
                            ingredMult[idx] = opt->points[pointIdx];
                    }
                    dmsg = {};
                    dmsg.cmd = DCMD_SELECT_STRENGTH;
                    dmsg.recipeIdx = currentRecipe;
                    dmsg.mults = ingredMult;
                    send_display(dmsg);
                }
                break;

            case STATE_DISPENSING:
                if (tevt.type == TOUCH_STOP_BUTTON) {
                    xSemaphoreTake(mtx_pumps, portMAX_DELAY);
                    pumps_stopAll();
                    xSemaphoreGive(mtx_pumps);
                    ESP_LOGI(TAG, "[STOP]");
                    enterState(STATE_IDLE);
                    dmsg = {};
                    dmsg.cmd = DCMD_IDLE;
                    dmsg.cocktailsToday = cocktailsToday;
                    send_display(dmsg);
                }
                break;

            case STATE_BOTTLE_EMPTY:
                if (tevt.type == TOUCH_BOTTLE_CONTINUE) {
                    xSemaphoreTake(mtx_pumps, portMAX_DELAY);
                    if (emptyPump >= 0) pumps_resume(emptyPump);
                    xSemaphoreGive(mtx_pumps);
                    enterState(STATE_DISPENSING);
                    dmsg = {};
                    dmsg.cmd = DCMD_DISPENSING_STATIC;
                    dmsg.text = RECIPES[currentRecipe].name;
                    send_display(dmsg);
                } else if (tevt.type == TOUCH_BOTTLE_CANCEL) {
                    xSemaphoreTake(mtx_pumps, portMAX_DELAY);
                    pumps_stopAll();
                    xSemaphoreGive(mtx_pumps);
                    enterState(STATE_IDLE);
                    dmsg = {};
                    dmsg.cmd = DCMD_IDLE;
                    dmsg.cocktailsToday = cocktailsToday;
                    send_display(dmsg);
                }
                break;

            case STATE_DONE:
                if (tevt.type == TOUCH_SCREEN) {
                    enterState(STATE_IDLE);
                    dmsg = {};
                    dmsg.cmd = DCMD_IDLE;
                    dmsg.cocktailsToday = cocktailsToday;
                    send_display(dmsg);
                }
                break;

            default: break;
            }
        }

        // Periodic state updates
        switch (state) {
        case STATE_DISPENSING: {
            float pct = calcProgress();
            int active = findActivePump();
            dmsg = {};
            dmsg.cmd = DCMD_DISPENSING_UPDATE;
            dmsg.progressPct = pct;
            dmsg.activePump = active;
            if (active >= 0) {
                xSemaphoreTake(mtx_pumps, portMAX_DELAY);
                snprintf(dmsg.ingredText, sizeof(dmsg.ingredText), S_INGR_ML_FMT,
                         PUMP_CONTENTS[active], pumps_getTarget(active));
                xSemaphoreGive(mtx_pumps);
            }
            send_display(dmsg);

            // Check air
            xSemaphoreTake(mtx_pumps, portMAX_DELAY);
            emptyPump = pumps_airDetectedPump();
            bool allDone = pumps_allDone();
            if (emptyPump >= 0) pumps_stopAll();
            xSemaphoreGive(mtx_pumps);

            if (emptyPump >= 0) {
                ESP_LOGI(TAG, "[AIR] Pump %d", emptyPump);
                enterState(STATE_BOTTLE_EMPTY);
                dmsg = {};
                dmsg.cmd = DCMD_BOTTLE_EMPTY;
                dmsg.text = (emptyPump >= 0 && emptyPump < MAX_PUMPS)
                            ? PUMP_CONTENTS[emptyPump] : "???";
                send_display(dmsg);
            } else if (allDone) {
                cocktailsToday++;
                stats_increment(currentRecipe);
                stats_save();
                ESP_LOGI(TAG, "[DONE] #%d", cocktailsToday);
                enterState(STATE_DONE);
                dmsg = {};
                dmsg.cmd = DCMD_DONE;
                send_display(dmsg);
            }
            break;
        }
        case STATE_DONE:
            if (elapsed > 5000) {
                enterState(STATE_IDLE);
                dmsg = {};
                dmsg.cmd = DCMD_IDLE;
                dmsg.cocktailsToday = cocktailsToday;
                send_display(dmsg);
            }
            break;
        default: break;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ══════════════════════════════════════
// ── app_main ──
// ══════════════════════════════════════

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "=== Cocktail Machine (ESP-IDF) ===");
    ESP_LOGI(TAG, "========================================");

    // USB CDC console
    tinyusb_config_t tusb_cfg = {};
    tinyusb_driver_install(&tusb_cfg);
    tinyusb_config_cdcacm_t acm_cfg = {};
    acm_cfg.usb_dev = TINYUSB_USBDEV_0;
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_0;
    acm_cfg.rx_unread_buf_sz = 256;
    tusb_cdc_acm_init(&acm_cfg);
    esp_tusb_init_console(TINYUSB_CDC_ACM_0);

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Queues & mutex
    q_touch   = xQueueCreate(8, sizeof(TouchEvent));
    q_display = xQueueCreate(4, sizeof(DisplayMsg));
    q_qr      = xQueueCreate(4, sizeof(int));
    mtx_pumps = xSemaphoreCreateMutex();

    // Init display first (for boot log on screen)
    display_init();

    // Boot log helper — draw text on screen
    int bootY = 10;
    auto bootLog = [&](const char* msg) {
        tft.setFont(&fonts::FreeSans9pt7b);
        tft.setTextColor(0xCDD6F4);  // ui::TEXT
        tft.setCursor(10, bootY);
        tft.print(msg);
        bootY += 18;
        ESP_LOGI(TAG, "%s", msg);
    };

    tft.fillScreen(0x1E1E2E);  // ui::BG
    bootLog("Cocktail Machine v2.0 (ESP-IDF)");
    bootLog("================================");

    // SPIFFS check
    {
        char buf[64];
        size_t total = 0, used = 0;
        if (esp_spiffs_info(NULL, &total, &used) == ESP_OK) {
            snprintf(buf, sizeof(buf), "SPIFFS: %d/%d bytes", (int)used, (int)total);
            bootLog(buf);
        } else {
            bootLog("SPIFFS: MOUNT FAILED!");
        }

        // Check key files
        const char* files[] = {"/spiffs/bg.png", "/spiffs/mont_b28.vlw", "/spiffs/recipes.json", "/spiffs/wifi.txt", "/spiffs/bot.txt"};
        const char* names[] = {"bg.png", "mont_b28.vlw", "recipes.json", "wifi.txt", "bot.txt"};
        for (int i = 0; i < 5; i++) {
            struct stat st;
            if (stat(files[i], &st) == 0) {
                snprintf(buf, sizeof(buf), "  %s: %ld bytes", names[i], st.st_size);
            } else {
                snprintf(buf, sizeof(buf), "  %s: MISSING!", names[i]);
            }
            bootLog(buf);
        }
    }

    // Recipes
    {
        char buf[48];
        if (recipes_load()) {
            snprintf(buf, sizeof(buf), "Recipes: %d loaded", NUM_RECIPES);
        } else {
            snprintf(buf, sizeof(buf), "Recipes: LOAD FAILED!");
        }
        bootLog(buf);
    }

    // Pumps
    pumps_init();
    bootLog("Pumps: OK");

    // QR
    qr_init();
    bootLog("QR UART: OK");

    // WiFi + Bot
    bootLog("WiFi: connecting...");
    tft.setCursor(10, bootY - 18);  // update same line
    bot_init();
    {
        char buf[64];
        if (tg_ready) {
            snprintf(buf, sizeof(buf), "WiFi: OK | Bot: ready");
        } else {
            snprintf(buf, sizeof(buf), "WiFi/Bot: FAILED (check wifi.txt)");
        }
        // Redraw last line
        tft.fillRect(10, bootY - 18, 300, 18, 0x1E1E2E);
        tft.setCursor(10, bootY - 18);
        tft.print(buf);
        bootY += 0;  // don't advance, we overwrote
        ESP_LOGI(TAG, "%s", buf);
    }

    bootLog("");
    bootLog("Starting in 3s...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Launch tasks
    // ESP32-S2 is single-core (core 0 only)
    xTaskCreate(pumps_task,   "pumps",   4096,  NULL, 6, NULL);
    xTaskCreate(fsm_task,     "fsm",     16384, NULL, 5, NULL);
    xTaskCreate(display_task, "display", 16384, NULL, 4, NULL);
    xTaskCreate(qr_task,      "qr",      4096,  NULL, 3, NULL);
    xTaskCreate(bot_task,     "bot",     16384, NULL, 2, NULL);

    ESP_LOGI(TAG, "All tasks launched");
    // app_main returns, FreeRTOS scheduler runs tasks
}
