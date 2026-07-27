#include "chassis_actuator.h"

#include <string.h>

#include "FreeRTOS.h"
#include "attitude_estimator.h"
#include "bsp_motor.h"
#include "bsp_supply_voltage.h"
#include "bsp_time.h"
#include "distance_controller.h"
#include "heading_controller.h"
#include "motor_profile.h"
#include "parameter_service.h"
#include "task.h"
#include "wheel_speed_controller.h"

#define CHASSIS_ACTUATOR_CONTROL_PERIOD_MS 10U
#define CHASSIS_ACTUATOR_LOW_SPEED_CYCLES      15U
#define CHASSIS_ACTUATOR_RECOVERY_BOOST_CYCLES  8U
#define CHASSIS_ACTUATOR_RECOVERY_MIN_CYCLES    2U
#define CHASSIS_ACTUATOR_MAX_RECOVERY_ATTEMPTS  5U
#define CHASSIS_ACTUATOR_RECOVERY_RPM            1.0f
#define CHASSIS_ACTUATOR_RECOVERY_OUTPUT         330
#define CHASSIS_ACTUATOR_CRAWL_MAX_RPM            8.0f
#define CHASSIS_ACTUATOR_CRAWL_HYSTERESIS_RPM     1.0f
#define CHASSIS_ACTUATOR_CRAWL_STOP_RPM           1.0f
#define CHASSIS_ACTUATOR_CRAWL_HOLD_PERMILLE     350U
#define CHASSIS_ACTUATOR_CRAWL_KICK_PERMILLE     600U
#define CHASSIS_ACTUATOR_CRAWL_KICK_CYCLES        4U
#define CHASSIS_ACTUATOR_CRAWL_REST_CYCLES        4U
#define CHASSIS_ACTUATOR_SUPPLY_MIN_MV         12000U
#define CHASSIS_ACTUATOR_SUPPLY_MAX_MV         18000U
#define CHASSIS_ACTUATOR_SUPPLY_STALE_CYCLES      50U
#define CHASSIS_ACTUATOR_COMPENSATED_MAX_PERMILLE 900
#define CHASSIS_ACTUATOR_HEADING_MAX_DELTA_DECI_DEG 3600
#define CHASSIS_ACTUATOR_HEADING_MIN_BASE_DECI_RPM   80
#define CHASSIS_ACTUATOR_HEADING_MAX_AGE_US       50000U
#define CHASSIS_ACTUATOR_HEADING_KP_RPM_PER_DEG     0.55f
#define CHASSIS_ACTUATOR_HEADING_KD_RPM_PER_DPS     0.20f
#define CHASSIS_ACTUATOR_HEADING_MAX_CORRECTION_RPM 8.0f
#define CHASSIS_ACTUATOR_PIVOT_KP_RPM_PER_DEG       0.55f
#define CHASSIS_ACTUATOR_PIVOT_KD_RPM_PER_DPS       0.03f
#define CHASSIS_ACTUATOR_PIVOT_MAX_RPM              35.0f
#define CHASSIS_ACTUATOR_PIVOT_MIN_RPM              15.0f
#define CHASSIS_ACTUATOR_PIVOT_SETTLE_ERROR_DEG      1.5f
#define CHASSIS_ACTUATOR_PIVOT_BRAKE_CYCLES           8U
#define CHASSIS_ACTUATOR_PIVOT_INTEGRATOR_PRELOAD   150.0f
#define CHASSIS_ACTUATOR_HEADING_STALL_CYCLES        100U
#define CHASSIS_ACTUATOR_WHEEL_CIRCUMFERENCE_MM      204.2035f
#define CHASSIS_ACTUATOR_DISTANCE_DEPARTURE_MIN_RPM  15.0f
#define CHASSIS_ACTUATOR_DISTANCE_DEPARTURE_GAIN      0.1f
#define CHASSIS_ACTUATOR_CASTER_TRIGGER_DISTANCE_MM  40.0f
#define CHASSIS_ACTUATOR_CASTER_TRIGGER_HEADING_DEG   2.0f
#define CHASSIS_ACTUATOR_DISTANCE_APPROACH_MIN_RPM    10.0f
#define CHASSIS_ACTUATOR_DISTANCE_APPROACH_GAIN        0.2f
#define CHASSIS_ACTUATOR_DISTANCE_MIN_MM                20

volatile chassis_actuator_diagnostics_t g_chassis_actuator_diag;

static volatile chassis_actuator_debug_request_t s_pending_request;
static volatile bool s_pending_valid;
static wheel_speed_controller_t s_speed_controller[MOTOR_WHEEL_COUNT];
static uint16_t s_common_low_speed_cycles;
static uint16_t s_common_recovery_remaining;
static uint8_t s_consecutive_failed_recoveries;
static uint8_t s_recovery_candidate_mask;
static uint8_t s_recovery_active_mask;
static uint8_t s_crawl_drive_mask;
static uint8_t s_crawl_kick_remaining[MOTOR_WHEEL_COUNT];
static uint8_t s_crawl_rest_remaining[MOTOR_WHEEL_COUNT];
static bool s_continuous_speed;
static uint32_t s_last_supply_sequence;
static uint16_t s_supply_stale_cycles;
static heading_controller_t s_heading_controller;
static distance_controller_t s_distance_controller;
static bool s_heading_pivot_mode;
static bool s_heading_pivot_settled;
static uint16_t s_motion_brake_remaining;
static float s_heading_pivot_command_deg;
static float s_heading_pivot_progress_deg;
static float s_heading_pivot_last_yaw_deg;
static uint16_t s_heading_stall_cycles;
static bool s_distance_settled;
static int8_t s_last_distance_direction;
static int8_t s_distance_direction;
static bool s_caster_reversal_active;
static float s_distance_heading_reference_deg;

static const heading_controller_config_t s_heading_config = {
    CHASSIS_ACTUATOR_HEADING_KP_RPM_PER_DEG,
    CHASSIS_ACTUATOR_HEADING_KD_RPM_PER_DPS,
    CHASSIS_ACTUATOR_HEADING_MAX_CORRECTION_RPM
};

static void ChassisActuator_UpdateControllerDiagnostics(void)
{
    const wheel_speed_controller_t *left =
        &s_speed_controller[MOTOR_WHEEL_LEFT];
    const wheel_speed_controller_t *right =
        &s_speed_controller[MOTOR_WHEEL_RIGHT];

    g_chassis_actuator_diag.left_filtered_rpm = left->filtered_rpm;
    g_chassis_actuator_diag.right_filtered_rpm = right->filtered_rpm;
    g_chassis_actuator_diag.left_pid_proportional_permille =
        left->proportional_permille;
    g_chassis_actuator_diag.left_pid_integrator_permille =
        left->integrator_permille;
    g_chassis_actuator_diag.left_pid_derivative_permille =
        left->derivative_permille;
    g_chassis_actuator_diag.left_pid_feedforward_permille =
        left->feedforward_permille;
    g_chassis_actuator_diag.right_pid_proportional_permille =
        right->proportional_permille;
    g_chassis_actuator_diag.right_pid_integrator_permille =
        right->integrator_permille;
    g_chassis_actuator_diag.right_pid_derivative_permille =
        right->derivative_permille;
    g_chassis_actuator_diag.right_pid_feedforward_permille =
        right->feedforward_permille;
}

static uint16_t ChassisActuator_AbsPermille(int16_t value)
{
    return (value < 0) ? (uint16_t) (-value) : (uint16_t) value;
}

static float ChassisActuator_AbsFloat(float value)
{
    return (value < 0.0f) ? -value : value;
}

static bool ChassisActuator_SameDirection(float target, float measured)
{
    return (target > 0.0f && measured > 0.0f) ||
        (target < 0.0f && measured < 0.0f);
}

static int16_t ChassisActuator_SignedPermille(
    float target_rpm, uint16_t magnitude)
{
    if (target_rpm > 0.0f) {
        return (int16_t) magnitude;
    }
    if (target_rpm < 0.0f) {
        return -(int16_t) magnitude;
    }
    return 0;
}

