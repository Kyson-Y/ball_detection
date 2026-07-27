#include "display_task.h"

#include <stddef.h>
#include <string.h>

#include "bsp_i2c.h"
#include "bsp_encoder.h"
#include "bsp_supply_voltage.h"
#include "chassis_actuator.h"
#include "diagnostic_page.h"
#include "imu_service.h"
#include "motor_profile.h"
#include "parameter_service.h"
#include "rtos_diagnostics.h"
#include "serial_tx.h"
#include "ssd1306.h"
#include "system_health.h"
#include "ui_input.h"
#include "vehicle_bringup_config.h"

#define DISPLAY_POWER_UP_DELAY pdMS_TO_TICKS(100U)
#define DISPLAY_ONLINE_PERIOD  pdMS_TO_TICKS(500U)
#define DISPLAY_RETRY_PERIOD   pdMS_TO_TICKS(1000U)
#define DISPLAY_IO_WINDOW_POLL_PERIOD pdMS_TO_TICKS(1U)
#define DISPLAY_IO_WINDOW_WAIT_TICKS  pdMS_TO_TICKS(5U)
#define DISPLAY_IO_WINDOW_RETRY_PERIOD pdMS_TO_TICKS(7U)
#define DISPLAY_IO_WINDOW_MAX_DEFERRALS 8U

volatile display_task_diagnostics_t g_display_task_diag;
volatile uint32_t g_display_debug_refresh_enable = 1U;
volatile uint32_t g_display_debug_force_offline;

static uint32_t s_ui_transaction_id;
static float s_parameter_draft_value;

static uint32_t DisplayTask_NextTransactionId(void)
{
    s_ui_transaction_id++;
    if (s_ui_transaction_id < 0xF1000000UL) {
        s_ui_transaction_id = 0xF1000000UL;
    }
    return s_ui_transaction_id;
}

static void DisplayTask_PreviousPage(void)
{
    if (g_display_task_diag.page_index == 0U) {
        g_display_task_diag.page_index =
            (uint8_t) (DIAGNOSTIC_PAGE_COUNT - 1U);
    } else {
        g_display_task_diag.page_index--;
    }
}

static void DisplayTask_NextPage(void)
{
    g_display_task_diag.page_index++;
    if (g_display_task_diag.page_index >= DIAGNOSTIC_PAGE_COUNT) {
        g_display_task_diag.page_index = 0U;
    }
}

static void DisplayTask_AdjustParameter(bool increase)
{
    const parameter_metadata_t *metadata =
        ParameterService_GetMetadataByIndex(
            g_display_task_diag.parameter_index);

    if (metadata == NULL) {
        return;
    }
    if (increase) {
        s_parameter_draft_value += metadata->step;
        if (s_parameter_draft_value > metadata->maximum_value) {
            s_parameter_draft_value = metadata->maximum_value;
        }
    } else {
        s_parameter_draft_value -= metadata->step;
        if (s_parameter_draft_value < metadata->minimum_value) {
            s_parameter_draft_value = metadata->minimum_value;
        }
    }
}

static void DisplayTask_SubmitParameter(void)
{
    const parameter_metadata_t *metadata =
        ParameterService_GetMetadataByIndex(
            g_display_task_diag.parameter_index);
    parameter_status_t status;

    if (metadata == NULL) {
        return;
    }
    status = ParameterService_StageValue(DisplayTask_NextTransactionId(),
        metadata->id, s_parameter_draft_value, PARAMETER_ORIGIN_OLED);
    g_display_task_diag.parameter_result = (uint8_t) status;
    if (status == PARAMETER_STATUS_STAGED) {
        g_display_task_diag.parameter_stage_count++;
        g_display_task_diag.parameter_mode = PARAMETER_UI_BROWSE;
    } else {
        g_display_task_diag.parameter_reject_count++;
    }
}

static void DisplayTask_SubmitDefaults(void)
{
    parameter_status_t status = ParameterService_StageDefaults(
        DisplayTask_NextTransactionId(), PARAMETER_ORIGIN_OLED);

    g_display_task_diag.parameter_result = (uint8_t) status;
    if (status == PARAMETER_STATUS_STAGED) {
        g_display_task_diag.parameter_stage_count++;
        g_display_task_diag.defaults_confirm_count++;
        g_display_task_diag.parameter_mode = PARAMETER_UI_BROWSE;
    } else {
        g_display_task_diag.parameter_reject_count++;
    }
}

