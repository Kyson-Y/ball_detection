#include "service_task.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "bsp_led.h"
#include "bsp_reflectance.h"
#include "bsp_supply_voltage.h"
#include "bsp_tfmini_uart.h"
#include "bsp_time.h"
#include "command_service.h"
#include "motor_profile.h"
#include "queue.h"
#include "rtos_diagnostics.h"
#include "rtos_hooks.h"
#include "serial_tx.h"
#include "system_health.h"
#include "task.h"
#include "telemetry.h"
#include "tfmini_s.h"

#define SERVICE_TASK_PERIOD pdMS_TO_TICKS(1U)
#define SERVICE_HEARTBEAT_TIMEOUT pdMS_TO_TICKS(1500U)
#define SERVICE_HEALTH_REFRESH_PERIOD pdMS_TO_TICKS(100U)
#define SERVICE_HEALTH_TELEMETRY_PERIOD pdMS_TO_TICKS(1000U)
#define SERVICE_REFLECTANCE_SCAN_DIVIDER 1U
#define SERVICE_REFLECTANCE_TELEMETRY_DECIMATION 125U
#define SERVICE_SUPPLY_SAMPLE_PERIOD pdMS_TO_TICKS(10U)
#define SERVICE_SUPPLY_TELEMETRY_PERIOD pdMS_TO_TICKS(100U)
#define SERVICE_TFMINI_TELEMETRY_PERIOD pdMS_TO_TICKS(50U)
#define SERVICE_TFMINI_QUERY_DELAY pdMS_TO_TICKS(250U)
#define SERVICE_TFMINI_QUERY_RETRY_PERIOD pdMS_TO_TICKS(500U)
#define SERVICE_TFMINI_QUERY_MAX_ATTEMPTS 3U

