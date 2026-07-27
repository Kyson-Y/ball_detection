#ifndef ECHO_DISTANCE_CONTROLLER_H
#define ECHO_DISTANCE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float wheel_circumference_mm;
    uint32_t left_counts_per_revolution;
    uint32_t right_counts_per_revolution;
    int8_t left_encoder_count_sign;
    int8_t right_encoder_count_sign;
    float departure_minimum_rpm;
    float departure_gain_rpm_per_mm;
    float approach_minimum_rpm;
    float approach_gain_rpm_per_mm;
} distance_controller_config_t;

typedef struct {
    distance_controller_config_t config;
    float requested_speed_rpm;
    float target_distance_mm;
    float left_distance_mm;
    float right_distance_mm;
    float center_distance_mm;
    float remaining_distance_mm;
    uint32_t update_count;
    uint8_t initialized;
    uint8_t active;
    uint8_t reached;
} distance_controller_t;

void DistanceController_Init(distance_controller_t *controller,
    const distance_controller_config_t *config);
bool DistanceController_Start(distance_controller_t *controller,
    float requested_speed_rpm, float target_distance_mm);
void DistanceController_Stop(distance_controller_t *controller);
bool DistanceController_Update(distance_controller_t *controller,
    int32_t left_delta_counts, int32_t right_delta_counts);
float DistanceController_GetTargetSpeedRpm(
    const distance_controller_t *controller);

#endif
