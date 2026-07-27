#include "heading_controller.h"

#include <string.h>

static float HeadingController_Clamp(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

float HeadingController_WrapDegrees(float angle_deg)
{
    while (angle_deg > 180.0f) {
        angle_deg -= 360.0f;
    }
    while (angle_deg < -180.0f) {
        angle_deg += 360.0f;
    }
    return angle_deg;
}

void HeadingController_Init(heading_controller_t *controller,
    const heading_controller_config_t *config)
{
    if ((controller == NULL) || (config == NULL)) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    controller->config = *config;
}

void HeadingController_Start(heading_controller_t *controller,
    float base_rpm, float target_yaw_deg)
{
    if (controller == NULL) {
        return;
    }
    controller->base_rpm = base_rpm;
    controller->target_yaw_deg =
        HeadingController_WrapDegrees(target_yaw_deg);
    controller->active = 1U;
}

void HeadingController_Stop(heading_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    controller->active = 0U;
    controller->correction_rpm = 0.0f;
    controller->left_target_rpm = 0.0f;
    controller->right_target_rpm = 0.0f;
}

bool HeadingController_Update(heading_controller_t *controller,
    float current_yaw_deg, float yaw_rate_dps)
{
    float correction;
    float limited;

    if ((controller == NULL) || (controller->active == 0U)) {
        return false;
    }
    controller->current_yaw_deg =
        HeadingController_WrapDegrees(current_yaw_deg);
    controller->error_deg = HeadingController_WrapDegrees(
        controller->target_yaw_deg - controller->current_yaw_deg);
    correction = controller->config.kp_rpm_per_deg *
        controller->error_deg -
        controller->config.kd_rpm_per_dps * yaw_rate_dps;
    limited = HeadingController_Clamp(correction,
        -controller->config.maximum_correction_rpm,
        controller->config.maximum_correction_rpm);
    if (limited != correction) {
        controller->saturation_count++;
    }
    controller->correction_rpm = limited;
    controller->left_target_rpm = controller->base_rpm - limited;
    controller->right_target_rpm = controller->base_rpm + limited;
    controller->update_count++;
    return true;
}