static void ChassisActuator_PrimeTracking(
    motor_wheel_t wheel, float target_rpm, float measured_rpm)
{
    wheel_speed_controller_t *controller = &s_speed_controller[wheel];
    float magnitude = ChassisActuator_AbsFloat(target_rpm);
    float feedforward = controller->config.feedforward_offset_permille +
        controller->config.feedforward_gain_permille_per_rpm * magnitude;
    float integrator = 0.0f;

    if (target_rpm < 0.0f) {
        feedforward = -feedforward;
    } else if (target_rpm == 0.0f) {
        feedforward = 0.0f;
    }
    if (s_heading_pivot_mode && target_rpm != 0.0f) {
        integrator = (target_rpm < 0.0f) ?
            -CHASSIS_ACTUATOR_PIVOT_INTEGRATOR_PRELOAD :
            CHASSIS_ACTUATOR_PIVOT_INTEGRATOR_PRELOAD;
    }
    WheelSpeedController_PrimeOutput(
        controller, feedforward + integrator);
    controller->integrator_permille = integrator;
    if (!ChassisActuator_SameDirection(target_rpm, measured_rpm)) {
        measured_rpm = 0.0f;
    } else if (ChassisActuator_AbsFloat(measured_rpm) > magnitude) {
        measured_rpm = target_rpm;
    }
    controller->ramped_target_rpm = measured_rpm;
    controller->requested_target_rpm = target_rpm;
}

static int16_t ChassisActuator_RoundDeci(float value)
{
    float scaled = value * 10.0f;

    return (int16_t) ((scaled >= 0.0f) ?
        (scaled + 0.5f) : (scaled - 0.5f));
}

static void ChassisActuator_UpdateHeadingDiagnostics(void)
{
    g_chassis_actuator_diag.heading_target_deg =
        s_heading_controller.target_yaw_deg;
    g_chassis_actuator_diag.heading_current_deg =
        s_heading_controller.current_yaw_deg;
    g_chassis_actuator_diag.heading_error_deg =
        s_heading_controller.error_deg;
    g_chassis_actuator_diag.heading_correction_rpm =
        s_heading_controller.correction_rpm;
    g_chassis_actuator_diag.heading_update_count =
        s_heading_controller.update_count;
    g_chassis_actuator_diag.heading_saturation_count =
        s_heading_controller.saturation_count;
}

static void ChassisActuator_UpdateDistanceDiagnostics(void)
{
    g_chassis_actuator_diag.distance_target_mm =
        s_distance_controller.target_distance_mm;
    g_chassis_actuator_diag.distance_left_mm =
        s_distance_controller.left_distance_mm;
    g_chassis_actuator_diag.distance_right_mm =
        s_distance_controller.right_distance_mm;
    g_chassis_actuator_diag.distance_progress_mm =
        s_distance_controller.center_distance_mm;
    g_chassis_actuator_diag.distance_remaining_mm =
        s_distance_controller.remaining_distance_mm;
    g_chassis_actuator_diag.distance_update_count =
        s_distance_controller.update_count;
}

static bool ChassisActuator_UpdatePivotHeading(
    float current_yaw_deg, float yaw_rate_dps)
{
    float yaw_step_deg;
    float correction;
    float limit;

    if (s_heading_controller.active == 0U) {
        return false;
    }
    yaw_step_deg = HeadingController_WrapDegrees(
        current_yaw_deg - s_heading_pivot_last_yaw_deg);
    s_heading_pivot_progress_deg += yaw_step_deg;
    s_heading_pivot_last_yaw_deg = current_yaw_deg;
    s_heading_controller.current_yaw_deg =
        HeadingController_WrapDegrees(current_yaw_deg);
    s_heading_controller.error_deg =
        s_heading_pivot_command_deg - s_heading_pivot_progress_deg;
    correction = s_heading_controller.config.kp_rpm_per_deg *
        s_heading_controller.error_deg -
        s_heading_controller.config.kd_rpm_per_dps * yaw_rate_dps;
    limit = s_heading_controller.config.maximum_correction_rpm;
    if (correction > limit) {
        correction = limit;
        s_heading_controller.saturation_count++;
    } else if (correction < -limit) {
        correction = -limit;
        s_heading_controller.saturation_count++;
    }
    s_heading_controller.correction_rpm = correction;
    s_heading_controller.left_target_rpm = -correction;
    s_heading_controller.right_target_rpm = correction;
    s_heading_controller.update_count++;
    return true;
}

static bool ChassisActuator_UpdateHeadingTargets(void)
{
    attitude_estimator_snapshot_t attitude;
    uint32_t age_us;

    if (!AttitudeEstimator_GetSnapshot(&attitude) ||
        (attitude.flags & ATTITUDE_ESTIMATOR_FLAG_SOURCE_VALID) == 0U) {
        return false;
    }
    age_us = BSP_Time_GetUs() - attitude.timestamp_us;
    if (age_us > CHASSIS_ACTUATOR_HEADING_MAX_AGE_US) {
        return false;
    }
    if (g_chassis_actuator_diag.control_mode ==
        (uint8_t) CHASSIS_ACTUATOR_MODE_DISTANCE) {
        s_heading_controller.base_rpm =
            DistanceController_GetTargetSpeedRpm(
                &s_distance_controller);
        s_heading_controller.target_yaw_deg =
            s_distance_heading_reference_deg;
        if (s_caster_reversal_active &&
            s_distance_controller.center_distance_mm <
                CHASSIS_ACTUATOR_CASTER_TRIGGER_DISTANCE_MM) {
            s_heading_controller.target_yaw_deg =
                HeadingController_WrapDegrees(
                    s_distance_heading_reference_deg +
                    CHASSIS_ACTUATOR_CASTER_TRIGGER_HEADING_DEG *
                        (float) s_distance_direction);
        } else {
            s_caster_reversal_active = false;
        }
    }
    if (s_heading_pivot_mode) {
        if (!ChassisActuator_UpdatePivotHeading(
                attitude.yaw_deg, attitude.axis_rate_dps[2])) {
            return false;
        }
    } else if (!HeadingController_Update(&s_heading_controller,
            attitude.yaw_deg, attitude.axis_rate_dps[2])) {
        return false;
    }
    if (s_heading_pivot_mode) {
        float correction;
        float error_magnitude = ChassisActuator_AbsFloat(
            s_heading_controller.error_deg);

        if (!s_heading_pivot_settled &&
            error_magnitude <= CHASSIS_ACTUATOR_PIVOT_SETTLE_ERROR_DEG) {
            s_heading_pivot_settled = true;
            s_motion_brake_remaining =
                CHASSIS_ACTUATOR_PIVOT_BRAKE_CYCLES;
        }
        if (s_heading_pivot_settled) {
            s_heading_controller.correction_rpm = 0.0f;
            s_heading_controller.left_target_rpm = 0.0f;
            s_heading_controller.right_target_rpm = 0.0f;
        } else if (ChassisActuator_AbsFloat(
                s_heading_controller.correction_rpm) <
                CHASSIS_ACTUATOR_PIVOT_MIN_RPM) {
            correction = (s_heading_controller.error_deg < 0.0f) ?
                -CHASSIS_ACTUATOR_PIVOT_MIN_RPM :
                CHASSIS_ACTUATOR_PIVOT_MIN_RPM;
            s_heading_controller.correction_rpm = correction;
            s_heading_controller.left_target_rpm = -correction;
            s_heading_controller.right_target_rpm = correction;
        }
    }
    g_chassis_actuator_diag.left_target_deci_rpm =
        ChassisActuator_RoundDeci(
            s_heading_controller.left_target_rpm);
    g_chassis_actuator_diag.right_target_deci_rpm =
        ChassisActuator_RoundDeci(
            s_heading_controller.right_target_rpm);
    ChassisActuator_UpdateHeadingDiagnostics();
    return true;
}

static bool ChassisActuator_IsCrawlTarget(float target_rpm)
{
    float magnitude = ChassisActuator_AbsFloat(target_rpm);

    return magnitude >= 0.1f &&
        magnitude < CHASSIS_ACTUATOR_CRAWL_MAX_RPM;
}

