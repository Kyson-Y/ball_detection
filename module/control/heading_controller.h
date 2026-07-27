#ifndef ECHO_HEADING_CONTROLLER_H
#define ECHO_HEADING_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float kp_rpm_per_deg;
    float kd_rpm_per_dps;
    float maximum_correction_rpm;
} heading_controller_config_t;

typedef struct {
    heading_controller_config_t config;
    float base_rpm;
    float target_yaw_deg;
    float current_yaw_deg;
    float error_deg;
    float correction_rpm;
    float left_target_rpm;
    float right_target_rpm;
    uint32_t update_count;
    uint32_t saturation_count;
    uint8_t active;
} heading_controller_t;

void HeadingController_Init(heading_controller_t *controller,
    const heading_controller_config_t *config);
void HeadingController_Start(heading_controller_t *controller,
    float base_rpm, float target_yaw_deg);
void HeadingController_Stop(heading_controller_t *controller);
bool HeadingController_Update(heading_controller_t *controller,
    float current_yaw_deg, float yaw_rate_dps);
float HeadingController_WrapDegrees(float angle_deg);

#endif
