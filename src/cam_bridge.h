#pragma once
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// UART shared with QR scanner (same physical connection to ESP32-CAM)
#define CAM_UART     UART_NUM_1
#define CAM_TX_PIN   17   // S2 TX → CAM U0R (RX)
#define CAM_RX_PIN   34   // S2 RX ← CAM U0T (TX)
#define CAM_GPIO0    38   // CAM GPIO0 (LOW = boot mode)
#define CAM_PWR      40   // BC327 PNP base via 1k: LOW=on, HIGH=off
#define CAM_BUF_SZ   256

static inline void cam_bridge_enter() {
    static const char* TAG = "CAM";

    // Setup UART (may already be running from qr_serial)
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(CAM_UART, CAM_BUF_SZ, CAM_BUF_SZ, 0, NULL, 0);
    uart_param_config(CAM_UART, &cfg);
    uart_set_pin(CAM_UART, CAM_TX_PIN, CAM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Setup GPIO0 (boot mode) and PWR (BC327 PNP via 1k)
    gpio_reset_pin((gpio_num_t)CAM_GPIO0);
    gpio_set_direction((gpio_num_t)CAM_GPIO0, GPIO_MODE_OUTPUT);
    gpio_reset_pin((gpio_num_t)CAM_PWR);
    gpio_set_direction((gpio_num_t)CAM_PWR, GPIO_MODE_OUTPUT);

    // Power cycle into bootloader:
    // 1. GPIO0 = LOW (boot mode select)
    // 2. Power off CAM (BC327 base HIGH = PNP closed)
    // 3. Wait 200ms
    // 4. Power on CAM (BC327 base LOW = PNP open) → boots into download mode
    // 5. Wait 500ms for bootloader ready
    gpio_set_level((gpio_num_t)CAM_GPIO0, 0);
    gpio_set_level((gpio_num_t)CAM_PWR, 1);    // off
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level((gpio_num_t)CAM_PWR, 0);    // on
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Bridge mode — flash CAM via this port");

    // Passthrough: USB CDC ↔ CAM UART (forever, exit by resetting S2)
    uint8_t byte;
    while (1) {
        int c = fgetc(stdin);
        if (c != EOF) {
            byte = (uint8_t)c;
            uart_write_bytes(CAM_UART, &byte, 1);
        }
        int len = 0;
        uart_get_buffered_data_len(CAM_UART, (size_t*)&len);
        while (len-- > 0) {
            if (uart_read_bytes(CAM_UART, &byte, 1, 0) > 0)
                fputc(byte, stdout);
        }
        vTaskDelay(1);
    }
}