static void DisplayTask_HandleParameterEvent(ui_input_event_t event)
{
    const parameter_metadata_t *metadata;

    if (event.kind == UI_EVENT_TIMEOUT) {
        if (g_display_task_diag.parameter_mode != PARAMETER_UI_BROWSE) {
            g_display_task_diag.parameter_mode = PARAMETER_UI_BROWSE;
            g_display_task_diag.timeout_cancel_count++;
        }
        return;
    }
    if (g_display_task_diag.parameter_mode ==
        PARAMETER_UI_DEFAULT_CONFIRM) {
        if ((event.kind == UI_EVENT_PRESS) &&
            (event.key == UI_KEY_OK)) {
            DisplayTask_SubmitDefaults();
        } else if ((event.kind == UI_EVENT_PRESS) &&
            (event.key == UI_KEY_LEFT)) {
            g_display_task_diag.parameter_mode = PARAMETER_UI_BROWSE;
        }
        return;
    }
    if (g_display_task_diag.parameter_mode == PARAMETER_UI_EDIT) {
        if ((event.kind != UI_EVENT_PRESS) &&
            (event.kind != UI_EVENT_REPEAT)) {
            return;
        }
        if ((event.key == UI_KEY_UP) || (event.key == UI_KEY_RIGHT)) {
            DisplayTask_AdjustParameter(true);
        } else if (event.key == UI_KEY_DOWN) {
            DisplayTask_AdjustParameter(false);
        } else if (event.key == UI_KEY_LEFT) {
            g_display_task_diag.parameter_mode = PARAMETER_UI_BROWSE;
        } else if (event.key == UI_KEY_OK) {
            DisplayTask_SubmitParameter();
        }
        return;
    }

    if ((event.kind == UI_EVENT_LONG_PRESS) &&
        (event.key == UI_KEY_OK)) {
        g_display_task_diag.parameter_mode =
            PARAMETER_UI_DEFAULT_CONFIRM;
        return;
    }
    if ((event.kind != UI_EVENT_PRESS) &&
        (event.kind != UI_EVENT_REPEAT)) {
        return;
    }
    if (event.key == UI_KEY_UP) {
        if (g_display_task_diag.parameter_index == 0U) {
            g_display_task_diag.parameter_index = PARAMETER_COUNT - 1U;
        } else {
            g_display_task_diag.parameter_index--;
        }
    } else if (event.key == UI_KEY_DOWN) {
        g_display_task_diag.parameter_index++;
        if (g_display_task_diag.parameter_index >= PARAMETER_COUNT) {
            g_display_task_diag.parameter_index = 0U;
        }
    } else if (event.key == UI_KEY_LEFT) {
        DisplayTask_PreviousPage();
    } else if (event.key == UI_KEY_RIGHT) {
        DisplayTask_NextPage();
    } else if (event.key == UI_KEY_OK) {
        metadata = ParameterService_GetMetadataByIndex(
            g_display_task_diag.parameter_index);
        if ((metadata != NULL) && ParameterService_GetValue(
                metadata->id, &s_parameter_draft_value)) {
            g_display_task_diag.parameter_mode = PARAMETER_UI_EDIT;
        }
    }
}

static void DisplayTask_HandleEvent(ui_input_event_t event)
{
    if (event.kind == UI_EVENT_NONE) {
        return;
    }

    g_display_task_diag.last_key = (uint8_t) event.key;
    g_display_task_diag.last_event_kind = (uint8_t) event.kind;
    g_display_task_diag.key_event_count++;

    if (g_display_task_diag.page_index == DIAGNOSTIC_PAGE_PARAMETER) {
        DisplayTask_HandleParameterEvent(event);
        return;
    }
    if ((g_display_task_diag.page_index == DIAGNOSTIC_PAGE_OVERVIEW) &&
        (event.kind == UI_EVENT_LONG_PRESS) &&
        (event.key == UI_KEY_OK)) {
        SystemHealth_RequestClearRecoverable();
        g_display_task_diag.health_clear_request_count++;
        return;
    }
    if ((event.kind != UI_EVENT_PRESS) &&
        (event.kind != UI_EVENT_REPEAT)) {
        return;
    }
    if ((event.key == UI_KEY_UP) || (event.key == UI_KEY_LEFT)) {
        DisplayTask_PreviousPage();
    } else if ((event.key == UI_KEY_DOWN) ||
        (event.key == UI_KEY_RIGHT)) {
        DisplayTask_NextPage();
    }
}