static int16_t ChassisActuator_UpdateCrawlWheel(
    motor_wheel_t wheel, float target_rpm, float measured_rpm)
{
    wheel_speed_controller_t *controller = &s_speed_controller[wheel];
    uint8_t mask = (uint8_t) (1U << (uint32_t) wheel);
    float target_magnitude = ChassisActuator_AbsFloat(target_rpm);
    float directed_speed = measured_rpm *
        ((target_rpm < 0.0f) ? -1.0f : 1.0f);
    float low_rpm = target_magnitude -
        CHASSIS_ACTUATOR_CRAWL_HYSTERESIS_RPM;
    float high_rpm = target_magnitude +
        CHASSIS_ACTUATOR_CRAWL_HYSTERESIS_RPM;
    uint16_t hold_permille;
    int16_t output;

    WheelSpeedController_Observe(controller, measured_rpm);
    controller->integrator_permille = 0.0f;
    controller->ramped_target_rpm = target_rpm;
    controller->requested_target_rpm = target_rpm;
    if (low_rpm < 0.5f) {
        low_rpm = 0.5f;
    }

    if (directed_speed < CHASSIS_ACTUATOR_CRAWL_STOP_RPM) {
        if (s_crawl_kick_remaining[wheel] == 0U &&
            s_crawl_rest_remaining[wheel] == 0U) {
            s_crawl_kick_remaining[wheel] =
                CHASSIS_ACTUATOR_CRAWL_KICK_CYCLES;
        }
        if (s_crawl_kick_remaining[wheel] != 0U) {
            s_crawl_kick_remaining[wheel]--;
            if (s_crawl_kick_remaining[wheel] == 0U) {
                s_crawl_rest_remaining[wheel] =
                    CHASSIS_ACTUATOR_CRAWL_REST_CYCLES;
            }
            output = ChassisActuator_SignedPermille(
                target_rpm, CHASSIS_ACTUATOR_CRAWL_KICK_PERMILLE);
        } else {
            s_crawl_rest_remaining[wheel]--;
            output = 0;
        }
        WheelSpeedController_PrimeOutput(controller, (float) output);
        return output;
    }

    s_crawl_kick_remaining[wheel] = 0U;
    s_crawl_rest_remaining[wheel] = 0U;
    if ((s_crawl_drive_mask & mask) != 0U) {
        if (directed_speed >= high_rpm) {
            s_crawl_drive_mask &= (uint8_t) ~mask;
        }
    } else if (directed_speed <= low_rpm) {
        s_crawl_drive_mask |= mask;
    }

    hold_permille = (uint16_t) (
        controller->config.feedforward_offset_permille +
        controller->config.feedforward_gain_permille_per_rpm *
            target_magnitude + 0.5f);
    if (hold_permille < CHASSIS_ACTUATOR_CRAWL_HOLD_PERMILLE) {
        hold_permille = CHASSIS_ACTUATOR_CRAWL_HOLD_PERMILLE;
    }
    output = ((s_crawl_drive_mask & mask) != 0U) ?
        ChassisActuator_SignedPermille(target_rpm, hold_permille) : 0;
    WheelSpeedController_PrimeOutput(controller, (float) output);
    return output;
}

static float ChassisActuator_BoostReleaseRpm(float target_rpm)
{
    const motor_speed_pid_config_t *config =
        MotorProfile_GetSpeedPidConfig();
    float release_rpm = config->boost_release_fraction *
        ChassisActuator_AbsFloat(target_rpm);

    if (release_rpm < 1.0f) {
        release_rpm = 1.0f;
    }
    if (release_rpm > config->boost_release_rpm) {
        release_rpm = config->boost_release_rpm;
    }
    return release_rpm;
}

static float ChassisActuator_RecoveryReleaseRpm(float target_rpm)
{
    const motor_speed_pid_config_t *config =
        MotorProfile_GetSpeedPidConfig();
    float release_rpm = 0.8f * ChassisActuator_AbsFloat(target_rpm);

    if (release_rpm < CHASSIS_ACTUATOR_RECOVERY_RPM) {
        release_rpm = CHASSIS_ACTUATOR_RECOVERY_RPM;
    }
    if (release_rpm > config->boost_release_rpm) {
        release_rpm = config->boost_release_rpm;
    }
    return release_rpm;
}

static void ChassisActuator_InitSpeedControllers(void)
{
    const motor_speed_pid_config_t *profile =
        MotorProfile_GetSpeedPidConfig();
    wheel_speed_controller_config_t config;

    memset(&config, 0, sizeof(config));
    config.kp = profile->kp;
    config.ki = profile->ki;
    config.kd = profile->kd;
    config.integrator_limit_permille = profile->integrator_limit;
    config.load_release_unwind_gain =
        profile->load_release_unwind_gain;
    config.load_release_threshold_rpm =
        profile->load_release_threshold_rpm;
    config.load_release_output_slew_down_permille_per_s =
        profile->load_release_output_slew_down_permille_per_s;
    config.output_limit_permille = profile->output_limit_permille;
    config.measurement_filter_alpha =
        profile->measurement_filter_alpha;
    config.target_slew_rpm_per_s = profile->target_slew_rpm_per_s;
    config.target_slew_down_rpm_per_s =
        profile->target_slew_down_rpm_per_s;
    config.output_slew_up_permille_per_s =
        profile->output_slew_up_permille_per_s;
    config.output_slew_down_permille_per_s =
        profile->output_slew_down_permille_per_s;
    config.feedforward_offset_permille =
        profile->left_feedforward_offset_permille;
    config.feedforward_gain_permille_per_rpm =
        profile->left_feedforward_gain_permille_per_rpm;
    WheelSpeedController_Init(
        &s_speed_controller[MOTOR_WHEEL_LEFT], &config);

    config.feedforward_offset_permille =
        profile->right_feedforward_offset_permille;
    config.feedforward_gain_permille_per_rpm =
        profile->right_feedforward_gain_permille_per_rpm;
    WheelSpeedController_Init(
        &s_speed_controller[MOTOR_WHEEL_RIGHT], &config);
}

static chassis_actuator_command_status_t ChassisActuator_ValidateRequest(
    const chassis_actuator_debug_request_t *request)
{
    uint32_t active_motor_count = 0U;

    if (request == NULL ||
        request->magic != CHASSIS_ACTUATOR_DEBUG_MAGIC ||
        request->magic_inverse != CHASSIS_ACTUATOR_DEBUG_MAGIC_INVERSE) {
        return CHASSIS_ACTUATOR_COMMAND_BAD_MAGIC;
    }
    if (request->sequence == 0U ||
        (request->duration_ms == 0U && request->reserved !=
            (uint16_t) CHASSIS_ACTUATOR_MODE_SPEED)) {
        return CHASSIS_ACTUATOR_COMMAND_BAD_VALUE;
    }

    if (request->reserved == (uint16_t) CHASSIS_ACTUATOR_MODE_ELECTRICAL) {
        const motor_profile_t *profile = MotorProfile_GetActive();

        if (!MotorProfile_ActuatorTestReady()) {
            return CHASSIS_ACTUATOR_COMMAND_PROFILE_LOCKED;
        }
        if (request->duration_ms > CHASSIS_ACTUATOR_TEST_MAX_DURATION_MS ||
            ChassisActuator_AbsPermille(
                request->left_electrical_permille) >
                CHASSIS_ACTUATOR_TEST_MAX_PERMILLE ||
            ChassisActuator_AbsPermille(
                request->right_electrical_permille) >
                CHASSIS_ACTUATOR_TEST_MAX_PERMILLE ||
            ChassisActuator_AbsPermille(
                request->left_electrical_permille) >
                profile->maximum_pwm_permille ||
            ChassisActuator_AbsPermille(
                request->right_electrical_permille) >
                profile->maximum_pwm_permille) {
            return CHASSIS_ACTUATOR_COMMAND_BAD_VALUE;
        }
    } else if (request->reserved ==
        (uint16_t) CHASSIS_ACTUATOR_MODE_SPEED) {
        const motor_profile_t *profile = MotorProfile_GetActive();
        uint16_t maximum_deci_rpm =
            (uint16_t) (profile->speed_limit_rpm * 10.0f + 0.5f);

        if (!MotorProfile_ClosedLoopReady()) {
            return CHASSIS_ACTUATOR_COMMAND_PROFILE_LOCKED;
        }
        if (request->duration_ms >
                CHASSIS_ACTUATOR_SPEED_MAX_DURATION_MS ||
            ChassisActuator_AbsPermille(
                request->left_electrical_permille) > maximum_deci_rpm ||
            ChassisActuator_AbsPermille(
                request->right_electrical_permille) > maximum_deci_rpm) {
            return CHASSIS_ACTUATOR_COMMAND_BAD_VALUE;
        }
    } else if (request->reserved ==
        (uint16_t) CHASSIS_ACTUATOR_MODE_HEADING) {
        const motor_profile_t *profile = MotorProfile_GetActive();
        uint16_t maximum_deci_rpm =
            (uint16_t) (profile->speed_limit_rpm * 10.0f + 0.5f);

        if (!MotorProfile_ClosedLoopReady()) {
            return CHASSIS_ACTUATOR_COMMAND_PROFILE_LOCKED;
        }
        if (request->duration_ms == 0U ||
            request->duration_ms >
                CHASSIS_ACTUATOR_HEADING_MAX_DURATION_MS ||
            ChassisActuator_AbsPermille(
                request->left_electrical_permille) >
                maximum_deci_rpm - (uint16_t) (
                    CHASSIS_ACTUATOR_HEADING_MAX_CORRECTION_RPM * 10.0f) ||
            (request->left_electrical_permille != 0 &&
             ChassisActuator_AbsPermille(
                request->left_electrical_permille) <
                CHASSIS_ACTUATOR_HEADING_MIN_BASE_DECI_RPM) ||
            ChassisActuator_AbsPermille(
                request->right_electrical_permille) >
                CHASSIS_ACTUATOR_HEADING_MAX_DELTA_DECI_DEG) {
            return CHASSIS_ACTUATOR_COMMAND_BAD_VALUE;
        }
    } else if (request->reserved ==
        (uint16_t) CHASSIS_ACTUATOR_MODE_DISTANCE) {
        const motor_profile_t *profile = MotorProfile_GetActive();
        uint16_t maximum_deci_rpm =
            (uint16_t) (profile->speed_limit_rpm * 10.0f + 0.5f);

        if (!MotorProfile_ClosedLoopReady()) {
            return CHASSIS_ACTUATOR_COMMAND_PROFILE_LOCKED;
        }
        if (request->duration_ms == 0U ||
            request->duration_ms >
                CHASSIS_ACTUATOR_DISTANCE_MAX_DURATION_MS ||
            ChassisActuator_AbsPermille(
                request->left_electrical_permille) >
                maximum_deci_rpm - (uint16_t) (
                    CHASSIS_ACTUATOR_HEADING_MAX_CORRECTION_RPM * 10.0f) ||
            ChassisActuator_AbsPermille(
                request->left_electrical_permille) <
                CHASSIS_ACTUATOR_HEADING_MIN_BASE_DECI_RPM ||
            request->right_electrical_permille <
                CHASSIS_ACTUATOR_DISTANCE_MIN_MM ||
            request->right_electrical_permille >
                CHASSIS_ACTUATOR_DISTANCE_MAX_MM) {
            return CHASSIS_ACTUATOR_COMMAND_BAD_VALUE;
        }
    } else {
        return CHASSIS_ACTUATOR_COMMAND_BAD_VALUE;
    }

    if (request->left_electrical_permille != 0) {
        active_motor_count++;
    }
    if (request->right_electrical_permille != 0) {
        active_motor_count++;
    }
    if (active_motor_count >= 1U && active_motor_count <= 2U) {
        return CHASSIS_ACTUATOR_COMMAND_STAGED;
    }
    return (active_motor_count == 0U && request->reserved ==
        (uint16_t) CHASSIS_ACTUATOR_MODE_SPEED) ?
        CHASSIS_ACTUATOR_COMMAND_STAGED :
        CHASSIS_ACTUATOR_COMMAND_BAD_VALUE;
}

