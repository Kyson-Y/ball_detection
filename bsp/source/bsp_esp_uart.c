#include "bsp_esp_uart.h"

#include <stddef.h>
#include <string.h>

#include "cmsis_compiler.h"
#include "ti_msp_dl_config.h"

#if ESP_LINK_UART_BAUD_RATE != 230400
#error "ESP link UART must run at 230400 baud."
#endif

#if ESP_LINK_UART_RX_DMA_CHAN_ID != 1
#error "ESP link UART RX must use physical DMA channel 1."
#endif

#if ESP_LINK_UART_TX_DMA_CHAN_ID != 2
#error "ESP link UART TX must use physical DMA channel 2."
#endif

static uint8_t s_rx_dma_buffer[BSP_ESP_UART_RX_CAPACITY_BYTES];
static uint32_t s_rx_produced_bytes;
static uint32_t s_rx_consumed_bytes;
static uint16_t s_rx_dma_observed_remaining;
static uint8_t s_tx_buffer[BSP_ESP_UART_TX_CAPACITY_BYTES];

volatile bsp_esp_uart_diagnostics_t g_bsp_esp_uart_diag;

static void BSP_EspUart_StartRxDma(void)
{
    if (DL_DMA_isChannelEnabled(DMA, ESP_LINK_UART_RX_DMA_CHAN_ID)) {
        DL_DMA_disableChannel(DMA, ESP_LINK_UART_RX_DMA_CHAN_ID);
        g_bsp_esp_uart_diag.rx_dma_stale_disable_count++;
    }

    DL_DMA_setSrcAddr(DMA, ESP_LINK_UART_RX_DMA_CHAN_ID,
        (uint32_t) &ESP_LINK_UART_INST->RXDATA);
    DL_DMA_setDestAddr(DMA, ESP_LINK_UART_RX_DMA_CHAN_ID,
        (uint32_t) &s_rx_dma_buffer[0]);
    DL_DMA_setTransferSize(DMA, ESP_LINK_UART_RX_DMA_CHAN_ID,
        BSP_ESP_UART_RX_CAPACITY_BYTES);
    __DMB();
    DL_DMA_enableChannel(DMA, ESP_LINK_UART_RX_DMA_CHAN_ID);
    g_bsp_esp_uart_diag.rx_dma_active = 1U;
}

static uint32_t BSP_EspUart_RxProducedBytes(void)
{
    uint16_t remaining;
    uint16_t previous_remaining = s_rx_dma_observed_remaining;
    uint16_t transferred;

    remaining = DL_DMA_getTransferSize(
        DMA, ESP_LINK_UART_RX_DMA_CHAN_ID);
    if (remaining > BSP_ESP_UART_RX_CAPACITY_BYTES) {
        remaining = BSP_ESP_UART_RX_CAPACITY_BYTES;
    }
    if (remaining <= previous_remaining) {
        transferred = (uint16_t) (previous_remaining - remaining);
    } else {
        transferred = (uint16_t) (previous_remaining +
            BSP_ESP_UART_RX_CAPACITY_BYTES - remaining);
    }
    s_rx_dma_observed_remaining = remaining;
    s_rx_produced_bytes += transferred;
    return s_rx_produced_bytes;
}

void BSP_EspUart_Init(void)
{
    s_rx_produced_bytes = 0U;
    s_rx_consumed_bytes = 0U;
    s_rx_dma_observed_remaining = BSP_ESP_UART_RX_CAPACITY_BYTES;
    memset((void *) &g_bsp_esp_uart_diag, 0,
        sizeof(g_bsp_esp_uart_diag));
    g_bsp_esp_uart_diag.tx_line_idle = 1U;
    g_bsp_esp_uart_diag.initialized = 1U;

    DL_DMA_disableChannel(DMA, ESP_LINK_UART_RX_DMA_CHAN_ID);
    DL_DMA_disableChannel(DMA, ESP_LINK_UART_TX_DMA_CHAN_ID);
    DL_UART_Main_clearInterruptStatus(
        ESP_LINK_UART_INST,
        DL_UART_MAIN_INTERRUPT_DMA_DONE_RX |
            DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
            DL_UART_MAIN_INTERRUPT_EOT_DONE);
    DL_UART_Main_enableInterrupt(
        ESP_LINK_UART_INST,
        DL_UART_MAIN_INTERRUPT_DMA_DONE_RX |
            DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
            DL_UART_MAIN_INTERRUPT_EOT_DONE);
    NVIC_ClearPendingIRQ(ESP_LINK_UART_INST_INT_IRQN);
    NVIC_SetPriority(ESP_LINK_UART_INST_INT_IRQN, 1U);
    NVIC_EnableIRQ(ESP_LINK_UART_INST_INT_IRQN);
    BSP_EspUart_StartRxDma();
}

