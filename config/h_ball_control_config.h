#ifndef ECHO_H_BALL_CONTROL_CONFIG_H
#define ECHO_H_BALL_CONTROL_CONFIG_H

/*
 * Provisional linkage defaults. H_BALL_MOTOR_POLARITY is the first item to
 * verify on the assembled mechanism: a positive controller output must
 * accelerate the ball toward the camera's positive coordinate direction.
 */
#define H_BALL_MOTOR_POLARITY                        -1
#define H_BALL_KP_MILLIDEGREES_PER_MM             120.0f
#define H_BALL_KD_MILLIDEGREES_PER_MM_S            10.0f
#define H_BALL_MAXIMUM_OUTPUT_MILLIDEGREES        6000.0f
#define H_BALL_MAXIMUM_SLEW_MILLIDEGREES_PER_S   12000.0f
#define H_BALL_MOTOR_SPEED_RPM                         15U
#define H_BALL_MOTOR_ACCELERATION_RPM_S               100U
#define H_BALL_COMMAND_PERIOD_US                    20000U
#define H_BALL_VISION_HOLD_US                      120000U
#define H_BALL_STARTUP_TIMEOUT_US                  600000U
#define H_BALL_H3_TIMEOUT_US                            0U
#define H_BALL_H3_POSITIVE_TARGET_DECIMM              500
#define H_BALL_H3_NEGATIVE_TARGET_DECIMM             -500
#define H_BALL_H3_POSITIVE_REACHED_DECIMM              450
#define H_BALL_H3_FINAL_TOLERANCE_DECIMM                80
#define H_BALL_H3_FINAL_VELOCITY_MM_S                   30
#define H_BALL_H3_FINAL_SETTLE_US                   250000U
#define H_BALL_H3_START_POSITION_LIMIT_DECIMM           150
#define H_BALL_H3_START_VELOCITY_LIMIT_MM_S              60

#endif
