#include "pumps.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "PUMPS";

struct PumpTask {
    uint16_t targetMl;
    volatile float dispensedMl;
    volatile PumpState state;
    int64_t startTime;
};

static PumpTask tasks[MAX_PUMPS] = {};
static float pumpRate[MAX_PUMPS] = {1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f};

static int64_t millis_idf() { return esp_timer_get_time() / 1000; }

// ── Air tracking ──
// Air doesn't stop the pump — it pauses volume counting.
// Pump keeps running to push air through. dispensedMl only grows
// when liquid is detected (sensor HIGH).
// If air persists > AIR_STOP_MS, then we actually stop (empty bottle).

#define AIR_STOP_MS  5000  // continuous air = bottle empty, stop pump

static volatile bool air_active[MAX_PUMPS] = {};       // currently seeing air
static volatile int64_t air_start[MAX_PUMPS] = {};     // when continuous air started
static volatile int64_t air_pause_total[MAX_PUMPS] = {};  // accumulated air time (ms)
static volatile int64_t air_pause_start[MAX_PUMPS] = {};  // current pause start

static void IRAM_ATTR air_isr(void* arg) {
    int pump = (int)(intptr_t)arg;
    if (!air_active[pump]) {
        air_active[pump] = true;
        int64_t now = esp_timer_get_time() / 1000;
        air_start[pump] = now;
        air_pause_start[pump] = now;
    }
}

