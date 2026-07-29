#ifndef ECHO_BALL_POSITION_CONTROLLER_H
#define ECHO_BALL_POSITION_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float kp_millidegrees_per_mm;
    float kd_millidegrees_per_mm_s;
    float maximum_output_millidegrees;
    float maximum_slew_millidegrees_per_s;
} ball_position_controller_config_t;

typedef struct {
    ball_position_controller_config_t config;
    float output_millidegrees;
    float proportional_millidegrees;
    float damping_millidegrees;
    float error_mm;
    uint32_t update_count;
    uint8_t saturated;
    uint8_t initialized;
    uint8_t reserved[2];
} ball_position_controller_t;

bool BallPositionController_Init(ball_position_controller_t *controller,
    const ball_position_controller_config_t *config);
void BallPositionController_Reset(ball_position_controller_t *controller);
int32_t BallPositionController_Update(ball_position_controller_t *controller,
    float target_mm, float position_mm, float velocity_mm_s, float dt_s);

#endif
