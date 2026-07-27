#ifndef ECHO_BSP_ESP_UART_H
#define ECHO_BSP_ESP_UART_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_ESP_UART_RX_CAPACITY_BYTES 512U
#define BSP_ESP_UART_TX_CAPACITY_BYTES 256U

typedef struct {
    uint32_t rx_byte_count;
    uint32_t rx_overflow_count;
    uint32_t rx_dma_done_count;
    uint32_t rx_dma_restart_count;
    uint32_t tx_request_count;
    uint32_t tx_complete_count;
    uint32_t tx_byte_count;
    uint32_t tx_dma_done_count;
    uint32_t tx_eot_count;
    uint32_t tx_rejected_busy_count;
    uint32_t tx_rejected_argument_count;
    uint32_t irq_entry_count;
    uint32_t unexpected_iidx_count;
    uint32_t rx_dma_stale_disable_count;
    uint32_t tx_dma_stale_disable_count;
    uint16_t rx_high_water_bytes;
    uint16_t tx_active_length;
    uint8_t tx_busy;
    uint8_t initialized;
    uint8_t rx_dma_active;
    uint8_t tx_line_idle;
    uint32_t last_iidx;
} bsp_esp_uart_diagnostics_t;

extern volatile bsp_esp_uart_diagnostics_t g_bsp_esp_uart_diag;

void BSP_EspUart_Init(void);
bool BSP_EspUart_TryRead(uint8_t *byte);
bool BSP_EspUart_TryWrite(const uint8_t *data, uint16_t length);
void BSP_EspUart_ServiceTx(void);
const volatile bsp_esp_uart_diagnostics_t *BSP_EspUart_GetDiagnostics(void);

#endif