static bool ChassisActuator_ApplyNormalized(
    int16_t left_normalized, int16_t right_normalized)
{
    const motor_profile_t *profile = MotorProfile_GetActive();
    uint32_t supply_sequence;
    uint32_t supply_sample_count;
    uint32_t supply_mv;
    int32_t scale_permille;
    int32_t left_compensated;
    int32_t right_compensated;
    int16_t left_electrical;
    int16_t right_electrical;

    if (!MotorProfile_NormalizeMotorPermille(MOTOR_WHEEL_LEFT,
            left_normalized, &left_electrical) ||
        !MotorProfile_NormalizeMotorPermille(MOTOR_WHEEL_RIGHT,
            right_normalized, &right_electrical)) {
        return false;
    }

    taskENTER_CRITICAL();
    supply_sequence = g_bsp_supply_voltage_diag.sample_sequence;
    supply_sample_count = g_bsp_supply_voltage_diag.sample_count;
    supply_mv = g_bsp_supply_voltage_diag.battery_mv;
    taskEXIT_CRITICAL();

    if (supply_sequence != s_last_supply_sequence) {
        s_last_supply_sequence = supply_sequence;
        s_supply_stale_cycles = 0U;
    } else if (s_supply_stale_cycles < UINT16_MAX) {
        s_supply_stale_cycles++;
    }
    if (supply_sample_count == 0U ||
        supply_mv < CHASSIS_ACTUATOR_SUPPLY_MIN_MV ||
        supply_mv > CHASSIS_ACTUATOR_SUPPLY_MAX_MV ||
        s_supply_stale_cycles > CHASSIS_ACTUATOR_SUPPLY_STALE_CYCLES) {
        return false;
    }

    scale_permille = (int32_t) (
        (profile->control_reference_voltage_mv * 1000U + supply_mv / 2U) /
        supply_mv);
    left_compensated = ((int32_t) left_electrical * scale_permille +
        ((left_electrical < 0) ? -500 : 500)) / 1000;
    right_compensated = ((int32_t) right_electrical * scale_permille +
        ((right_electrical < 0) ? -500 : 500)) / 1000;
    if (left_compensated < -CHASSIS_ACTUATOR_COMPENSATED_MAX_PERMILLE ||
        left_compensated > CHASSIS_ACTUATOR_COMPENSATED_MAX_PERMILLE ||
        right_compensated < -CHASSIS_ACTUATOR_COMPENSATED_MAX_PERMILLE ||
        right_compensated > CHASSIS_ACTUATOR_COMPENSATED_MAX_PERMILLE) {
        return false;
    }
    left_electrical = (int16_t) left_compensated;
    right_electrical = (int16_t) right_compensated;
    if (!BSP_Motor_SetFastDecay(BSP_MOTOR_LEFT, left_electrical) ||
        !BSP_Motor_SetFastDecay(BSP_MOTOR_RIGHT, right_electrical)) {
        return false;
    }
    g_chassis_actuator_diag.normalized_left_permille = left_normalized;
    g_chassis_actuator_diag.normalized_right_permille = right_normalized;
    g_chassis_actuator_diag.compensated_left_permille = (int16_t) (
        left_electrical * profile->wheel[MOTOR_WHEEL_LEFT].motor_output_sign);
    g_chassis_actuator_diag.compensated_right_permille = (int16_t) (
        right_electrical * profile->wheel[MOTOR_WHEEL_RIGHT].motor_output_sign);
    g_chassis_actuator_diag.applied_left_permille = left_electrical;
    g_chassis_actuator_diag.applied_right_permille = right_electrical;
    g_chassis_actuator_diag.supply_voltage_mv = supply_mv;
    g_chassis_actuator_diag.voltage_scale_permille =
        (uint16_t) scale_permille;
    g_chassis_actuator_diag.supply_stale_cycles = s_supply_stale_cycles;
    g_chassis_actuator_diag.voltage_compensation_active = 1U;
    return true;
}

void ChassisActuator_ForceSafe(chassis_actuator_stop_reason_t reason)
{
    BSP_Motor_ForceSafe();
    WheelSpeedController_Reset(
        &s_speed_controller[MOTOR_WHEEL_LEFT]);
    WheelSpeedController_Reset(
        &s_speed_controller[MOTOR_WHEEL_RIGHT]);
    s_common_low_speed_cycles = 0U;
    s_common_recovery_remaining = 0U;
    s_consecutive_failed_recoveries = 0U;
    s_recovery_candidate_mask = 0U;
    s_recovery_active_mask = 0U;
    s_crawl_drive_mask = 0U;
    memset(s_crawl_kick_remaining, 0, sizeof(s_crawl_kick_remaining));
    memset(s_crawl_rest_remaining, 0, sizeof(s_crawl_rest_remaining));
    s_continuous_speed = false;
    s_heading_pivot_mode = false;
    s_heading_pivot_settled = false;
    s_motion_brake_remaining = 0U;
    s_heading_pivot_command_deg = 0.0f;
    s_heading_pivot_progress_deg = 0.0f;
    s_heading_pivot_last_yaw_deg = 0.0f;
    s_heading_stall_cycles = 0U;
    s_distance_settled = false;
    s_distance_direction = 0;
    s_caster_reversal_active = false;
    s_distance_heading_reference_deg = 0.0f;
    DistanceController_Stop(&s_distance_controller);
    s_heading_controller.config.kp_rpm_per_deg =
        CHASSIS_ACTUATOR_HEADING_KP_RPM_PER_DEG;
    s_heading_controller.config.kd_rpm_per_dps =
        CHASSIS_ACTUATOR_HEADING_KD_RPM_PER_DPS;
    s_heading_controller.config.maximum_correction_rpm =
        CHASSIS_ACTUATOR_HEADING_MAX_CORRECTION_RPM;
    HeadingController_Stop(&s_heading_controller);
    g_chassis_actuator_diag.remaining_control_cycles = 0U;
    g_chassis_actuator_diag.boost_elapsed_cycles = 0U;
    g_chassis_actuator_diag.applied_left_permille = 0;
    g_chassis_actuator_diag.applied_right_permille = 0;
    g_chassis_actuator_diag.normalized_left_permille = 0;
    g_chassis_actuator_diag.normalized_right_permille = 0;
    g_chassis_actuator_diag.compensated_left_permille = 0;
    g_chassis_actuator_diag.compensated_right_permille = 0;
    g_chassis_actuator_diag.left_target_deci_rpm = 0;
    g_chassis_actuator_diag.right_target_deci_rpm = 0;
    /* Keep the last mode so post-stop telemetry retains its field semantics. */
    g_chassis_actuator_diag.speed_phase =
        (uint8_t) CHASSIS_SPEED_PHASE_IDLE;
    g_chassis_actuator_diag.armed = 0U;
    g_chassis_actuator_diag.output_permitted = 0U;
    g_chassis_actuator_diag.continuous_speed = 0U;
    g_chassis_actuator_diag.voltage_compensation_active = 0U;
    g_chassis_actuator_diag.last_stop_reason = (uint8_t) reason;
    s_pending_valid = false;
    ChassisActuator_UpdateControllerDiagnostics();
    ChassisActuator_UpdateDistanceDiagnostics();
}

