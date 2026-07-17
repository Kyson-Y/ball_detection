#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "motor_profile.h"

/* The calibrated hardware in this bring-up is the MG513X profile. */

int main(void)
{
    const motor_profile_t *profile;
    int16_t electrical_permille = 0;

    MotorProfile_Init();
    profile = MotorProfile_GetActive();

    assert(profile != NULL);
    assert(profile->profile_id == MOTOR_PROFILE_ID_513X);
    assert(profile->profile_version == 5U);
    assert(strcmp(profile->model_name, "MG513X") == 0);
    assert(profile->rated_voltage_mv == 12000U);
    assert(profile->rated_current_ma == 360U);
    assert(profile->stall_current_ma == 3200U);
    assert(profile->gear_ratio == 28.0f);
    assert(profile->encoder_ppr == 500U);
    assert(profile->encoder_signal_mv == 3300U);
    assert(profile->maximum_output_rpm == 370U);
    assert(profile->start_pwm_permille == 600U);
    assert(profile->maximum_pwm_permille == 650U);
    assert(profile->speed_limit_rpm == 100.0f);
    assert(profile->acceleration_limit_rpm_per_s == 150.0f);
    assert(profile->encoder_interface == MOTOR_ENCODER_INTERFACE_GMR_AB);
    assert(profile->wheel[MOTOR_WHEEL_LEFT].counts_per_output_revolution ==
        56000U);
    assert(profile->wheel[MOTOR_WHEEL_RIGHT].counts_per_output_revolution ==
        14000U);

    assert(g_motor_profile_diag.selection_valid == 1U);
    assert(MotorProfile_ActuatorTestReady());
    assert(MotorProfile_ClosedLoopReady());
    assert(g_motor_profile_diag.output_locked == 0U);

    assert(MotorProfile_NormalizeMotorPermille(
        MOTOR_WHEEL_LEFT, 650, &electrical_permille));
    assert(electrical_permille == 650);
    assert(MotorProfile_NormalizeMotorPermille(
        MOTOR_WHEEL_RIGHT, 650, &electrical_permille));
    assert(electrical_permille == -650);
    assert(!MotorProfile_NormalizeMotorPermille(
        MOTOR_WHEEL_LEFT, 651, &electrical_permille));
    assert(!MotorProfile_NormalizeMotorPermille(
        MOTOR_WHEEL_COUNT, 100, &electrical_permille));

    MotorProfile_UpdateEncoderSpeeds(560, -140, 10000U);
    assert(g_motor_profile_diag.left_output_rpm > 59.99f);
    assert(g_motor_profile_diag.left_output_rpm < 60.01f);
    assert(g_motor_profile_diag.right_output_rpm > 59.99f);
    assert(g_motor_profile_diag.right_output_rpm < 60.01f);
    assert(g_motor_profile_diag.speed_sample_count == 1U);
    assert(g_motor_profile_diag.invalid_speed_sample_count == 0U);
    return 0;
}
