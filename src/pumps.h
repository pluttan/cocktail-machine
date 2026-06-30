#pragma once
#include <cstdint>
#include "recipes.h"

static constexpr int PUMP_PINS[6]   = {1, 2, 3, 4, 6, 8};
static constexpr int SENSOR_PINS[6] = {10, 12, 13, 14, 15, 21};

enum PumpState : uint8_t {
    PUMP_IDLE,
    PUMP_RUNNING,
    PUMP_DONE,
    PUMP_AIR,
};

void pumps_init();
void pumps_update();
void pumps_startTask(int pump, uint16_t ml);
void pumps_stopAll();
void pumps_stop(int pump);
void pumps_resume(int pump);
PumpState pumps_getState(int pump);
float pumps_getDispensed(int pump);
uint16_t pumps_getTarget(int pump);
bool pumps_allDone();
int pumps_airDetectedPump();
float pumps_getRate(int pump);
void pumps_setRate(int pump, float mlPerSec);
void pumps_loadCalibration();
void pumps_saveCalibration();