void ChassisActuator_Init(void)
{
    const motor_profile_t *profile = MotorProfile_GetActive();
    distance_controller_config_t distance_config;

    memset((void *) &s_pending_request, 0, sizeof(s_pending_request));
    memset((void *) &g_chassis_actuator_diag, 0,
        sizeof(g_chassis_actuator_diag));
    s_pending_valid = false;
    s_continuous_speed = false;
    s_last_supply_sequence = 0U;
    s_supply_stale_cycles = 0U;
    s_last_distance_direction = 0;
    HeadingController_Init(&s_heading_controller, &s_heading_config);
    memset(&distance_config, 0, sizeof(distance_config));
    distance_config.wheel_circumference_mm =
        CHASSIS_ACTUATOR_WHEEL_CIRCUMFERENCE_MM;
    distance_config.left_counts_per_revolution =
        profile->wheel[MOTOR_WHEEL_LEFT].counts_per_output_revolution;
    distance_config.right_counts_per_revolution =
        profile->wheel[MOTOR_WHEEL_RIGHT].counts_per_output_revolution;
    distance_config.left_encoder_count_sign =
        profile->wheel[MOTOR_WHEEL_LEFT].encoder_count_sign;
    distance_config.right_encoder_count_sign =
        profile->wheel[MOTOR_WHEEL_RIGHT].encoder_count_sign;
    distance_config.departure_minimum_rpm =
        CHASSIS_ACTUATOR_DISTANCE_DEPARTURE_MIN_RPM;
    distance_config.departure_gain_rpm_per_mm =
        CHASSIS_ACTUATOR_DISTANCE_DEPARTURE_GAIN;
    distance_config.approach_minimum_rpm =
        CHASSIS_ACTUATOR_DISTANCE_APPROACH_MIN_RPM;
    distance_config.approach_gain_rpm_per_mm =
        CHASSIS_ACTUATOR_DISTANCE_APPROACH_GAIN;
    DistanceController_Init(&s_distance_controller, &distance_config);
    ChassisActuator_InitSpeedControllers();
    BSP_Motor_Init();
    ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_NONE);
    g_chassis_actuator_diag.initialized = 1U;
}

chassis_actuator_command_status_t ChassisActuator_StageDebugRequest(
    const chassis_actuator_debug_request_t *request)
{
    chassis_actuator_command_status_t status =
        ChassisActuator_ValidateRequest(request);

    if (status != CHASSIS_ACTUATOR_COMMAND_STAGED) {
        g_chassis_actuator_diag.rejected_request_count++;
        return status;
    }

    taskENTER_CRITICAL();
    if (request->sequence ==
        g_chassis_actuator_diag.last_request_sequence) {
        status = CHASSIS_ACTUATOR_COMMAND_DUPLICATE;
    } else if (s_pending_valid) {
        status = CHASSIS_ACTUATOR_COMMAND_BUSY;
    } else if (g_chassis_actuator_diag.output_permitted != 0U &&
        !(request->reserved == (uint16_t) CHASSIS_ACTUATOR_MODE_SPEED &&
          ((g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_SPEED) ||
           (request->left_electrical_permille == 0 &&
            request->right_electrical_permille == 0)))) {
        status = CHASSIS_ACTUATOR_COMMAND_BUSY;
    } else {
        s_pending_request = *request;
        s_pending_valid = true;
        g_chassis_actuator_diag.armed = 1U;
    }
    taskEXIT_CRITICAL();

    if (status != CHASSIS_ACTUATOR_COMMAND_STAGED) {
        g_chassis_actuator_diag.rejected_request_count++;
    }
    return status;
}

static bool ChassisActuator_WheelNeedsRecovery(
    float target_rpm, float measured_rpm, int16_t normalized_output)
{
    return target_rpm != 0.0f &&
        (!ChassisActuator_SameDirection(target_rpm, measured_rpm) ||
         ChassisActuator_AbsFloat(measured_rpm) <
            CHASSIS_ACTUATOR_RECOVERY_RPM) &&
        ChassisActuator_AbsPermille(normalized_output) >=
            CHASSIS_ACTUATOR_RECOVERY_OUTPUT;
}

