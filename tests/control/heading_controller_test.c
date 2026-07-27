#include <assert.h>
#include <math.h>

#include "heading_controller.h"

static heading_controller_config_t TestConfig(void)
{
    heading_controller_config_t config;

    config.kp_rpm_per_deg = 0.25f;
    config.kd_rpm_per_dps = 0.03f;
    config.maximum_correction_rpm = 6.0f;
    return config;
}

int main(void)
{
    heading_controller_config_t config = TestConfig();
    heading_controller_t controller;

    HeadingController_Init(&controller, &config);
    HeadingController_Start(&controller, 12.0f, 10.0f);
    assert(HeadingController_Update(&controller, 0.0f, 0.0f));
    assert(fabsf(controller.error_deg - 10.0f) < 0.001f);
    assert(fabsf(controller.left_target_rpm - 9.5f) < 0.001f);
    assert(fabsf(controller.right_target_rpm - 14.5f) < 0.001f);

    HeadingController_Start(&controller, 12.0f, -179.0f);
    assert(HeadingController_Update(&controller, 179.0f, 0.0f));
    assert(fabsf(controller.error_deg - 2.0f) < 0.001f);

    HeadingController_Start(&controller, 12.0f, 90.0f);
    assert(HeadingController_Update(&controller, 0.0f, 0.0f));
    assert(fabsf(controller.correction_rpm - 6.0f) < 0.001f);
    assert(controller.saturation_count == 1U);
    HeadingController_Stop(&controller);
    assert(!HeadingController_Update(&controller, 0.0f, 0.0f));
    return 0;
}
