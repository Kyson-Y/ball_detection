#include "esp_uart_link_test.h"

#include <stddef.h>
#include <string.h>

#include "bsp_esp_uart.h"

#define ESP_LINK_SYNC_0 0xA5U
#define ESP_LINK_SYNC_1 0x5AU
#define ESP_LINK_VERSION 1U
#define ESP_LINK_TYPE_PING 1U
#define ESP_LINK_TYPE_ACK 2U
#define ESP_LINK_PATTERN_0 0x3CU
#define ESP_LINK_PATTERN_1 0xC3U
#define ESP_LINK_SEND_PERIOD_US 20000U
#define ESP_LINK_RETRY_PERIOD_US 30000U
#define ESP_LINK_MAX_RETRY_COUNT 4U
#define ESP_LINK_ACK_TIMEOUT_US 160000U
#define ESP_LINK_CRC_INIT 0xFFFFU
#define ESP_LINK_CRC_POLY 0x1021U

static uint8_t s_rx_frame[ESP_UART_LINK_TEST_FRAME_BYTES];
static uint8_t s_rx_index;
static uint32_t s_next_sequence;
static uint32_t s_outstanding_sequence;
static uint32_t s_outstanding_timestamp_us;
static uint8_t s_outstanding_frame[ESP_UART_LINK_TEST_FRAME_BYTES];
static uint32_t s_last_attempt_us;
static uint8_t s_retry_count;
static uint32_t s_last_send_us;

volatile esp_uart_link_test_snapshot_t g_esp_uart_link_test;

static void EspUartLinkTest_PutU32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t) value;
    destination[1] = (uint8_t) (value >> 8);
    destination[2] = (uint8_t) (value >> 16);
    destination[3] = (uint8_t) (value >> 24);
}

static uint32_t EspUartLinkTest_GetU32(const uint8_t *source)
{
    return (uint32_t) source[0] |
        ((uint32_t) source[1] << 8) |
        ((uint32_t) source[2] << 16) |
        ((uint32_t) source[3] << 24);
}