static bool ChassisActuator_UpdateSpeed(
    float left_measured_rpm, float right_measured_rpm,
    uint32_t period_us)
{
    const motor_profile_t *profile = MotorProfile_GetActive();
    const motor_speed_pid_config_t *speed =
        MotorProfile_GetSpeedPidConfig();
    float left_target =
        (float) g_chassis_actuator_diag.left_target_deci_rpm * 0.1f;
    float right_target =
        (float) g_chassis_actuator_diag.right_target_deci_rpm * 0.1f;
    uint16_t minimum_boost_cycles = (uint16_t) (
        (speed->boost_minimum_ms + CHASSIS_ACTUATOR_CONTROL_PERIOD_MS - 1U) /
        CHASSIS_ACTUATOR_CONTROL_PERIOD_MS);
    uint16_t maximum_boost_cycles = (uint16_t) (
        (speed->boost_maximum_ms + CHASSIS_ACTUATOR_CONTROL_PERIOD_MS - 1U) /
        CHASSIS_ACTUATOR_CONTROL_PERIOD_MS);
    bool left_ready;
    bool right_ready;
    int16_t left_output;
    int16_t right_output;

    s_speed_controller[MOTOR_WHEEL_LEFT].config.kp =
        g_control_tuning_params.kp;
    s_speed_controller[MOTOR_WHEEL_LEFT].config.ki =
        g_control_tuning_params.ki;
    s_speed_controller[MOTOR_WHEEL_LEFT].config.kd =
        g_control_tuning_params.kd;
    s_speed_controller[MOTOR_WHEEL_RIGHT].config.kp =
        g_control_tuning_params.kp;
    s_speed_controller[MOTOR_WHEEL_RIGHT].config.ki =
        g_control_tuning_params.ki;
    s_speed_controller[MOTOR_WHEEL_RIGHT].config.kd =
        g_control_tuning_params.kd;

    if (s_motion_brake_remaining != 0U) {
        WheelSpeedController_Reset(
            &s_speed_controller[MOTOR_WHEEL_LEFT]);
        WheelSpeedController_Reset(
            &s_speed_controller[MOTOR_WHEEL_RIGHT]);
        if (!BSP_Motor_Brake(BSP_MOTOR_LEFT) ||
            !BSP_Motor_Brake(BSP_MOTOR_RIGHT)) {
            return false;
        }
        s_motion_brake_remaining--;
        g_chassis_actuator_diag.normalized_left_permille = 0;
        g_chassis_actuator_diag.normalized_right_permille = 0;
        g_chassis_actuator_diag.compensated_left_permille = 0;
        g_chassis_actuator_diag.compensated_right_permille = 0;
        g_chassis_actuator_diag.applied_left_permille = 0;
        g_chassis_actuator_diag.applied_right_permille = 0;
        ChassisActuator_UpdateControllerDiagnostics();
        return true;
    }

    if (g_chassis_actuator_diag.speed_phase ==
        (uint8_t) CHASSIS_SPEED_PHASE_BOOST) {
        WheelSpeedController_Observe(
            &s_speed_controller[MOTOR_WHEEL_LEFT], left_measured_rpm);
        WheelSpeedController_Observe(
            &s_speed_controller[MOTOR_WHEEL_RIGHT], right_measured_rpm);
        g_chassis_actuator_diag.boost_elapsed_cycles++;
        left_ready = (left_target == 0.0f) ||
            (ChassisActuator_SameDirection(left_target, left_measured_rpm) &&
             ChassisActuator_AbsFloat(left_measured_rpm) >=
                 ChassisActuator_BoostReleaseRpm(left_target));
        right_ready = (right_target == 0.0f) ||
            (ChassisActuator_SameDirection(right_target, right_measured_rpm) &&
             ChassisActuator_AbsFloat(right_measured_rpm) >=
                 ChassisActuator_BoostReleaseRpm(right_target));
        if (g_chassis_actuator_diag.boost_elapsed_cycles >=
                minimum_boost_cycles && left_ready && right_ready) {
            ChassisActuator_PrimeTracking(
                MOTOR_WHEEL_LEFT, left_target, left_measured_rpm);
            ChassisActuator_PrimeTracking(
                MOTOR_WHEEL_RIGHT, right_target, right_measured_rpm);
            g_chassis_actuator_diag.speed_phase =
                (uint8_t) CHASSIS_SPEED_PHASE_TRACKING;
            g_chassis_actuator_diag.boost_complete_count++;
        } else if (g_chassis_actuator_diag.boost_elapsed_cycles >=
            maximum_boost_cycles) {
            g_chassis_actuator_diag.stall_stop_count++;
            ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_STALL);
            return false;
        } else {
            left_output = ChassisActuator_SignedPermille(
                left_target, profile->start_pwm_permille);
            right_output = ChassisActuator_SignedPermille(
                right_target, profile->start_pwm_permille);
            return ChassisActuator_ApplyNormalized(
                left_output, right_output);
        }
    }

    if (g_chassis_actuator_diag.control_mode ==
            (uint8_t) CHASSIS_ACTUATOR_MODE_HEADING ||
        g_chassis_actuator_diag.control_mode ==
            (uint8_t) CHASSIS_ACTUATOR_MODE_DISTANCE) {
        bool heading_stalled;

        s_common_low_speed_cycles = 0U;
        s_common_recovery_remaining = 0U;
        s_recovery_candidate_mask = 0U;
        s_recovery_active_mask = 0U;
        left_output = WheelSpeedController_Update(
            &s_speed_controller[MOTOR_WHEEL_LEFT], left_target,
            left_measured_rpm, (float) period_us / 1000000.0f);
        right_output = WheelSpeedController_Update(
            &s_speed_controller[MOTOR_WHEEL_RIGHT], right_target,
            right_measured_rpm, (float) period_us / 1000000.0f);
        heading_stalled =
            ChassisActuator_WheelNeedsRecovery(
                left_target, left_measured_rpm, left_output) ||
            ChassisActuator_WheelNeedsRecovery(
                right_target, right_measured_rpm, right_output);
        if (heading_stalled) {
            if (s_heading_stall_cycles < UINT16_MAX) {
                s_heading_stall_cycles++;
            }
        } else {
            s_heading_stall_cycles = 0U;
        }
        if (s_heading_stall_cycles >=
            CHASSIS_ACTUATOR_HEADING_STALL_CYCLES) {
            g_chassis_actuator_diag.stall_stop_count++;
            ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_STALL);
            return false;
        }
    } else if (s_common_recovery_remaining != 0U) {
        uint16_t recovery_elapsed_cycles =
            CHASSIS_ACTUATOR_RECOVERY_BOOST_CYCLES -
            s_common_recovery_remaining;
        bool left_moving;
        bool right_moving;
        bool left_recovering =
            (s_recovery_active_mask & 0x01U) != 0U;
        bool right_recovering =
            (s_recovery_active_mask & 0x02U) != 0U;

        if (recovery_elapsed_cycles >=
                CHASSIS_ACTUATOR_RECOVERY_MIN_CYCLES) {
            if (left_recovering &&
                ChassisActuator_SameDirection(
                    left_target, left_measured_rpm) &&
                ChassisActuator_AbsFloat(left_measured_rpm) >=
                    ChassisActuator_RecoveryReleaseRpm(left_target)) {
                s_recovery_active_mask &= (uint8_t) ~0x01U;
                left_recovering = false;
                ChassisActuator_PrimeTracking(
                    MOTOR_WHEEL_LEFT, left_target, left_measured_rpm);
            }
            if (right_recovering &&
                ChassisActuator_SameDirection(
                    right_target, right_measured_rpm) &&
                ChassisActuator_AbsFloat(right_measured_rpm) >=
                    ChassisActuator_RecoveryReleaseRpm(right_target)) {
                s_recovery_active_mask &= (uint8_t) ~0x02U;
                right_recovering = false;
                ChassisActuator_PrimeTracking(
                    MOTOR_WHEEL_RIGHT, right_target, right_measured_rpm);
            }
        }

        if (left_recovering) {
            WheelSpeedController_Observe(
                &s_speed_controller[MOTOR_WHEEL_LEFT], left_measured_rpm);
            left_output = ChassisActuator_SignedPermille(
                left_target, speed->recovery_boost_permille);
        } else {
            left_output = WheelSpeedController_Update(
                &s_speed_controller[MOTOR_WHEEL_LEFT], left_target,
                left_measured_rpm, (float) period_us / 1000000.0f);
        }
        if (right_recovering) {
            WheelSpeedController_Observe(
                &s_speed_controller[MOTOR_WHEEL_RIGHT], right_measured_rpm);
            right_output = ChassisActuator_SignedPermille(
                right_target, speed->recovery_boost_permille);
        } else {
            right_output = WheelSpeedController_Update(
                &s_speed_controller[MOTOR_WHEEL_RIGHT], right_target,
                right_measured_rpm, (float) period_us / 1000000.0f);
        }

        if (s_recovery_active_mask == 0U) {
            s_common_recovery_remaining = 0U;
            s_consecutive_failed_recoveries = 0U;
        } else {
            s_common_recovery_remaining--;
        }
        if (s_common_recovery_remaining == 0U &&
            s_recovery_active_mask != 0U) {
            left_moving = !left_recovering ||
                (left_target == 0.0f) ||
                (ChassisActuator_SameDirection(
                    left_target, left_measured_rpm) &&
                 ChassisActuator_AbsFloat(left_measured_rpm) >=
                    CHASSIS_ACTUATOR_RECOVERY_RPM);
            right_moving = !right_recovering ||
                (right_target == 0.0f) ||
                (ChassisActuator_SameDirection(
                    right_target, right_measured_rpm) &&
                 ChassisActuator_AbsFloat(right_measured_rpm) >=
                    CHASSIS_ACTUATOR_RECOVERY_RPM);
            if (left_moving && right_moving) {
                s_consecutive_failed_recoveries = 0U;
            } else {
                s_consecutive_failed_recoveries++;
                if (s_consecutive_failed_recoveries >=
                    CHASSIS_ACTUATOR_MAX_RECOVERY_ATTEMPTS) {
                    g_chassis_actuator_diag.recovery_failed_count++;
                    g_chassis_actuator_diag.stall_stop_count++;
                    ChassisActuator_ForceSafe(
                        CHASSIS_ACTUATOR_STOP_STALL);
                    return false;
                }
            }
            if ((s_recovery_active_mask & 0x01U) != 0U) {
                ChassisActuator_PrimeTracking(
                    MOTOR_WHEEL_LEFT, left_target, left_measured_rpm);
            }
            if ((s_recovery_active_mask & 0x02U) != 0U) {
                ChassisActuator_PrimeTracking(
                    MOTOR_WHEEL_RIGHT, right_target, right_measured_rpm);
            }
            s_recovery_active_mask = 0U;
        }
    } else {
        uint8_t needs_recovery_mask = 0U;
        bool left_crawl = ChassisActuator_IsCrawlTarget(left_target);
        bool right_crawl = ChassisActuator_IsCrawlTarget(right_target);

        left_output = left_crawl ?
            ChassisActuator_UpdateCrawlWheel(
                MOTOR_WHEEL_LEFT, left_target, left_measured_rpm) :
            WheelSpeedController_Update(
                &s_speed_controller[MOTOR_WHEEL_LEFT], left_target,
                left_measured_rpm, (float) period_us / 1000000.0f);
        right_output = right_crawl ?
            ChassisActuator_UpdateCrawlWheel(
                MOTOR_WHEEL_RIGHT, right_target, right_measured_rpm) :
            WheelSpeedController_Update(
                &s_speed_controller[MOTOR_WHEEL_RIGHT], right_target,
                right_measured_rpm, (float) period_us / 1000000.0f);
        if (!left_crawl && ChassisActuator_WheelNeedsRecovery(left_target,
                left_measured_rpm, left_output)) {
            needs_recovery_mask |= 0x01U;
        }
        if (!right_crawl && ChassisActuator_WheelNeedsRecovery(right_target,
                right_measured_rpm, right_output)) {
            needs_recovery_mask |= 0x02U;
        }
        if (needs_recovery_mask != 0U) {
            s_recovery_candidate_mask |= needs_recovery_mask;
            if (s_common_low_speed_cycles < UINT16_MAX) {
                s_common_low_speed_cycles++;
            }
        } else {
            s_common_low_speed_cycles = 0U;
            s_recovery_candidate_mask = 0U;
        }
        if (s_common_low_speed_cycles >=
            CHASSIS_ACTUATOR_LOW_SPEED_CYCLES) {
            s_common_low_speed_cycles = 0U;
            s_common_recovery_remaining =
                CHASSIS_ACTUATOR_RECOVERY_BOOST_CYCLES;
            s_recovery_active_mask = s_recovery_candidate_mask;
            s_recovery_candidate_mask = 0U;
            if ((s_recovery_active_mask & 0x01U) != 0U) {
                left_output = ChassisActuator_SignedPermille(
                    left_target, speed->recovery_boost_permille);
                s_speed_controller[MOTOR_WHEEL_LEFT].integrator_permille =
                    0.0f;
                WheelSpeedController_PrimeOutput(
                    &s_speed_controller[MOTOR_WHEEL_LEFT],
                    (float) left_output);
            }
            if ((s_recovery_active_mask & 0x02U) != 0U) {
                right_output = ChassisActuator_SignedPermille(
                    right_target, speed->recovery_boost_permille);
                s_speed_controller[MOTOR_WHEEL_RIGHT].integrator_permille =
                    0.0f;
                WheelSpeedController_PrimeOutput(
                    &s_speed_controller[MOTOR_WHEEL_RIGHT],
                    (float) right_output);
            }
            g_chassis_actuator_diag.recovery_boost_count++;
        }
    }
    if (!ChassisActuator_ApplyNormalized(left_output, right_output)) {
        return false;
    }
    g_chassis_actuator_diag.speed_update_count++;
    return true;
}

