#ifndef ECHO_COMPETITION_PAGE_H
#define ECHO_COMPETITION_PAGE_H

#include <stdint.h>

#include "competition_service.h"
#include "parameter_service.h"
#include "system_health.h"

typedef struct {
    competition_service_snapshot_t competition;
    system_health_snapshot_t health;
    const parameter_metadata_t *advanced_metadata;
    float advanced_value;
    uint32_t battery_mv;
    uint32_t uptime_s;
    uint32_t esp_rtt_us;
    float yaw_deg;
    float temperature_c;
    float left_rpm;
    float right_rpm;
    float distance_progress_mm;
    uint8_t supply_valid;
    uint8_t imu_ready;
    uint8_t encoders_ready;
    uint8_t reflectance_mask;
    uint8_t esp_ready;
    uint8_t lidar_online;
    uint8_t zdt_gen1_online;
    uint8_t zdt_gen2_online;
} competition_page_data_t;

void CompetitionPage_Render(const competition_page_data_t *data);

#endif
