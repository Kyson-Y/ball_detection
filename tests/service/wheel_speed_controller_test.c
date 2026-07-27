#include <assert.h>
#include <math.h>
#include <string.h>

#include "wheel_speed_controller.h"

static wheel_speed_controller_config_t TestConfig(void)
{
    wheel_speed_controller_config_t config;

    memset(&config, 0, sizeof(config));
    config.kp = 6.0f;
    config.ki = 8.0f;
    config.integrator_limit_permille = 90.0f;
    config.load_release_unwind_gain = 4.0f;
    config.load_release_threshold_rpm = 3.0f;
    config.load_release_output_slew_down_permille_per_s = 6000.0f;
    config.output_limit_permille = 650.0f;
    config.feedforward_offset_permille = 371.0f;
    config.feedforward_gain_permille_per_rpm = 2.17f;
    config.measurement_filter_alpha = 1.0f;
    config.target_slew_rpm_per_s = 150.0f;
    config.target_slew_down_rpm_per_s = 90.0f;
    config.output_slew_up_permille_per_s = 3000.0f;
    config.output_slew_down_permille_per_s = 3000.0f;
    return config;
}

static void TestDownStepHoldsOpposingIntegrator(void)
{
    wheel_speed_controller_config_t config = TestConfig();
    wheel_speed_controller_t controller;
    int index;

    WheelSpeedController_Init(&controller, &config);
    controller.requested_target_rpm = 20.0f;
    controller.ramped_target_rpm = 20.0f;
    controller.integrator_permille = -20.0f;
    WheelSpeedController_PrimeOutput(&controller, 400.0f);

    (void) WheelSpeedController_Update(&controller, 8.0f, 20.0f, 0.01f);
    assert(controller.deceleration_integrator_hold_active == 1U);
    assert(controller.integrator_permille == 0.0f);

    for (index = 0; index < 20; index++) {
        (void) WheelSpeedController_Update(
            &controller, 8.0f, 12.0f, 0.01f);
    }
    assert(fabsf(controller.ramped_target_rpm - 8.0f) < 0.01f);
    assert(controller.deceleration_integrator_hold_active == 1U);
    assert(controller.integrator_permille == 0.0f);

    (void) WheelSpeedController_Update(&controller, 8.0f, 10.0f, 0.01f);
    assert(controller.deceleration_integrator_hold_active == 0U);
    assert(controller.integrator_permille < 0.0f);
    assert(controller.integrator_permille > -1.0f);
}

static void TestUpStepDoesNotArmDecelerationHold(void)
{
    wheel_speed_controller_config_t config = TestConfig();
    wheel_speed_controller_t controller;

    WheelSpeedController_Init(&controller, &config);
    controller.requested_target_rpm = 8.0f;
    controller.ramped_target_rpm = 8.0f;
    controller.integrator_permille = -5.0f;
    WheelSpeedController_PrimeOutput(&controller, 385.0f);

    (void) WheelSpeedController_Update(&controller, 20.0f, 8.0f, 0.01f);
    assert(controller.deceleration_integrator_hold_active == 0U);
    assert(controller.integrator_permille == -5.0f);
}

int main(void)
{
    TestDownStepHoldsOpposingIntegrator();
    TestUpStepDoesNotArmDecelerationHold();
    return 0;
}
