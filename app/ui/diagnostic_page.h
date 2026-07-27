#ifndef ECHO_DIAGNOSTIC_PAGE_H
#define ECHO_DIAGNOSTIC_PAGE_H

#include <stdint.h>

#include "parameter_service.h"
#include "system_health.h"
#include "ui_input.h"

typedef enum {
    DIAGNOSTIC_PAGE_CAR = 0U,
    DIAGNOSTIC_PAGE_OVERVIEW,
    DIAGNOSTIC_PAGE_RTOS,
    DIAGNOSTIC_PAGE_COMM,
    DIAGNOSTIC_PAGE_DEVICE,
    DIAGNOSTIC_PAGE_PARAMETER,
    DIAGNOSTIC_PAGE_COUNT
} diagnostic_page_t;

typedef enum {
    PARAMETER_UI_BROWSE = 0U,
    PARAMETER_UI_EDIT,
    PARAMETER_UI_DEFAULT_CONFIRM
} parameter_ui_mode_t;

typedef struct {
    system_health_snapshot_t health;
    const parameter_metadata_t *parameter_metadata;
    float parameter_value;
    float parameter_draft_value;
    uint32_t key_event_count;
    uint8_t page_index;
    uint8_t parameter_index;
    uint8_t parameter_mode;
    uint8_t parameter_result;
    ui_input_event_t last_event;
    uint32_t battery_mv;
    uint32_t supply_sample_count;
    uint32_t supply_timeout_count;
    float left_rpm;
    float right_rpm;
    int16_t left_target_deci_rpm;
    int16_t right_target_deci_rpm;
    uint8_t supply_valid;
    uint8_t imu_online;
    uint8_t imu_ready;
    uint8_t imu_who_am_i;
    uint8_t encoder_initialized;
    uint8_t motor_profile_valid;
} diagnostic_page_data_t;

void DiagnosticPage_Render(const diagnostic_page_data_t *data);

#endif