void ChassisActuator_ServiceAtControlBoundary(
    bool schedule_resynchronized, int32_t left_delta_counts,
    int32_t right_delta_counts, float left_measured_rpm,
    float right_measured_rpm, uint32_t period_us)
{
    chassis_actuator_debug_request_t request;
    uint16_t cycles;
    uint8_t retarget_start_mask;
    bool pending;

    g_chassis_actuator_diag.left_measured_rpm = left_measured_rpm;
    g_chassis_actuator_diag.right_measured_rpm = right_measured_rpm;
    ChassisActuator_UpdateControllerDiagnostics();

    if (schedule_resynchronized) {
        if (g_chassis_actuator_diag.output_permitted != 0U) {
            g_chassis_actuator_diag.timing_stop_count++;
        }
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_TIMING);
        return;
    }

    taskENTER_CRITICAL();
    pending = s_pending_valid;
    if (pending) {
        request = s_pending_request;
        s_pending_valid = false;
    }
    taskEXIT_CRITICAL();

    if (pending && request.reserved ==
            (uint16_t) CHASSIS_ACTUATOR_MODE_SPEED &&
        request.left_electrical_permille == 0 &&
        request.right_electrical_permille == 0) {
        g_chassis_actuator_diag.accepted_request_count++;
        g_chassis_actuator_diag.speed_retarget_count++;
        g_chassis_actuator_diag.last_request_sequence = request.sequence;
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_COMPLETE);
        g_chassis_actuator_diag.last_request_sequence = request.sequence;
        return;
    }

    if (pending && g_chassis_actuator_diag.output_permitted != 0U) {
        if (g_chassis_actuator_diag.control_mode !=
                (uint8_t) CHASSIS_ACTUATOR_MODE_SPEED ||
            request.reserved !=
                (uint16_t) CHASSIS_ACTUATOR_MODE_SPEED ||
            ChassisActuator_ValidateRequest(&request) !=
                CHASSIS_ACTUATOR_COMMAND_STAGED) {
            g_chassis_actuator_diag.rejected_request_count++;
            g_chassis_actuator_diag.last_request_sequence =
                request.sequence;
            ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_REJECTED);
            return;
        }

        retarget_start_mask = 0U;
        if (g_chassis_actuator_diag.left_target_deci_rpm == 0 &&
            request.left_electrical_permille != 0) {
            retarget_start_mask |= 0x01U;
            WheelSpeedController_Reset(
                &s_speed_controller[MOTOR_WHEEL_LEFT]);
        }
        if (g_chassis_actuator_diag.right_target_deci_rpm == 0 &&
            request.right_electrical_permille != 0) {
            retarget_start_mask |= 0x02U;
            WheelSpeedController_Reset(
                &s_speed_controller[MOTOR_WHEEL_RIGHT]);
        }
        s_continuous_speed = (request.duration_ms == 0U);
        cycles = s_continuous_speed ? 1U : (uint16_t) (
            (request.duration_ms + CHASSIS_ACTUATOR_CONTROL_PERIOD_MS - 1U) /
            CHASSIS_ACTUATOR_CONTROL_PERIOD_MS);
        g_chassis_actuator_diag.left_target_deci_rpm =
            request.left_electrical_permille;
        g_chassis_actuator_diag.right_target_deci_rpm =
            request.right_electrical_permille;
        g_chassis_actuator_diag.remaining_control_cycles = cycles;
        g_chassis_actuator_diag.last_request_sequence = request.sequence;
        g_chassis_actuator_diag.last_stop_reason =
            (uint8_t) CHASSIS_ACTUATOR_STOP_NONE;
        g_chassis_actuator_diag.accepted_request_count++;
        g_chassis_actuator_diag.speed_retarget_count++;
        g_chassis_actuator_diag.armed = 1U;
        g_chassis_actuator_diag.continuous_speed =
            s_continuous_speed ? 1U : 0U;
        s_common_low_speed_cycles = 0U;
        s_common_recovery_remaining = (retarget_start_mask != 0U) ?
            CHASSIS_ACTUATOR_RECOVERY_BOOST_CYCLES : 0U;
        s_consecutive_failed_recoveries = 0U;
        s_recovery_candidate_mask = 0U;
        s_recovery_active_mask = retarget_start_mask;
        if (retarget_start_mask != 0U) {
            g_chassis_actuator_diag.recovery_boost_count++;
        }
        s_crawl_drive_mask = 0x03U;
        s_heading_stall_cycles = 0U;
        memset(s_crawl_kick_remaining, 0,
            sizeof(s_crawl_kick_remaining));
        memset(s_crawl_rest_remaining, 0,
            sizeof(s_crawl_rest_remaining));
        pending = false;
    }

    if (g_chassis_actuator_diag.remaining_control_cycles != 0U) {
        if (!s_continuous_speed) {
            g_chassis_actuator_diag.remaining_control_cycles--;
            if (g_chassis_actuator_diag.remaining_control_cycles == 0U) {
                chassis_actuator_stop_reason_t reason =
                    (g_chassis_actuator_diag.control_mode ==
                        (uint8_t) CHASSIS_ACTUATOR_MODE_DISTANCE) ?
                    CHASSIS_ACTUATOR_STOP_DISTANCE_TIMEOUT :
                    CHASSIS_ACTUATOR_STOP_COMPLETE;

                ChassisActuator_ForceSafe(reason);
                g_chassis_actuator_diag.completed_pulse_count++;
                return;
            }
        }
        if (g_chassis_actuator_diag.control_mode ==
            (uint8_t) CHASSIS_ACTUATOR_MODE_DISTANCE) {
            bool reached = DistanceController_Update(
                &s_distance_controller, left_delta_counts,
                right_delta_counts);

            ChassisActuator_UpdateDistanceDiagnostics();
            if (reached && !s_distance_settled) {
                s_distance_settled = true;
                s_motion_brake_remaining =
                    CHASSIS_ACTUATOR_PIVOT_BRAKE_CYCLES;
                g_chassis_actuator_diag.left_target_deci_rpm = 0;
                g_chassis_actuator_diag.right_target_deci_rpm = 0;
            }
            if (s_distance_settled &&
                s_motion_brake_remaining == 0U) {
                g_chassis_actuator_diag.distance_complete_count++;
                ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_COMPLETE);
                g_chassis_actuator_diag.completed_pulse_count++;
                return;
            }
        }
        if ((g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_HEADING ||
             g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_DISTANCE) &&
            !s_distance_settled &&
            !ChassisActuator_UpdateHeadingTargets()) {
            g_chassis_actuator_diag.rejected_request_count++;
            ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_REJECTED);
            return;
        }
        if (g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_SPEED ||
            g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_HEADING ||
            g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_DISTANCE) {
            if (!ChassisActuator_UpdateSpeed(
                    left_measured_rpm, right_measured_rpm, period_us) &&
                g_chassis_actuator_diag.output_permitted != 0U) {
                g_chassis_actuator_diag.rejected_request_count++;
                ChassisActuator_ForceSafe(
                    CHASSIS_ACTUATOR_STOP_REJECTED);
            }
            ChassisActuator_UpdateControllerDiagnostics();
            return;
        }
    }

    if (!pending) {
        return;
    }

    if (g_chassis_actuator_diag.output_permitted != 0U ||
        ChassisActuator_ValidateRequest(&request) !=
            CHASSIS_ACTUATOR_COMMAND_STAGED) {
        g_chassis_actuator_diag.rejected_request_count++;
        g_chassis_actuator_diag.last_request_sequence = request.sequence;
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_REJECTED);
        return;
    }

    s_continuous_speed = request.reserved ==
        (uint16_t) CHASSIS_ACTUATOR_MODE_SPEED && request.duration_ms == 0U;
    cycles = s_continuous_speed ? 1U : (uint16_t) (
        (request.duration_ms + CHASSIS_ACTUATOR_CONTROL_PERIOD_MS - 1U) /
        CHASSIS_ACTUATOR_CONTROL_PERIOD_MS);
    g_chassis_actuator_diag.control_mode = (uint8_t) request.reserved;
    if (request.reserved ==
        (uint16_t) CHASSIS_ACTUATOR_MODE_ELECTRICAL) {
        if (!BSP_Motor_SetFastDecay(
                BSP_MOTOR_LEFT, request.left_electrical_permille) ||
            !BSP_Motor_SetFastDecay(
                BSP_MOTOR_RIGHT, request.right_electrical_permille)) {
            g_chassis_actuator_diag.rejected_request_count++;
            g_chassis_actuator_diag.last_request_sequence = request.sequence;
            ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_REJECTED);
            return;
        }
        g_chassis_actuator_diag.applied_left_permille =
            request.left_electrical_permille;
        g_chassis_actuator_diag.applied_right_permille =
            request.right_electrical_permille;
    } else {
        float left_target =
            (float) request.left_electrical_permille * 0.1f;
        float right_target;
        int16_t left_boost;
        int16_t right_boost;

        if (request.reserved ==
                (uint16_t) CHASSIS_ACTUATOR_MODE_HEADING ||
            request.reserved ==
                (uint16_t) CHASSIS_ACTUATOR_MODE_DISTANCE) {
            attitude_estimator_snapshot_t attitude;
            bool distance_mode = request.reserved ==
                (uint16_t) CHASSIS_ACTUATOR_MODE_DISTANCE;
            float heading_delta_deg = distance_mode ? 0.0f :
                (float) request.right_electrical_permille * 0.1f;

            if (!AttitudeEstimator_GetSnapshot(&attitude) ||
                (attitude.flags &
                    ATTITUDE_ESTIMATOR_FLAG_SOURCE_VALID) == 0U) {
                g_chassis_actuator_diag.rejected_request_count++;
                g_chassis_actuator_diag.last_request_sequence =
                    request.sequence;
                ChassisActuator_ForceSafe(
                    CHASSIS_ACTUATOR_STOP_REJECTED);
                return;
            }
            s_heading_pivot_mode = !distance_mode &&
                ChassisActuator_AbsFloat(left_target) < 0.1f;
            s_heading_pivot_settled = false;
            s_distance_settled = false;
            s_motion_brake_remaining = 0U;
            s_heading_controller.config.kp_rpm_per_deg =
                s_heading_pivot_mode ?
                    CHASSIS_ACTUATOR_PIVOT_KP_RPM_PER_DEG :
                    CHASSIS_ACTUATOR_HEADING_KP_RPM_PER_DEG;
            s_heading_controller.config.kd_rpm_per_dps =
                s_heading_pivot_mode ?
                    CHASSIS_ACTUATOR_PIVOT_KD_RPM_PER_DPS :
                    CHASSIS_ACTUATOR_HEADING_KD_RPM_PER_DPS;
            s_heading_controller.config.maximum_correction_rpm =
                s_heading_pivot_mode ? CHASSIS_ACTUATOR_PIVOT_MAX_RPM :
                    CHASSIS_ACTUATOR_HEADING_MAX_CORRECTION_RPM;
            if (s_heading_pivot_mode) {
                s_heading_pivot_command_deg = heading_delta_deg;
                s_heading_pivot_progress_deg = 0.0f;
                s_heading_pivot_last_yaw_deg = attitude.yaw_deg;
            }
            if (distance_mode) {
                s_distance_direction = (left_target < 0.0f) ? -1 : 1;
                s_caster_reversal_active =
                    s_last_distance_direction != 0 &&
                    s_distance_direction != s_last_distance_direction;
                s_last_distance_direction = s_distance_direction;
                s_distance_heading_reference_deg = attitude.yaw_deg;
                if (!DistanceController_Start(&s_distance_controller,
                        left_target,
                        (float) request.right_electrical_permille)) {
                    g_chassis_actuator_diag.rejected_request_count++;
                    g_chassis_actuator_diag.last_request_sequence =
                        request.sequence;
                    ChassisActuator_ForceSafe(
                        CHASSIS_ACTUATOR_STOP_REJECTED);
                    return;
                }
                ChassisActuator_UpdateDistanceDiagnostics();
                left_target = DistanceController_GetTargetSpeedRpm(
                    &s_distance_controller);
            }
            HeadingController_Start(&s_heading_controller, left_target,
                attitude.yaw_deg + heading_delta_deg);
            if (!ChassisActuator_UpdateHeadingTargets()) {
                g_chassis_actuator_diag.rejected_request_count++;
                g_chassis_actuator_diag.last_request_sequence =
                    request.sequence;
                ChassisActuator_ForceSafe(
                    CHASSIS_ACTUATOR_STOP_REJECTED);
                return;
            }
            left_target = s_heading_controller.left_target_rpm;
            right_target = s_heading_controller.right_target_rpm;
        } else {
            right_target =
                (float) request.right_electrical_permille * 0.1f;
        }
        left_boost = ChassisActuator_SignedPermille(
            left_target, MotorProfile_GetActive()->start_pwm_permille);
        right_boost = ChassisActuator_SignedPermille(
            right_target, MotorProfile_GetActive()->start_pwm_permille);

        WheelSpeedController_Reset(
            &s_speed_controller[MOTOR_WHEEL_LEFT]);
        WheelSpeedController_Reset(
            &s_speed_controller[MOTOR_WHEEL_RIGHT]);
        s_common_low_speed_cycles = 0U;
        s_common_recovery_remaining = 0U;
        s_consecutive_failed_recoveries = 0U;
        s_recovery_candidate_mask = 0U;
        s_recovery_active_mask = 0U;
        s_crawl_drive_mask = 0x03U;
        memset(s_crawl_kick_remaining, 0,
            sizeof(s_crawl_kick_remaining));
        memset(s_crawl_rest_remaining, 0,
            sizeof(s_crawl_rest_remaining));
        if (request.reserved !=
                (uint16_t) CHASSIS_ACTUATOR_MODE_HEADING &&
            request.reserved !=
                (uint16_t) CHASSIS_ACTUATOR_MODE_DISTANCE) {
            g_chassis_actuator_diag.left_target_deci_rpm =
                request.left_electrical_permille;
            g_chassis_actuator_diag.right_target_deci_rpm =
                request.right_electrical_permille;
        }
        g_chassis_actuator_diag.boost_elapsed_cycles = 0U;
        g_chassis_actuator_diag.speed_phase =
            (uint8_t) CHASSIS_SPEED_PHASE_BOOST;
        if (!ChassisActuator_ApplyNormalized(left_boost, right_boost)) {
            g_chassis_actuator_diag.rejected_request_count++;
            g_chassis_actuator_diag.last_request_sequence = request.sequence;
            ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_REJECTED);
            return;
        }
    }

    g_chassis_actuator_diag.accepted_request_count++;
    g_chassis_actuator_diag.last_request_sequence = request.sequence;
    g_chassis_actuator_diag.remaining_control_cycles = cycles;
    g_chassis_actuator_diag.last_stop_reason =
        (uint8_t) CHASSIS_ACTUATOR_STOP_NONE;
    g_chassis_actuator_diag.armed = 1U;
    g_chassis_actuator_diag.output_permitted = 1U;
    g_chassis_actuator_diag.continuous_speed =
        s_continuous_speed ? 1U : 0U;
    ChassisActuator_UpdateControllerDiagnostics();
}

void RtosFault_EmergencyStop(void)
{
    if (g_chassis_actuator_diag.initialized != 0U) {
        g_chassis_actuator_diag.emergency_stop_count++;
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_EMERGENCY);
    }
}