void pumps_init() {
    // Install GPIO ISR service (shared for all pins)
    gpio_install_isr_service(0);

    for (int i = 0; i < MAX_PUMPS; i++) {
        // Pump output
        gpio_reset_pin((gpio_num_t)PUMP_PINS[i]);
        gpio_set_direction((gpio_num_t)PUMP_PINS[i], GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)PUMP_PINS[i], 0);

        // Air sensor input with interrupt
        gpio_reset_pin((gpio_num_t)SENSOR_PINS[i]);
        gpio_set_direction((gpio_num_t)SENSOR_PINS[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode((gpio_num_t)SENSOR_PINS[i], GPIO_PULLUP_ONLY);
        gpio_set_intr_type((gpio_num_t)SENSOR_PINS[i], GPIO_INTR_NEGEDGE);
        gpio_isr_handler_add((gpio_num_t)SENSOR_PINS[i], air_isr, (void*)(intptr_t)i);
        // Disable interrupt until pump starts
        gpio_intr_disable((gpio_num_t)SENSOR_PINS[i]);

        tasks[i].state = PUMP_IDLE;
    }
    pumps_loadCalibration();
    ESP_LOGI(TAG, "Pumps initialized (ISR air detection)");
}

void pumps_loadCalibration() {
    nvs_handle_t h;
    if (nvs_open("pumps", NVS_READONLY, &h) == ESP_OK) {
        for (int i = 0; i < MAX_PUMPS; i++) {
            char key[8];
            snprintf(key, sizeof(key), "rate%d", i);
            int32_t val;
            if (nvs_get_i32(h, key, &val) == ESP_OK)
                pumpRate[i] = val / 1000.0f;
        }
        nvs_close(h);
    }
}

void pumps_saveCalibration() {
    nvs_handle_t h;
    if (nvs_open("pumps", NVS_READWRITE, &h) == ESP_OK) {
        for (int i = 0; i < MAX_PUMPS; i++) {
            char key[8];
            snprintf(key, sizeof(key), "rate%d", i);
            nvs_set_i32(h, key, (int32_t)(pumpRate[i] * 1000));
        }
        nvs_commit(h);
        nvs_close(h);
    }
}

float pumps_getRate(int pump) { return (pump >= 0 && pump < MAX_PUMPS) ? pumpRate[pump] : 0; }
void pumps_setRate(int pump, float r) { if (pump >= 0 && pump < MAX_PUMPS) pumpRate[pump] = r; }

void pumps_startTask(int pump, uint16_t ml) {
    if (pump < 0 || pump >= MAX_PUMPS || ml == 0) return;
    tasks[pump].targetMl = ml;
    tasks[pump].dispensedMl = 0;
    tasks[pump].state = PUMP_RUNNING;
    tasks[pump].startTime = millis_idf();
    air_active[pump] = false;
    air_start[pump] = 0;
    air_pause_total[pump] = 0;
    air_pause_start[pump] = 0;
    gpio_set_level((gpio_num_t)PUMP_PINS[pump], 1);
    gpio_intr_enable((gpio_num_t)SENSOR_PINS[pump]);
}

void pumps_stop(int pump) {
    if (pump < 0 || pump >= MAX_PUMPS) return;
    gpio_intr_disable((gpio_num_t)SENSOR_PINS[pump]);
    gpio_set_level((gpio_num_t)PUMP_PINS[pump], 0);
    if (tasks[pump].state == PUMP_RUNNING)
        tasks[pump].state = PUMP_IDLE;
}

void pumps_stopAll() {
    for (int i = 0; i < MAX_PUMPS; i++) pumps_stop(i);
}

void pumps_resume(int pump) {
    if (pump < 0 || pump >= MAX_PUMPS) return;
    if (tasks[pump].state == PUMP_AIR) {
        tasks[pump].state = PUMP_RUNNING;
        tasks[pump].startTime = millis_idf();
        float remaining = tasks[pump].targetMl - tasks[pump].dispensedMl;
        if (remaining <= 0) { tasks[pump].state = PUMP_DONE; return; }
        gpio_set_level((gpio_num_t)PUMP_PINS[pump], 1);
        gpio_intr_enable((gpio_num_t)SENSOR_PINS[pump]);
    }
}

void pumps_update() {
    int64_t now = millis_idf();
    for (int i = 0; i < MAX_PUMPS; i++) {
        if (tasks[i].state != PUMP_RUNNING) continue;

        // Track air state transitions
        if (air_active[i]) {
            if (gpio_get_level((gpio_num_t)SENSOR_PINS[i]) != 0) {
                // Liquid returned — end air pause
                air_pause_total[i] += now - air_pause_start[i];
                air_active[i] = false;
                air_start[i] = 0;
            } else if (now - air_start[i] >= AIR_STOP_MS) {
                // Air too long — bottle empty, stop pump
                gpio_intr_disable((gpio_num_t)SENSOR_PINS[i]);
                gpio_set_level((gpio_num_t)PUMP_PINS[i], 0);
                tasks[i].state = PUMP_AIR;
                ESP_LOGW(TAG, "BOTTLE EMPTY pump %d (air > %dms)", i, AIR_STOP_MS);
                continue;
            }
            // Air active but not yet timeout — pump keeps running, no counting
        }

        // Volume = (total elapsed - total air pause time) * rate
        int64_t active_ms = (now - tasks[i].startTime) - air_pause_total[i];
        if (air_active[i]) {
            // Currently in air — subtract ongoing pause too
            active_ms -= (now - air_pause_start[i]);
        }
        if (active_ms < 0) active_ms = 0;
        tasks[i].dispensedMl = (active_ms / 1000.0f) * pumpRate[i];

        // Done check
        if (tasks[i].dispensedMl >= tasks[i].targetMl) {
            gpio_intr_disable((gpio_num_t)SENSOR_PINS[i]);
            gpio_set_level((gpio_num_t)PUMP_PINS[i], 0);
            tasks[i].dispensedMl = tasks[i].targetMl;
            tasks[i].state = PUMP_DONE;
        }
    }
}

PumpState pumps_getState(int pump) { return (pump >= 0 && pump < MAX_PUMPS) ? tasks[pump].state : PUMP_IDLE; }
float pumps_getDispensed(int pump) { return (pump >= 0 && pump < MAX_PUMPS) ? tasks[pump].dispensedMl : 0; }
uint16_t pumps_getTarget(int pump) { return (pump >= 0 && pump < MAX_PUMPS) ? tasks[pump].targetMl : 0; }

bool pumps_allDone() {
    for (int i = 0; i < MAX_PUMPS; i++)
        if (tasks[i].state == PUMP_RUNNING) return false;
    for (int i = 0; i < MAX_PUMPS; i++)
        if (tasks[i].state == PUMP_DONE) return true;
    return false;
}

int pumps_airDetectedPump() {
    for (int i = 0; i < MAX_PUMPS; i++)
        if (tasks[i].state == PUMP_AIR) return i;
    return -1;
}