void ServiceTask_Entry(void *context)
{
    QueueHandle_t heartbeat_queue = (QueueHandle_t) context;
    TickType_t last_wake_time = xTaskGetTickCount();
    TickType_t last_heartbeat_time = last_wake_time;
    TickType_t last_health_refresh_time = last_wake_time;
    TickType_t last_health_telemetry_time = last_wake_time;
    TickType_t last_supply_sample_time = last_wake_time;
    TickType_t last_supply_telemetry_time = last_wake_time;
    TickType_t last_tfmini_telemetry_time = last_wake_time;
    TickType_t last_tfmini_query_time = last_wake_time;
    TickType_t tfmini_start_time = last_wake_time;
    uint32_t heartbeat_sequence = 0U;
    uint8_t reflectance_scan_divider = 0U;
    uint8_t tfmini_query_attempt_count = 0U;

    configASSERT(heartbeat_queue != NULL);

    for (;;) {
        TickType_t now;
        bsp_reflectance_sample_t reflectance_sample;
        bsp_supply_voltage_sample_t supply_sample;
        uint32_t now_us;
        uint8_t tfmini_byte;

        (void) xTaskDelayUntil(&last_wake_time, SERVICE_TASK_PERIOD);
        now = xTaskGetTickCount();
        g_rtos_diag.service_task_run_count++;
        g_rtos_diag.service_task_last_wake_tick = now;

        CommandService_ProcessRx();
        SerialTx_Service();
        BSP_TfminiUart_ServiceTx();
        now_us = BSP_Time_GetUs();
        while (BSP_TfminiUart_TryRead(&tfmini_byte)) {
            TfminiS_ProcessByte(tfmini_byte, now_us);
        }
        TfminiS_Update(now_us);

        if (!TfminiS_HasFirmwareVersion() &&
            (tfmini_query_attempt_count <
                SERVICE_TFMINI_QUERY_MAX_ATTEMPTS) &&
            (((tfmini_query_attempt_count == 0U) &&
                 ((TickType_t) (now - tfmini_start_time) >=
                     SERVICE_TFMINI_QUERY_DELAY)) ||
                ((tfmini_query_attempt_count != 0U) &&
                 ((TickType_t) (now - last_tfmini_query_time) >=
                     SERVICE_TFMINI_QUERY_RETRY_PERIOD)))) {
            uint8_t version_query[TFMINI_S_VERSION_QUERY_BYTES];
            uint8_t version_query_length =
                TfminiS_BuildFirmwareVersionQuery(version_query);

            if (BSP_TfminiUart_TryWrite(
                    version_query, version_query_length)) {
                tfmini_query_attempt_count++;
                last_tfmini_query_time = now;
            }
        }

        if ((TickType_t) (now - last_tfmini_telemetry_time) >=
            SERVICE_TFMINI_TELEMETRY_PERIOD) {
            telemetry_tfmini_sample_t tfmini_telemetry;
            const volatile bsp_tfmini_uart_diagnostics_t *uart_diag =
                BSP_TfminiUart_GetDiagnostics();

            last_tfmini_telemetry_time = now;
            TfminiS_GetSnapshot(now_us, &tfmini_telemetry.snapshot);
            tfmini_telemetry.uart_rx_overflow_count =
                uart_diag->rx_overflow_count;
            tfmini_telemetry.query_attempt_count =
                tfmini_query_attempt_count;
            tfmini_telemetry.reserved[0] = 0U;
            tfmini_telemetry.reserved[1] = 0U;
            tfmini_telemetry.reserved[2] = 0U;
            (void) Telemetry_PublishTfmini(&tfmini_telemetry);
        }

        reflectance_scan_divider++;
        if (reflectance_scan_divider >=
            SERVICE_REFLECTANCE_SCAN_DIVIDER) {
            reflectance_scan_divider = 0U;
            if (BSP_Reflectance_Service(&reflectance_sample) &&
                (reflectance_sample.scan_sequence %
                    SERVICE_REFLECTANCE_TELEMETRY_DECIMATION) == 0U) {
                (void) Telemetry_PublishReflectance(&reflectance_sample);
            }
        }

        if ((TickType_t) (now - last_supply_sample_time) >=
            SERVICE_SUPPLY_SAMPLE_PERIOD) {
            last_supply_sample_time = now;
            if (BSP_SupplyVoltage_Sample(&supply_sample) &&
                (TickType_t) (now - last_supply_telemetry_time) >=
                    SERVICE_SUPPLY_TELEMETRY_PERIOD) {
                last_supply_telemetry_time = now;
                (void) Telemetry_PublishSupplyVoltage(&supply_sample);
            }
        }

        if (xQueueReceive(
                heartbeat_queue, &heartbeat_sequence, 0U) == pdPASS) {
            last_heartbeat_time = now;
            g_rtos_diag.queue_receive_count++;
            g_rtos_diag.last_heartbeat_sequence = heartbeat_sequence;

            BSP_LED_Toggle();
            g_rtos_diag.led_toggle_count++;
            g_rtos_diag.last_led_toggle_tick = now;
            RtosDiagnostics_Refresh();
        } else if ((TickType_t) (now - last_heartbeat_time) >=
                   SERVICE_HEARTBEAT_TIMEOUT) {
            RtosFault_Halt(RTOS_FAULT_HEARTBEAT_TIMEOUT,
                xTaskGetCurrentTaskHandle(), "Service", 0);
        }

        if ((TickType_t) (now - last_health_refresh_time) >=
            SERVICE_HEALTH_REFRESH_PERIOD) {
            system_health_snapshot_t health;

            last_health_refresh_time = now;
            SystemHealth_ServiceRefresh();
            if ((TickType_t) (now - last_health_telemetry_time) >=
                SERVICE_HEALTH_TELEMETRY_PERIOD) {
                last_health_telemetry_time = now;
                if (SystemHealth_GetSnapshot(&health)) {
                    (void) Telemetry_PublishHealth(&health);
                }
                (void) Telemetry_PublishMotorProfile(
                    MotorProfile_GetActive());
            }
        }
    }
}