bool BSP_EspUart_TryRead(uint8_t *byte)
{
    uint32_t produced;
    uint32_t used;

    if ((byte == NULL) || (g_bsp_esp_uart_diag.initialized == 0U)) {
        return false;
    }

    produced = BSP_EspUart_RxProducedBytes();
    used = produced - s_rx_consumed_bytes;
    if (used > BSP_ESP_UART_RX_CAPACITY_BYTES) {
        uint32_t dropped = used - BSP_ESP_UART_RX_CAPACITY_BYTES;

        g_bsp_esp_uart_diag.rx_overflow_count += dropped;
        s_rx_consumed_bytes += dropped;
        used = BSP_ESP_UART_RX_CAPACITY_BYTES;
    }
    if (used == 0U) {
        return false;
    }
    if (used > g_bsp_esp_uart_diag.rx_high_water_bytes) {
        g_bsp_esp_uart_diag.rx_high_water_bytes = (uint16_t) used;
    }

    *byte = s_rx_dma_buffer[
        s_rx_consumed_bytes % BSP_ESP_UART_RX_CAPACITY_BYTES];
    __DMB();
    s_rx_consumed_bytes++;
    g_bsp_esp_uart_diag.rx_byte_count++;
    return true;
}

bool BSP_EspUart_TryWrite(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U) ||
        (length > BSP_ESP_UART_TX_CAPACITY_BYTES) ||
        (g_bsp_esp_uart_diag.initialized == 0U)) {
        g_bsp_esp_uart_diag.tx_rejected_argument_count++;
        return false;
    }
    if (g_bsp_esp_uart_diag.tx_busy != 0U) {
        g_bsp_esp_uart_diag.tx_rejected_busy_count++;
        return false;
    }

    memcpy(s_tx_buffer, data, length);
    if (DL_DMA_isChannelEnabled(DMA, ESP_LINK_UART_TX_DMA_CHAN_ID)) {
        DL_DMA_disableChannel(DMA, ESP_LINK_UART_TX_DMA_CHAN_ID);
        g_bsp_esp_uart_diag.tx_dma_stale_disable_count++;
    }

    DL_UART_Main_clearInterruptStatus(ESP_LINK_UART_INST,
        DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
            DL_UART_MAIN_INTERRUPT_EOT_DONE);
    DL_DMA_setSrcAddr(DMA, ESP_LINK_UART_TX_DMA_CHAN_ID,
        (uint32_t) &s_tx_buffer[0]);
    DL_DMA_setDestAddr(DMA, ESP_LINK_UART_TX_DMA_CHAN_ID,
        (uint32_t) &ESP_LINK_UART_INST->TXDATA);
    DL_DMA_setTransferSize(DMA, ESP_LINK_UART_TX_DMA_CHAN_ID, length);

    g_bsp_esp_uart_diag.tx_active_length = length;
    g_bsp_esp_uart_diag.tx_busy = 1U;
    g_bsp_esp_uart_diag.tx_line_idle = 0U;
    g_bsp_esp_uart_diag.tx_request_count++;
    __DMB();
    DL_DMA_enableChannel(DMA, ESP_LINK_UART_TX_DMA_CHAN_ID);
    return true;
}

void BSP_EspUart_ServiceTx(void)
{
    /* TX advances entirely from the UART2 DMA trigger. */
}

const volatile bsp_esp_uart_diagnostics_t *BSP_EspUart_GetDiagnostics(void)
{
    return &g_bsp_esp_uart_diag;
}

void ESP_LINK_UART_INST_IRQHandler(void)
{
    g_bsp_esp_uart_diag.irq_entry_count++;
    for (;;) {
        DL_UART_IIDX iidx =
            DL_UART_Main_getPendingInterrupt(ESP_LINK_UART_INST);

        if (iidx == DL_UART_MAIN_IIDX_NO_INTERRUPT) {
            break;
        }
        g_bsp_esp_uart_diag.last_iidx = (uint32_t) iidx;
        if (iidx == DL_UART_MAIN_IIDX_DMA_DONE_RX) {
            g_bsp_esp_uart_diag.rx_dma_done_count++;
            g_bsp_esp_uart_diag.rx_dma_active =
                DL_DMA_isChannelEnabled(
                    DMA, ESP_LINK_UART_RX_DMA_CHAN_ID) ? 1U : 0U;
            if (g_bsp_esp_uart_diag.rx_dma_active == 0U) {
                BSP_EspUart_StartRxDma();
            }
            g_bsp_esp_uart_diag.rx_dma_restart_count++;
        } else if (iidx == DL_UART_MAIN_IIDX_DMA_DONE_TX) {
            if (g_bsp_esp_uart_diag.tx_busy == 0U) {
                g_bsp_esp_uart_diag.unexpected_iidx_count++;
                continue;
            }
            g_bsp_esp_uart_diag.tx_byte_count +=
                g_bsp_esp_uart_diag.tx_active_length;
            g_bsp_esp_uart_diag.tx_active_length = 0U;
            g_bsp_esp_uart_diag.tx_busy = 0U;
            g_bsp_esp_uart_diag.tx_dma_done_count++;
            g_bsp_esp_uart_diag.tx_complete_count++;
            __DMB();
        } else if (iidx == DL_UART_MAIN_IIDX_EOT_DONE) {
            g_bsp_esp_uart_diag.tx_line_idle = 1U;
            g_bsp_esp_uart_diag.tx_eot_count++;
        } else {
            g_bsp_esp_uart_diag.unexpected_iidx_count++;
        }
    }
}
