#include "ball_position_controller.h"

#include <stddef.h>

static float BallPositionController_Abs(float value)
{
    return value < 0.0f ? -value : value;
}

static float BallPositionController_Clamp(float value, float limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static int32_t BallPositionController_Round(float value)
{
    return value >= 0.0f ? (int32_t) (value + 0.5f) :
        (int32_t) (value - 0.5f);
}

bool BallPositionController_Init(ball_position_controller_t *controller,
    const ball_position_controller_config_t *config)
{
    if (controller == NULL || config == NULL ||
        config->kp_millidegrees_per_mm <= 0.0f ||
        config->kd_millidegrees_per_mm_s < 0.0f ||
        config->maximum_output_millidegrees <= 0.0f ||
        config->maximum_slew_millidegrees_per_s <= 0.0f) {
        return false;
    }
    controller->config = *config;
    controller->initialized = 1U;
    controller->reserved[0] = 0U;
    controller->reserved[1] = 0U;
    BallPositionController_Reset(controller);
    return true;
}

void BallPositionController_Reset(ball_position_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    controller->output_millidegrees = 0.0f;
    controller->proportional_millidegrees = 0.0f;
    controller->damping_millidegrees = 0.0f;
    controller->error_mm = 0.0f;
    controller->update_count = 0U;
    controller->saturated = 0U;
}

int32_t BallPositionController_Update(ball_position_controller_t *controller,
    float target_mm, float position_mm, float velocity_mm_s, float dt_s)
{
    float requested;
    float limited;
    float maximum_delta;
    float delta;

    if (controller == NULL || controller->initialized == 0U ||
        dt_s <= 0.0f || dt_s > 0.25f) {
        return 0;
    }
    controller->error_mm = target_mm - position_mm;
    controller->proportional_millidegrees =
        controller->config.kp_millidegrees_per_mm * controller->error_mm;
    controller->damping_millidegrees =
        -controller->config.kd_millidegrees_per_mm_s * velocity_mm_s;
    requested = controller->proportional_millidegrees +
        controller->damping_millidegrees;
    limited = BallPositionController_Clamp(requested,
        controller->config.maximum_output_millidegrees);
    controller->saturated =
        BallPositionController_Abs(requested - limited) > 0.01f ? 1U : 0U;

    maximum_delta =
        controller->config.maximum_slew_millidegrees_per_s * dt_s;
    delta = limited - controller->output_millidegrees;
    delta = BallPositionController_Clamp(delta, maximum_delta);
    controller->output_millidegrees += delta;
    controller->update_count++;
    return BallPositionController_Round(controller->output_millidegrees);
}
