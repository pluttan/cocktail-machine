#include "qr_serial.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>

static const char* TAG = "QR";

#define QR_UART    UART_NUM_1
#define QR_TX_PIN  17
#define QR_RX_PIN  34
#define QR_BUF_SZ  256

static char qr_buf[64];
static int qr_buf_pos = 0;

void qr_init() {
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(QR_UART, QR_BUF_SZ, 0, 0, NULL, 0);
    uart_param_config(QR_UART, &cfg);
    uart_set_pin(QR_UART, QR_TX_PIN, QR_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "QR UART initialized (RX=%d TX=%d)", QR_RX_PIN, QR_TX_PIN);
}

int qr_check() {
    uint8_t byte;
    while (uart_read_bytes(QR_UART, &byte, 1, 0) > 0) {
        if (byte == '\n' || byte == '\r') {
            if (qr_buf_pos > 0) {
                qr_buf[qr_buf_pos] = '\0';
                qr_buf_pos = 0;
                // Parse "QR:N"
                if (strncmp(qr_buf, "QR:", 3) == 0) {
                    int id = atoi(qr_buf + 3);
                    if (id >= 0 && id < 20) {
                        ESP_LOGI(TAG, "QR decoded: %d", id);
                        return id;
                    }
                }
            }
        } else if (qr_buf_pos < 62) {
            qr_buf[qr_buf_pos++] = (char)byte;
        }
    }
    return -1;
}
