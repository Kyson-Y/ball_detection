#include "display_task.h"

#include <stddef.h>
#include <string.h>

#include "bsp_encoder.h"
#include "bsp_esp_uart.h"
#include "bsp_i2c.h"
#include "bsp_reflectance.h"
#include "bsp_supply_voltage.h"
#include "bsp_time.h"
#include "attitude_estimator.h"
#include "competition_page.h"
#include "competition_service.h"
#include "competition_storage.h"
#include "chassis_actuator.h"
#include "esp_uart_link_test.h"
#include "imu_service.h"
#include "motor_profile.h"
#include "parameter_service.h"
#include "rtos_diagnostics.h"
#include "serial_tx.h"
#include "ssd1306.h"
#include "system_health.h"
#include "task.h"
#include "ti_msp_dl_config.h"
#include "ui_input.h"
#include "vehicle_bringup_config.h"
#include "zdt_stepper.h"

#define DISPLAY_POWER_UP_DELAY pdMS_TO_TICKS(100U)
#define DISPLAY_ONLINE_PERIOD  pdMS_TO_TICKS(50U)
#define DISPLAY_RETRY_PERIOD   pdMS_TO_TICKS(250U)
#define DISPLAY_IO_WINDOW_POLL_PERIOD pdMS_TO_TICKS(1U)
#define DISPLAY_IO_WINDOW_WAIT_TICKS  pdMS_TO_TICKS(5U)
#define DISPLAY_IO_WINDOW_RETRY_PERIOD pdMS_TO_TICKS(7U)
#define DISPLAY_IO_WINDOW_MAX_DEFERRALS 8U

volatile display_task_diagnostics_t g_display_task_diag;
volatile uint32_t g_display_debug_refresh_enable = 1U;
volatile uint32_t g_display_debug_force_offline;

static uint8_t s_consecutive_deferred_count;

static TickType_t DisplayTask_ActivePeriod(void)
{
    return (g_competition_service.state ==
        (uint8_t) COMPETITION_STATE_RUNNING ||
        g_competition_service.state ==
        (uint8_t) COMPETITION_STATE_COUNTDOWN) ?
        pdMS_TO_TICKS(20U) : DISPLAY_ONLINE_PERIOD;
}

static bool DisplayTask_TryBeginIoWindow(void)
{
    TickType_t start_tick = xTaskGetTickCount();

    for (;;) {
        if (SerialTx_TryBeginPriorityQuietWindow()) {
            g_display_task_diag.io_window_acquired_count++;
            return true;
        }
        if ((TickType_t) (xTaskGetTickCount() - start_tick) >=
            DISPLAY_IO_WINDOW_WAIT_TICKS) {
            g_display_task_diag.io_window_deferred_count++;
            return false;
        }
        vTaskDelay(DISPLAY_IO_WINDOW_POLL_PERIOD);
    }
}

static TickType_t DisplayTask_GetDeferredDelay(void)
{
    s_consecutive_deferred_count++;
    if (s_consecutive_deferred_count >= DISPLAY_IO_WINDOW_MAX_DEFERRALS) {
        s_consecutive_deferred_count = 0U;
        g_display_task_diag.io_window_skipped_count++;
        return DISPLAY_ONLINE_PERIOD;
    }
    return DISPLAY_IO_WINDOW_RETRY_PERIOD;
}

static void DisplayTask_Render(void)
{
    competition_page_data_t data;
    competition_service_snapshot_t competition;
    esp_uart_link_test_snapshot_t esp_link;
    system_health_snapshot_t health;
    const parameter_metadata_t *metadata;

    memset(&data, 0, sizeof(data));
    CompetitionService_GetSnapshot(&competition);
    data.competition = competition;
    if (!SystemHealth_GetSnapshot(&health)) {
        memset(&health, 0, sizeof(health));
        health.level = SYSTEM_HEALTH_UNKNOWN;
    }
    data.health = health;
    data.battery_mv = g_bsp_supply_voltage_diag.battery_mv;
    data.supply_valid =
        (g_bsp_supply_voltage_diag.last_conversion_ok != 0U) &&
        data.battery_mv >= 8000U && data.battery_mv <= 18000U;
    data.uptime_s = health.uptime_ticks / configTICK_RATE_HZ;
    data.yaw_deg = g_attitude_estimator_snapshot.yaw_deg;
    data.temperature_c = g_imu_service_snapshot.temperature_c;
    data.left_rpm = g_motor_profile_diag.left_output_rpm;
    data.right_rpm = g_motor_profile_diag.right_output_rpm;
    data.distance_progress_mm = g_chassis_actuator_diag.distance_progress_mm;
    data.imu_ready = g_imu_service_diag.ready;
    data.encoders_ready =
        g_bsp_encoder_diag.left.initialized != 0U &&
        g_bsp_encoder_diag.right.initialized != 0U;
    data.reflectance_mask = g_bsp_reflectance_diag.valid_channel_mask;
    data.esp_ready = g_bsp_esp_uart_diag.initialized != 0U &&
        g_esp_uart_link_test.link_online != 0U;
    data.esp_rtt_us = g_esp_uart_link_test.average_rtt_us;
    data.lidar_online = 0U;
    data.zdt_gen1_online = g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN1].online;
    data.zdt_gen2_online = g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2].online;
    metadata = ParameterService_GetMetadataByIndex(
        competition.advanced_parameter_index);
    data.advanced_metadata = metadata;
    if (metadata != NULL) {
        (void) ParameterService_GetValue(metadata->id, &data.advanced_value);
    }
    CompetitionPage_Render(&data);
}

