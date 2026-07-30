#include <assert.h>

#include "ball_position_controller.h"

int main(void)
{
    const ball_position_controller_config_t config = {
        .position_velocity_gain_per_s = 2.8f,
        .negative_position_velocity_gain_per_s = 2.2f,
        .maximum_velocity_mm_s = 80.0f,
        .search_integral_gain_millidegrees_per_mm_s = 100.0f,
        .maximum_search_rate_millidegrees_per_s = 6000.0f,
        .velocity_noise_deadband_mm_s = 12.0f,
        .motion_detection_velocity_mm_s = 15.0f,
        .motion_detection_displacement_mm = 2.0f,
        .search_velocity_damping_millidegrees_per_mm_s = 0.0f,
        .velocity_feedback_millidegrees_per_mm_s = 100.0f,
        .reversal_braking_millidegrees_per_mm_s = 200.0f,
        .moving_bias_decay_millidegrees_per_s = 1000.0f,
        .hold_bias_decay_millidegrees_per_s = 500.0f,
        .stall_reacquire_position_error_mm = 3.0f,
        .integral_limit_millidegrees = 40000.0f,
        .maximum_output_millidegrees = 40000.0f,
        .maximum_slew_millidegrees_per_s = 50000.0f,
        .stall_reacquire_confirm_samples = 3U
    };
    ball_position_controller_t controller;
    int32_t output;

    assert(BallPositionController_Init(&controller, &config));

    /* Static camera jitter must not enter the velocity feedback path. */
    output = BallPositionController_Update(
        &controller, 50.0f, 0.0f, 10.0f, 0.02f);
    assert(output == 100);
    assert(controller.filtered_velocity_mm_s == 0.0f);
    assert(controller.target_velocity_mm_s == 80.0f);
    assert(controller.motion_detected == 0U);

    /* Positive and negative travel use independent continuous envelopes. */
    BallPositionController_Reset(&controller);
    (void) BallPositionController_Update(
        &controller, 50.0f, 25.0f, 0.0f, 0.02f);
    assert(controller.target_velocity_mm_s == 70.0f);
    (void) BallPositionController_Update(
        &controller, 50.0f, 45.0f, 0.0f, 0.02f);
    assert(controller.target_velocity_mm_s == 14.0f);
    BallPositionController_Reset(&controller);
    (void) BallPositionController_Update(
        &controller, -50.0f, -25.0f, 0.0f, 0.02f);
    assert(controller.target_velocity_mm_s == -55.0f);

    /* Three directional samples plus displacement reject single-frame noise. */
    BallPositionController_Reset(&controller);
    (void) BallPositionController_Update(
        &controller, 50.0f, 0.0f, 0.0f, 0.02f);
    (void) BallPositionController_Update(
        &controller, 50.0f, 2.0f, 20.0f, 0.02f);
    (void) BallPositionController_Update(
        &controller, 50.0f, 3.0f, 20.0f, 0.02f);
    (void) BallPositionController_Update(
        &controller, 50.0f, 4.0f, 20.0f, 0.02f);
    assert(controller.motion_detected != 0U);

    /* A stationary residual error must re-arm learned-angle search. */
    controller.output_millidegrees = 1000.0f;
    (void) BallPositionController_Update(
        &controller, 0.0f, -8.0f, 0.0f, 0.02f);
    (void) BallPositionController_Update(
        &controller, 0.0f, -8.0f, 0.0f, 0.02f);
    (void) BallPositionController_Update(
        &controller, 0.0f, -8.0f, 0.0f, 0.02f);
    assert(controller.motion_detected == 0U);
    assert(controller.search_start_valid != 0U);

    /* Reversal immediately requests braking while preserving output slew. */
    controller.output_millidegrees = 10000.0f;
    BallPositionController_BeginReversal(&controller);
    output = BallPositionController_Update(
        &controller, -50.0f, 50.0f, 60.0f, 0.02f);
    assert(output < 10000);
    assert(controller.proportional_millidegrees < 0.0f);
    assert(controller.integral_millidegrees == 0.0f);
    assert(controller.reversal_braking != 0U);

    /* Once wrong-way motion is stopped, resume learned-angle search. */
    (void) BallPositionController_Update(
        &controller, -50.0f, 50.0f, 8.0f, 0.02f);
    assert(controller.filtered_velocity_mm_s == 0.0f);
    assert(controller.reversal_braking == 0U);
    assert(controller.integral_millidegrees != 0.0f);

    /* Final hold also ignores the measured static velocity jitter. */
    BallPositionController_Reset(&controller);
    controller.output_millidegrees = -1000.0f;
    controller.integral_millidegrees = -1000.0f;
    output = BallPositionController_UpdateHold(
        &controller, -50.0f, -50.0f, -10.0f, 0.02f);
    assert(output == -990);
    assert(controller.filtered_velocity_mm_s == 0.0f);
    assert(controller.integral_millidegrees == -990.0f);
    assert(controller.saturated == 0U);
    return 0;
}
