#include "distance_controller.h"

#include <stddef.h>
#include <string.h>

static float DistanceController_Abs(float value)
{
    return (value < 0.0f) ? -value : value;
}

void DistanceController_Init(distance_controller_t *controller,
    const distance_controller_config_t *config)
{
    if (controller == NULL || config == NULL) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    controller->config = *config;
    if (config->wheel_circumference_mm > 0.0f &&
        config->left_counts_per_revolution != 0U &&
        config->right_counts_per_revolution != 0U &&
        (config->left_encoder_count_sign == 1 ||
         config->left_encoder_count_sign == -1) &&
        (config->right_encoder_count_sign == 1 ||
         config->right_encoder_count_sign == -1) &&
        config->departure_minimum_rpm > 0.0f &&
        config->departure_gain_rpm_per_mm > 0.0f &&
        config->approach_minimum_rpm > 0.0f &&
        config->approach_gain_rpm_per_mm > 0.0f) {
        controller->initialized = 1U;
    }
}

bool DistanceController_Start(distance_controller_t *controller,
    float requested_speed_rpm, float target_distance_mm)
{
    if (controller == NULL || controller->initialized == 0U ||
        DistanceController_Abs(requested_speed_rpm) < 0.1f ||
        target_distance_mm <= 0.0f) {
        return false;
    }
    controller->requested_speed_rpm = requested_speed_rpm;
    controller->target_distance_mm = target_distance_mm;
    controller->left_distance_mm = 0.0f;
    controller->right_distance_mm = 0.0f;
    controller->center_distance_mm = 0.0f;
    controller->remaining_distance_mm = target_distance_mm;
    controller->update_count = 0U;
    controller->active = 1U;
    controller->reached = 0U;
    return true;
}

void DistanceController_Stop(distance_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    controller->active = 0U;
}

bool DistanceController_Update(distance_controller_t *controller,
    int32_t left_delta_counts, int32_t right_delta_counts)
{
    float direction;
    float left_delta_mm;
    float right_delta_mm;

    if (controller == NULL || controller->initialized == 0U ||
        controller->active == 0U || controller->reached != 0U) {
        return false;
    }
    direction = (controller->requested_speed_rpm < 0.0f) ? -1.0f : 1.0f;
    left_delta_mm =
        (float) left_delta_counts *
        (float) controller->config.left_encoder_count_sign * direction *
        controller->config.wheel_circumference_mm /
        (float) controller->config.left_counts_per_revolution;
    right_delta_mm =
        (float) right_delta_counts *
        (float) controller->config.right_encoder_count_sign * direction *
        controller->config.wheel_circumference_mm /
        (float) controller->config.right_counts_per_revolution;
    controller->left_distance_mm += left_delta_mm;
    controller->right_distance_mm += right_delta_mm;
    controller->center_distance_mm = 0.5f *
        (controller->left_distance_mm + controller->right_distance_mm);
    if (controller->center_distance_mm < 0.0f) {
        controller->center_distance_mm = 0.0f;
    }
    controller->remaining_distance_mm =
        controller->target_distance_mm - controller->center_distance_mm;
    controller->update_count++;
    if (controller->remaining_distance_mm <= 0.0f) {
        controller->remaining_distance_mm = 0.0f;
        controller->reached = 1U;
        return true;
    }
    return false;
}

float DistanceController_GetTargetSpeedRpm(
    const distance_controller_t *controller)
{
    float requested_magnitude;
    float departure_limit;
    float approach_limit;
    float target_magnitude;

    if (controller == NULL || controller->initialized == 0U ||
        controller->active == 0U || controller->reached != 0U) {
        return 0.0f;
    }
    requested_magnitude = DistanceController_Abs(
        controller->requested_speed_rpm);
    departure_limit = controller->config.departure_minimum_rpm +
        controller->config.departure_gain_rpm_per_mm *
            controller->center_distance_mm;
    approach_limit = controller->config.approach_minimum_rpm +
        controller->config.approach_gain_rpm_per_mm *
            controller->remaining_distance_mm;
    target_magnitude = requested_magnitude;
    if (target_magnitude > departure_limit) {
        target_magnitude = departure_limit;
    }
    if (target_magnitude > approach_limit) {
        target_magnitude = approach_limit;
    }
    return (controller->requested_speed_rpm < 0.0f) ?
        -target_magnitude : target_magnitude;
}
