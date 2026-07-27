#ifndef ECHO_ESP_UART_LINK_TEST_H
#define ECHO_ESP_UART_LINK_TEST_H

#include <stdint.h>

#define ESP_UART_LINK_TEST_FRAME_BYTES 16U

typedef struct {
    uint32_t tx_frame_count;
    uint32_t ack_frame_count;
    uint32_t timeout_count;
    uint32_t crc_error_count;
    uint32_t format_error_count;
    uint32_t unexpected_sequence_count;
    uint32_t tx_busy_count;
    uint32_t last_sequence;
    uint32_t last_rtt_us;
    uint32_t minimum_rtt_us;
    uint32_t average_rtt_us;
    uint32_t maximum_rtt_us;
    uint32_t rtt_sum_us;
    uint32_t parser_frame_count;
    uint8_t link_online;
    uint8_t outstanding;
    uint8_t initialized;
    uint8_t reserved;
} esp_uart_link_test_snapshot_t;

extern volatile esp_uart_link_test_snapshot_t g_esp_uart_link_test;

void EspUartLinkTest_Init(void);
void EspUartLinkTest_Process(uint32_t now_us);
void EspUartLinkTest_GetSnapshot(esp_uart_link_test_snapshot_t *snapshot);

#endif
