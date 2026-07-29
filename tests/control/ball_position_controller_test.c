#include <assert.h>

#include "ball_position_controller.h"

int main(void)
{
    const ball_position_controller_config_t config = {
        100.0f, 20.0f, 10000.0f, 50000.0f
    };
    ball_position_controller_t controller;
    int32_t output;

    assert(BallPositionController_Init(&controller, &config));
    output = BallPositionController_Update(
        &controller, 50.0f, 0.0f, 0.0f, 0.02f);
    assert(output == 1000);
    assert(controller.saturated == 0U);

    output = BallPositionController_Update(
        &controller, 50.0f, 0.0f, 0.0f, 0.02f);
    assert(output == 2000);
    output = BallPositionController_Update(
        &controller, 50.0f, 0.0f, 500.0f, 0.02f);
    assert(output == 1000);

    BallPositionController_Reset(&controller);
    output = BallPositionController_Update(
        &controller, 1000.0f, 0.0f, 0.0f, 0.25f);
    assert(output == 10000);
    assert(controller.saturated != 0U);
    return 0;
}