void DisplayTask_Init(void)
{
    memset((void *) &g_display_task_diag, 0,
        sizeof(g_display_task_diag));
    g_display_debug_refresh_enable = 1U;
    g_display_debug_force_offline = ECHO_ENABLE_OLED ? 0U : 1U;
    g_display_task_diag.last_i2c_result =
        (uint32_t) BSP_I2C_RESULT_NOT_INITIALIZED;
    BSP_I2C_Init();
    UiInput_Init();
}

void DisplayTask_Entry(void *context)
{
    bool refresh_in_progress = false;

    (void) context;
    vTaskDelay(DISPLAY_POWER_UP_DELAY);
    for (;;) {
        ui_input_event_t event = UiInput_PollEvent();
        TickType_t delay_ticks;
        uint32_t now_ms = (uint32_t) xTaskGetTickCount() *
            (uint32_t) portTICK_PERIOD_MS;

        g_display_task_diag.run_count++;
        g_display_task_diag.last_wake_tick = xTaskGetTickCount();
        g_rtos_diag.display_task_run_count++;
        g_rtos_diag.display_task_last_wake_tick =
            g_display_task_diag.last_wake_tick;
        if (event.kind != UI_EVENT_NONE) {
            g_display_task_diag.last_key = (uint8_t) event.key;
            g_display_task_diag.last_event_kind = (uint8_t) event.kind;
            g_display_task_diag.key_event_count++;
        }
        CompetitionService_HandleEvent(event, now_ms);
        CompetitionService_Service(now_ms);
        g_display_task_diag.page_index = g_competition_service.page;

        if (g_display_debug_force_offline != 0U) {
            if (Ssd1306_IsOnline()) {
                Ssd1306_MarkOffline();
                g_display_task_diag.forced_offline_count++;
                g_display_task_diag.offline_count++;
            }
            g_display_task_diag.online = 0U;
            vTaskDelay(DISPLAY_RETRY_PERIOD);
            continue;
        }
        if (!Ssd1306_IsOnline()) {
            bool init_success;

            if (!DisplayTask_TryBeginIoWindow()) {
                vTaskDelay(DisplayTask_GetDeferredDelay());
                continue;
            }
            s_consecutive_deferred_count = 0U;
            g_display_task_diag.init_attempt_count++;
            init_success = Ssd1306_Init();
            SerialTx_EndQuietWindow();
            if (init_success) {
                g_display_task_diag.init_success_count++;
                g_display_task_diag.online = 1U;
                g_display_task_diag.address = Ssd1306_GetAddress();
                refresh_in_progress = false;
            } else {
                g_display_task_diag.offline_count++;
            }
            vTaskDelay(init_success ? DISPLAY_ONLINE_PERIOD :
                DISPLAY_RETRY_PERIOD);
            continue;
        }
        if (g_display_debug_refresh_enable != 0U) {
            ssd1306_refresh_step_result_t refresh_result;

            if (!refresh_in_progress) {
                DisplayTask_Render();
                Ssd1306_BeginRefresh();
                refresh_in_progress = true;
            }
            if (!DisplayTask_TryBeginIoWindow()) {
                vTaskDelay(DisplayTask_GetDeferredDelay());
                continue;
            }
            refresh_result = Ssd1306_RefreshStep();
            SerialTx_EndQuietWindow();
            s_consecutive_deferred_count = 0U;
            if (refresh_result == SSD1306_REFRESH_STEP_COMPLETE) {
                refresh_in_progress = false;
                g_display_task_diag.refresh_success_count++;
                g_display_task_diag.online = 1U;
                delay_ticks = DisplayTask_ActivePeriod();
            } else if (refresh_result == SSD1306_REFRESH_STEP_FAILED) {
                refresh_in_progress = false;
                g_display_task_diag.refresh_fail_count++;
                g_display_task_diag.online = 0U;
                g_display_task_diag.offline_count++;
                delay_ticks = DISPLAY_RETRY_PERIOD;
            } else {
                delay_ticks = DISPLAY_IO_WINDOW_POLL_PERIOD;
            }
        } else {
            delay_ticks = DisplayTask_ActivePeriod();
        }
        g_display_task_diag.last_i2c_result = g_bsp_i2c_diag.last_result;
        vTaskDelay(delay_ticks);
    }
}