static void DisplayTask_Render(void)
{
    diagnostic_page_data_t data;
    const parameter_metadata_t *metadata;

    memset(&data, 0, sizeof(data));
    if (!SystemHealth_GetSnapshot(&data.health)) {
        data.health.level = SYSTEM_HEALTH_UNKNOWN;
    }
    data.page_index = g_display_task_diag.page_index;
    data.parameter_index = g_display_task_diag.parameter_index;
    data.parameter_mode = g_display_task_diag.parameter_mode;
    data.parameter_result = g_display_task_diag.parameter_result;
    data.last_event.key = (ui_key_t) g_display_task_diag.last_key;
    data.last_event.kind =
        (ui_event_kind_t) g_display_task_diag.last_event_kind;
    data.key_event_count = g_display_task_diag.key_event_count;
    data.battery_mv = g_bsp_supply_voltage_diag.battery_mv;
    data.supply_sample_count = g_bsp_supply_voltage_diag.sample_count;
    data.supply_timeout_count =
        g_bsp_supply_voltage_diag.conversion_timeout_count;
    data.supply_valid =
        (g_bsp_supply_voltage_diag.last_conversion_ok != 0U) &&
        (data.battery_mv >= 8000U) && (data.battery_mv <= 18000U);
    data.imu_online = g_imu_service_diag.online;
    data.imu_ready = g_imu_service_diag.ready;
    data.imu_who_am_i = g_imu_service_snapshot.who_am_i;
    data.left_rpm = g_motor_profile_diag.left_output_rpm;
    data.right_rpm = g_motor_profile_diag.right_output_rpm;
    data.left_target_deci_rpm =
        g_chassis_actuator_diag.left_target_deci_rpm;
    data.right_target_deci_rpm =
        g_chassis_actuator_diag.right_target_deci_rpm;
    data.encoder_initialized =
        (g_bsp_encoder_diag.left.initialized != 0U) &&
        (g_bsp_encoder_diag.right.initialized != 0U);
    data.motor_profile_valid = g_motor_profile_diag.selection_valid;
    metadata = ParameterService_GetMetadataByIndex(
        g_display_task_diag.parameter_index);
    data.parameter_metadata = metadata;
    data.parameter_draft_value = s_parameter_draft_value;
    if (metadata != NULL) {
        (void) ParameterService_GetValue(
            metadata->id, &data.parameter_value);
    }

    DiagnosticPage_Render(&data);
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

static TickType_t DisplayTask_GetDeferredDelay(
    uint8_t *consecutive_deferred_count)
{
    (*consecutive_deferred_count)++;
    if (*consecutive_deferred_count >= DISPLAY_IO_WINDOW_MAX_DEFERRALS) {
        *consecutive_deferred_count = 0U;
        g_display_task_diag.io_window_skipped_count++;
        return DISPLAY_ONLINE_PERIOD;
    }
    return DISPLAY_IO_WINDOW_RETRY_PERIOD;
}

void DisplayTask_Init(void)
{
    memset((void *) &g_display_task_diag, 0,
        sizeof(g_display_task_diag));
    g_display_debug_refresh_enable = 1U;
    g_display_debug_force_offline = ECHO_ENABLE_OLED ? 0U : 1U;
    g_display_task_diag.last_i2c_result =
        (uint32_t) BSP_I2C_RESULT_NOT_INITIALIZED;
    g_display_task_diag.last_key = (uint8_t) UI_KEY_NONE;
    g_display_task_diag.last_event_kind = (uint8_t) UI_EVENT_NONE;
    g_display_task_diag.parameter_mode = PARAMETER_UI_BROWSE;
    g_display_task_diag.parameter_result = PARAMETER_STATUS_APPLIED;
    s_ui_transaction_id = 0xF1000000UL;
    s_parameter_draft_value = 0.0f;

    BSP_I2C_Init();
    UiInput_Init();
}

void DisplayTask_Entry(void *context)
{
    uint8_t consecutive_deferred_count = 0U;
    bool refresh_in_progress = false;

    (void) context;
    vTaskDelay(DISPLAY_POWER_UP_DELAY);

    for (;;) {
        ui_input_event_t event = UiInput_PollEvent();
        TickType_t delay_ticks;

        g_display_task_diag.run_count++;
        g_display_task_diag.last_wake_tick = xTaskGetTickCount();
        g_rtos_diag.display_task_run_count++;
        g_rtos_diag.display_task_last_wake_tick =
            g_display_task_diag.last_wake_tick;

        DisplayTask_HandleEvent(event);

        if (g_display_debug_force_offline != 0U) {
            if (Ssd1306_IsOnline()) {
                Ssd1306_MarkOffline();
                g_display_task_diag.forced_offline_count++;
                g_display_task_diag.offline_count++;
            }
            consecutive_deferred_count = 0U;
            g_display_task_diag.online = 0U;
            g_display_task_diag.address = 0U;
            vTaskDelay(DISPLAY_RETRY_PERIOD);
            continue;
        }

        if (!Ssd1306_IsOnline()) {
            bool init_success;

            if (!DisplayTask_TryBeginIoWindow()) {
                delay_ticks = DisplayTask_GetDeferredDelay(
                    &consecutive_deferred_count);
                g_display_task_diag.last_i2c_result =
                    g_bsp_i2c_diag.last_result;
                vTaskDelay(delay_ticks);
                continue;
            }
            consecutive_deferred_count = 0U;
            g_display_task_diag.init_attempt_count++;
            init_success = Ssd1306_Init();
            SerialTx_EndQuietWindow();
            if (init_success) {
                g_display_task_diag.init_success_count++;
                g_display_task_diag.online = 1U;
                g_display_task_diag.address = Ssd1306_GetAddress();
                refresh_in_progress = false;
            } else {
                g_display_task_diag.online = 0U;
                g_display_task_diag.address = 0U;
                g_display_task_diag.offline_count++;
            }
            g_display_task_diag.last_i2c_result =
                g_bsp_i2c_diag.last_result;
            vTaskDelay(init_success ? DISPLAY_ONLINE_PERIOD :
                DISPLAY_RETRY_PERIOD);
            continue;
        }

        if (Ssd1306_IsOnline() && (g_display_debug_refresh_enable != 0U)) {
            ssd1306_refresh_step_result_t refresh_result;

            if (!refresh_in_progress) {
                DisplayTask_Render();
                Ssd1306_BeginRefresh();
                refresh_in_progress = true;
            }
            if (!DisplayTask_TryBeginIoWindow()) {
                delay_ticks = DisplayTask_GetDeferredDelay(
                    &consecutive_deferred_count);
                g_display_task_diag.last_i2c_result =
                    g_bsp_i2c_diag.last_result;
                vTaskDelay(delay_ticks);
                continue;
            }
            refresh_result = Ssd1306_RefreshStep();
            SerialTx_EndQuietWindow();
            consecutive_deferred_count = 0U;
            if (refresh_result == SSD1306_REFRESH_STEP_COMPLETE) {
                refresh_in_progress = false;
                g_display_task_diag.refresh_success_count++;
                g_display_task_diag.online = 1U;
                delay_ticks = DISPLAY_ONLINE_PERIOD;
            } else if (refresh_result == SSD1306_REFRESH_STEP_FAILED) {
                refresh_in_progress = false;
                g_display_task_diag.refresh_fail_count++;
                g_display_task_diag.online = 0U;
                g_display_task_diag.address = 0U;
                g_display_task_diag.offline_count++;
                delay_ticks = DISPLAY_RETRY_PERIOD;
            } else {
                delay_ticks = DISPLAY_IO_WINDOW_POLL_PERIOD;
            }
        } else if (Ssd1306_IsOnline()) {
            consecutive_deferred_count = 0U;
            g_display_task_diag.online = 1U;
            delay_ticks = DISPLAY_ONLINE_PERIOD;
        } else {
            delay_ticks = DISPLAY_RETRY_PERIOD;
        }

        g_display_task_diag.last_i2c_result = g_bsp_i2c_diag.last_result;
        vTaskDelay(delay_ticks);
    }
}
