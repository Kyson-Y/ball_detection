#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "motor_profile.h"
#include "motor_profile_config.h"

/* The calibrated hardware in this bring-up is the MG513X profile. */

int main(void)
{
    const motor_profile_t *profile;
    int16_t electrical_permille = 0;

    MotorProfile_Init();
    profile = MotorProfile_GetActive();

    assert(profile != NULL);
#if ECHO_MOTOR_PROFILE_SELECTION == ECHO_MOTOR_PROFILE_513X
    assert(profile->profile_id == MOTOR_PROFILE_ID_513X);
    assert(profile->profile_version == 5U);
    assert(strcmp(profile->model_name, "MG513X") == 0);
    assert(profile->start_pwm_permille == 600U);
    assert(profile->maximum_pwm_permille == 650U);
    assert(profile->speed_pid.output_limit_permille == 650.0f);
    assert(profile->speed_pid.left_feedforward_offset_permille == 545.0f);
    assert(profile->speed_pid.right_feedforward_offset_permille == 535.0f);
#elif ECHO_MOTOR_PROFILE_SELECTION == ECHO_MOTOR_PROFILE_513X_4S
    assert(profile->profile_id == MOTOR_PROFILE_ID_513X_4S);
    assert(profile->profile_version == 14U);
    assert(strcmp(profile->model_name, "MG513X-4S") == 0);
    assert(profile->start_pwm_permille == 600U);
    assert(profile->maximum_pwm_permille == 650U);
    assert(profile->speed_pid.output_limit_permille == 650.0f);
    assert(profile->speed_pid.boost_minimum_ms == 10U);
    assert(profile->speed_pid.boost_release_fraction == 0.25f);
    assert(profile->speed_pid.kp == 6.0f);
    assert(profile->speed_pid.ki == 20.0f);
    assert(profile->speed_pid.kd == 0.0f);
    assert(profile->speed_pid.integrator_limit == 200.0f);
    assert(profile->speed_pid.left_feedforward_offset_permille == 388.5f);
    assert(profile->speed_pid.right_feedforward_offset_permille == 371.0f);
    assert(profile->speed_pid.left_feedforward_gain_permille_per_rpm ==
        1.93f);
    assert(profile->speed_pid.right_feedforward_gain_permille_per_rpm ==
        2.17f);
    assert(profile->speed_pid.target_slew_rpm_per_s == 150.0f);
    assert(profile->speed_pid.target_slew_down_rpm_per_s == 90.0f);
    assert(profile->control_reference_voltage_mv == 16580U);
#else
#error "This test only supports the 513X supply profiles."
#endif
    assert(profile->rated_voltage_mv == 12000U);
    assert(profile->rated_current_ma == 360U);
    assert(profile->stall_current_ma == 3200U);
    assert(profile->gear_ratio == 28.0f);
    assert(profile->encoder_ppr == 500U);
    assert(profile->encoder_signal_mv == 3300U);
    assert(profile->maximum_output_rpm == 370U);
    assert(profile->speed_limit_rpm == 100.0f);
    assert(profile->acceleration_limit_rpm_per_s == 150.0f);
    assert(profile->encoder_interface == MOTOR_ENCODER_INTERFACE_GMR_AB);
    assert(profile->wheel[MOTOR_WHEEL_LEFT].counts_per_output_revolution ==
        56000U);
    assert(profile->wheel[MOTOR_WHEEL_RIGHT].counts_per_output_revolution ==
        14000U);
    assert(profile->wheel[MOTOR_WHEEL_LEFT].encoder_count_sign == -1);
    assert(profile->wheel[MOTOR_WHEEL_RIGHT].encoder_count_sign == 1);

    assert(g_motor_profile_diag.selection_valid == 1U);
    assert(MotorProfile_ActuatorTestReady());
    assert(MotorProfile_ClosedLoopReady());
    assert(g_motor_profile_diag.output_locked == 0U);

    assert(MotorProfile_NormalizeMotorPermille(
        MOTOR_WHEEL_LEFT, (int16_t) profile->maximum_pwm_permille,
        &electrical_permille));
    assert(electrical_permille ==
        (int16_t) profile->maximum_pwm_permille);
    assert(MotorProfile_NormalizeMotorPermille(
        MOTOR_WHEEL_RIGHT, (int16_t) profile->maximum_pwm_permille,
        &electrical_permille));
    assert(electrical_permille ==
        -(int16_t) profile->maximum_pwm_permille);
    assert(!MotorProfile_NormalizeMotorPermille(
        MOTOR_WHEEL_LEFT,
        (int16_t) (profile->maximum_pwm_permille + 1U),
        &electrical_permille));
    assert(!MotorProfile_NormalizeMotorPermille(
        MOTOR_WHEEL_COUNT, 100, &electrical_permille));

    MotorProfile_UpdateEncoderSpeeds(-560, 140, 10000U);
    assert(g_motor_profile_diag.left_output_rpm > 59.99f);
    assert(g_motor_profile_diag.left_output_rpm < 60.01f);
    assert(g_motor_profile_diag.right_output_rpm > 59.99f);
    assert(g_motor_profile_diag.right_output_rpm < 60.01f);
    assert(g_motor_profile_diag.speed_sample_count == 1U);
    assert(g_motor_profile_diag.invalid_speed_sample_count == 0U);
    return 0;
}
