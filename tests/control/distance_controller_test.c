#include <assert.h>
#include <math.h>

#include "distance_controller.h"

static distance_controller_config_t TestConfig(void)
{
    distance_controller_config_t config;

    config.wheel_circumference_mm = 200.0f;
    config.left_counts_per_revolution = 4000U;
    config.right_counts_per_revolution = 1000U;
    config.left_encoder_count_sign = -1;
    config.right_encoder_count_sign = 1;
    config.departure_minimum_rpm = 15.0f;
    config.departure_gain_rpm_per_mm = 0.1f;
    config.approach_minimum_rpm = 10.0f;
    config.approach_gain_rpm_per_mm = 0.2f;
    return config;
}

static void TestForwardCenterDistanceAndApproach(void)
{
    distance_controller_config_t config = TestConfig();
    distance_controller_t controller;

    DistanceController_Init(&controller, &config);
    assert(DistanceController_Start(&controller, 40.0f, 200.0f));
    assert(fabsf(DistanceController_GetTargetSpeedRpm(&controller) -
        15.0f) < 0.001f);
    assert(!DistanceController_Update(&controller, -2000, 500));
    assert(fabsf(controller.center_distance_mm - 100.0f) < 0.001f);
    assert(fabsf(DistanceController_GetTargetSpeedRpm(&controller) -
        25.0f) < 0.001f);
    assert(DistanceController_Update(&controller, -2000, 500));
    assert(controller.reached != 0U);
    assert(DistanceController_GetTargetSpeedRpm(&controller) == 0.0f);
}

static void TestReverseUsesEncoderDirection(void)
{
    distance_controller_config_t config = TestConfig();
    distance_controller_t controller;

    DistanceController_Init(&controller, &config);
    assert(DistanceController_Start(&controller, -20.0f, 50.0f));
    assert(DistanceController_Update(&controller, 1000, -250));
    assert(fabsf(controller.left_distance_mm - 50.0f) < 0.001f);
    assert(fabsf(controller.right_distance_mm - 50.0f) < 0.001f);
}

int main(void)
{
    TestForwardCenterDistanceAndApproach();
    TestReverseUsesEncoderDirection();
    return 0;
}