static uint16_t EspUartLinkTest_Crc16(
    const uint8_t *data, uint8_t length)
{
    uint16_t crc = ESP_LINK_CRC_INIT;
    uint8_t index;

    for (index = 0U; index < length; index++) {
        uint8_t bit;

        crc ^= (uint16_t) data[index] << 8;
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t) ((crc << 1) ^ ESP_LINK_CRC_POLY);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static void EspUartLinkTest_OnFrame(uint32_t now_us)
{
    uint16_t expected_crc = EspUartLinkTest_Crc16(&s_rx_frame[2], 12U);
    uint16_t received_crc = (uint16_t) s_rx_frame[14] |
        (uint16_t) ((uint16_t) s_rx_frame[15] << 8);
    uint32_t sequence;
    uint32_t timestamp_us;
    uint32_t rtt_us;

    g_esp_uart_link_test.parser_frame_count++;
    if (expected_crc != received_crc) {
        g_esp_uart_link_test.crc_error_count++;
        return;
    }
    if ((s_rx_frame[2] != ESP_LINK_VERSION) ||
        (s_rx_frame[3] != ESP_LINK_TYPE_ACK) ||
        (s_rx_frame[12] != ESP_LINK_PATTERN_0) ||
        (s_rx_frame[13] != ESP_LINK_PATTERN_1)) {
        g_esp_uart_link_test.format_error_count++;
        return;
    }

    sequence = EspUartLinkTest_GetU32(&s_rx_frame[4]);
    timestamp_us = EspUartLinkTest_GetU32(&s_rx_frame[8]);
    if ((g_esp_uart_link_test.outstanding == 0U) ||
        (sequence != s_outstanding_sequence) ||
        (timestamp_us != s_outstanding_timestamp_us)) {
        g_esp_uart_link_test.unexpected_sequence_count++;
        return;
    }

    rtt_us = now_us - timestamp_us;
    g_esp_uart_link_test.ack_frame_count++;
    g_esp_uart_link_test.last_sequence = sequence;
    g_esp_uart_link_test.last_rtt_us = rtt_us;
    if ((g_esp_uart_link_test.ack_frame_count == 1U) ||
        (rtt_us < g_esp_uart_link_test.minimum_rtt_us)) {
        g_esp_uart_link_test.minimum_rtt_us = rtt_us;
    }
    if (rtt_us > g_esp_uart_link_test.maximum_rtt_us) {
        g_esp_uart_link_test.maximum_rtt_us = rtt_us;
    }
    g_esp_uart_link_test.rtt_sum_us += rtt_us;
    g_esp_uart_link_test.average_rtt_us =
        g_esp_uart_link_test.rtt_sum_us /
        g_esp_uart_link_test.ack_frame_count;
    g_esp_uart_link_test.link_online = 1U;
    g_esp_uart_link_test.outstanding = 0U;
}

static void EspUartLinkTest_OnByte(uint8_t byte, uint32_t now_us)
{
    if (s_rx_index == 0U) {
        if (byte == ESP_LINK_SYNC_0) {
            s_rx_frame[s_rx_index++] = byte;
        }
        return;
    }
    if (s_rx_index == 1U) {
        if (byte == ESP_LINK_SYNC_1) {
            s_rx_frame[s_rx_index++] = byte;
        } else if (byte != ESP_LINK_SYNC_0) {
            s_rx_index = 0U;
        }
        return;
    }

    s_rx_frame[s_rx_index++] = byte;
    if (s_rx_index >= ESP_UART_LINK_TEST_FRAME_BYTES) {
        EspUartLinkTest_OnFrame(now_us);
        s_rx_index = 0U;
    }
}

static bool EspUartLinkTest_SendPing(uint32_t now_us)
{
    uint8_t frame[ESP_UART_LINK_TEST_FRAME_BYTES];
    uint16_t crc;
    uint32_t sequence = s_next_sequence++;

    frame[0] = ESP_LINK_SYNC_0;
    frame[1] = ESP_LINK_SYNC_1;
    frame[2] = ESP_LINK_VERSION;
    frame[3] = ESP_LINK_TYPE_PING;
    EspUartLinkTest_PutU32(&frame[4], sequence);
    EspUartLinkTest_PutU32(&frame[8], now_us);
    frame[12] = ESP_LINK_PATTERN_0;
    frame[13] = ESP_LINK_PATTERN_1;
    crc = EspUartLinkTest_Crc16(&frame[2], 12U);
    frame[14] = (uint8_t) crc;
    frame[15] = (uint8_t) (crc >> 8);

    if (!BSP_EspUart_TryWrite(frame, sizeof(frame))) {
        g_esp_uart_link_test.tx_busy_count++;
        return false;
    }

    s_outstanding_sequence = sequence;
    s_outstanding_timestamp_us = now_us;
    memcpy(s_outstanding_frame, frame, sizeof(s_outstanding_frame));
    s_last_attempt_us = now_us;
    s_retry_count = 0U;
    g_esp_uart_link_test.tx_frame_count++;
    g_esp_uart_link_test.outstanding = 1U;
    return true;
}

void EspUartLinkTest_Init(void)
{
    memset((void *) &g_esp_uart_link_test, 0,
        sizeof(g_esp_uart_link_test));
    s_rx_index = 0U;
    s_next_sequence = 0U;
    s_outstanding_sequence = 0U;
    s_outstanding_timestamp_us = 0U;
    memset(s_outstanding_frame, 0, sizeof(s_outstanding_frame));
    s_last_attempt_us = 0U;
    s_retry_count = 0U;
    s_last_send_us = 0U;
    g_esp_uart_link_test.initialized = 1U;
}

void EspUartLinkTest_Process(uint32_t now_us)
{
    uint8_t byte;

    BSP_EspUart_ServiceTx();
    while (BSP_EspUart_TryRead(&byte)) {
        EspUartLinkTest_OnByte(byte, now_us);
    }

    if ((g_esp_uart_link_test.outstanding != 0U) &&
        ((uint32_t) (now_us - s_outstanding_timestamp_us) >=
            ESP_LINK_ACK_TIMEOUT_US)) {
        g_esp_uart_link_test.timeout_count++;
        g_esp_uart_link_test.outstanding = 0U;
        g_esp_uart_link_test.link_online = 0U;
    }

    if ((g_esp_uart_link_test.outstanding != 0U) &&
        (s_retry_count < ESP_LINK_MAX_RETRY_COUNT) &&
        ((uint32_t) (now_us - s_last_attempt_us) >=
            ESP_LINK_RETRY_PERIOD_US)) {
        s_last_attempt_us = now_us;
        if (BSP_EspUart_TryWrite(s_outstanding_frame,
                sizeof(s_outstanding_frame))) {
            s_retry_count++;
        } else {
            g_esp_uart_link_test.tx_busy_count++;
        }
    }

    if ((g_esp_uart_link_test.outstanding == 0U) &&
        ((uint32_t) (now_us - s_last_send_us) >=
            ESP_LINK_SEND_PERIOD_US)) {
        s_last_send_us = now_us;
        (void) EspUartLinkTest_SendPing(now_us);
    }
}

void EspUartLinkTest_GetSnapshot(esp_uart_link_test_snapshot_t *snapshot)
{
    if (snapshot != NULL) {
        *snapshot = g_esp_uart_link_test;
    }
}
