#include "attitude_estimator.h"

#include <math.h>
#include <string.h>

#if defined(ATTITUDE_ESTIMATOR_HOST_TEST)
#define ATTITUDE_ESTIMATOR_ENTER_CRITICAL()
#define ATTITUDE_ESTIMATOR_EXIT_CRITICAL()
#else
#include "FreeRTOS.h"
#include "task.h"
#define ATTITUDE_ESTIMATOR_ENTER_CRITICAL() taskENTER_CRITICAL()
#define ATTITUDE_ESTIMATOR_EXIT_CRITICAL()  taskEXIT_CRITICAL()
#endif

#define ATTITUDE_ESTIMATOR_PI                  3.14159265358979323846f
#define ATTITUDE_ESTIMATOR_RAD_TO_DEG          (180.0f / ATTITUDE_ESTIMATOR_PI)
#define ATTITUDE_ESTIMATOR_DEG_TO_RAD          (ATTITUDE_ESTIMATOR_PI / 180.0f)
#define ATTITUDE_ESTIMATOR_CORRECTION_GAIN     1.5f
#define ATTITUDE_ESTIMATOR_ACCEL_FULL_ERROR_G  0.05f
#define ATTITUDE_ESTIMATOR_ACCEL_REJECT_ERROR_G 0.15f
#define ATTITUDE_ESTIMATOR_MIN_DT_S            0.0025f
#define ATTITUDE_ESTIMATOR_MAX_DT_S            0.0500f
#define ATTITUDE_ESTIMATOR_MIN_NORM_SQ          0.01f

volatile attitude_estimator_snapshot_t g_attitude_estimator_snapshot;

static float s_quaternion[4];
static uint32_t s_last_source_update_sequence;
static uint32_t s_last_timestamp_us;
static uint32_t s_processed_count;
static uint32_t s_duplicate_count;
static uint32_t s_rejected_count;
static uint32_t s_timing_reset_count;
static bool s_initialized;
static bool s_have_source_sequence;

static float AttitudeEstimator_Abs(float value)
{
    return (value < 0.0f) ? -value : value;
}

static float AttitudeEstimator_Clamp(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void AttitudeEstimator_MapSensorToBody(
    const float sensor[3], float body[3])
{
    /* Installed module: sensor +Y forward, +X right, +Z up. */
    body[0] = sensor[1];
    body[1] = -sensor[0];
    body[2] = sensor[2];
}

static float AttitudeEstimator_AccelWeight(float norm_g)
{
    float error_g = AttitudeEstimator_Abs(norm_g - 1.0f);

    if (error_g <= ATTITUDE_ESTIMATOR_ACCEL_FULL_ERROR_G) {
        return 1.0f;
    }
    if (error_g >= ATTITUDE_ESTIMATOR_ACCEL_REJECT_ERROR_G) {
        return 0.0f;
    }
    return (ATTITUDE_ESTIMATOR_ACCEL_REJECT_ERROR_G - error_g) /
        (ATTITUDE_ESTIMATOR_ACCEL_REJECT_ERROR_G -
            ATTITUDE_ESTIMATOR_ACCEL_FULL_ERROR_G);
}

static void AttitudeEstimator_NormalizeQuaternion(void)
{
    float norm_sq = s_quaternion[0] * s_quaternion[0] +
        s_quaternion[1] * s_quaternion[1] +
        s_quaternion[2] * s_quaternion[2] +
        s_quaternion[3] * s_quaternion[3];
    float inverse_norm;
    uint8_t index;

    if (norm_sq < ATTITUDE_ESTIMATOR_MIN_NORM_SQ) {
        s_quaternion[0] = 1.0f;
        s_quaternion[1] = 0.0f;
        s_quaternion[2] = 0.0f;
        s_quaternion[3] = 0.0f;
        return;
    }
    inverse_norm = 1.0f / sqrtf(norm_sq);
    for (index = 0U; index < 4U; index++) {
        s_quaternion[index] *= inverse_norm;
    }
}

static void AttitudeEstimator_SetFromTilt(
    const float accel_body_g[3], float yaw_rad)
{
    float roll_rad = atan2f(accel_body_g[1], accel_body_g[2]);
    float pitch_rad = atan2f(-accel_body_g[0],
        sqrtf(accel_body_g[1] * accel_body_g[1] +
            accel_body_g[2] * accel_body_g[2]));
    float half_roll = 0.5f * roll_rad;
    float half_pitch = 0.5f * pitch_rad;
    float half_yaw = 0.5f * yaw_rad;
    float cr = cosf(half_roll);
    float sr = sinf(half_roll);
    float cp = cosf(half_pitch);
    float sp = sinf(half_pitch);
    float cy = cosf(half_yaw);
    float sy = sinf(half_yaw);

    s_quaternion[0] = cr * cp * cy + sr * sp * sy;
    s_quaternion[1] = sr * cp * cy - cr * sp * sy;
    s_quaternion[2] = cr * sp * cy + sr * cp * sy;
    s_quaternion[3] = cr * cp * sy - sr * sp * cy;
    AttitudeEstimator_NormalizeQuaternion();
}

static float AttitudeEstimator_GetYawRad(void)
{
    float w = s_quaternion[0];
    float x = s_quaternion[1];
    float y = s_quaternion[2];
    float z = s_quaternion[3];

    return atan2f(2.0f * (w * z + x * y),
        1.0f - 2.0f * (y * y + z * z));
}

static void AttitudeEstimator_Integrate(
    const float accel_body_g[3], const float gyro_body_dps[3],
    float accel_norm_g, float accel_weight, float dt_s)
{
    float accel[3];
    float gyro_rad_s[3];
    float predicted_up[3];
    float error[3];
    float inverse_accel_norm;
    float w = s_quaternion[0];
    float x = s_quaternion[1];
    float y = s_quaternion[2];
    float z = s_quaternion[3];
    float half_dt = 0.5f * dt_s;
    uint8_t axis;

    for (axis = 0U; axis < 3U; axis++) {
        gyro_rad_s[axis] = gyro_body_dps[axis] *
            ATTITUDE_ESTIMATOR_DEG_TO_RAD;
    }

    if ((accel_weight > 0.0f) && (accel_norm_g > 0.0f)) {
        inverse_accel_norm = 1.0f / accel_norm_g;
        for (axis = 0U; axis < 3U; axis++) {
            accel[axis] = accel_body_g[axis] * inverse_accel_norm;
        }

        predicted_up[0] = 2.0f * (x * z - w * y);
        predicted_up[1] = 2.0f * (w * x + y * z);
        predicted_up[2] = w * w - x * x - y * y + z * z;
        error[0] = accel[1] * predicted_up[2] -
            accel[2] * predicted_up[1];
        error[1] = accel[2] * predicted_up[0] -
            accel[0] * predicted_up[2];
        error[2] = accel[0] * predicted_up[1] -
            accel[1] * predicted_up[0];
        for (axis = 0U; axis < 3U; axis++) {
            gyro_rad_s[axis] += ATTITUDE_ESTIMATOR_CORRECTION_GAIN *
                accel_weight * error[axis];
        }
    }

    s_quaternion[0] += (-x * gyro_rad_s[0] - y * gyro_rad_s[1] -
        z * gyro_rad_s[2]) * half_dt;
    s_quaternion[1] += (w * gyro_rad_s[0] + y * gyro_rad_s[2] -
        z * gyro_rad_s[1]) * half_dt;
    s_quaternion[2] += (w * gyro_rad_s[1] - x * gyro_rad_s[2] +
        z * gyro_rad_s[0]) * half_dt;
    s_quaternion[3] += (w * gyro_rad_s[2] + x * gyro_rad_s[1] -
        y * gyro_rad_s[0]) * half_dt;
    AttitudeEstimator_NormalizeQuaternion();
}

static void AttitudeEstimator_Publish(
    const attitude_estimator_input_t *input, const float accel_body_g[3],
    const float gyro_body_dps[3], float accel_norm_g, float accel_weight,
    float dt_s, uint8_t flags)
{
    attitude_estimator_snapshot_t snapshot;
    float w = s_quaternion[0];
    float x = s_quaternion[1];
    float y = s_quaternion[2];
    float z = s_quaternion[3];
    float sin_pitch;
    uint32_t previous_sequence;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.version = ATTITUDE_ESTIMATOR_SNAPSHOT_VERSION;
    snapshot.size_bytes = (uint16_t) sizeof(snapshot);
    snapshot.imu_sample_count = input->sample_count;
    snapshot.timestamp_us = input->timestamp_us;
    snapshot.roll_deg = -atan2f(2.0f * (w * x + y * z),
        1.0f - 2.0f * (x * x + y * y)) *
        ATTITUDE_ESTIMATOR_RAD_TO_DEG;
    sin_pitch = AttitudeEstimator_Clamp(
        2.0f * (w * y - z * x), -1.0f, 1.0f);
    snapshot.pitch_deg = -asinf(sin_pitch) *
        ATTITUDE_ESTIMATOR_RAD_TO_DEG;
    snapshot.yaw_deg = AttitudeEstimator_GetYawRad() *
        ATTITUDE_ESTIMATOR_RAD_TO_DEG;
    snapshot.axis_rate_dps[0] = -gyro_body_dps[0];
    snapshot.axis_rate_dps[1] = -gyro_body_dps[1];
    snapshot.axis_rate_dps[2] = gyro_body_dps[2];
    snapshot.accel_norm_g = accel_norm_g;
    snapshot.accel_weight = accel_weight;
    snapshot.dt_s = dt_s;
    snapshot.processed_count = s_processed_count;
    snapshot.duplicate_count = s_duplicate_count;
    snapshot.rejected_count = s_rejected_count;
    snapshot.timing_reset_count = s_timing_reset_count;
    snapshot.flags = flags;

    ATTITUDE_ESTIMATOR_ENTER_CRITICAL();
    previous_sequence = g_attitude_estimator_snapshot.update_sequence;
    g_attitude_estimator_snapshot.update_sequence = previous_sequence + 1U;
    g_attitude_estimator_snapshot = snapshot;
    g_attitude_estimator_snapshot.update_sequence = previous_sequence + 2U;
    ATTITUDE_ESTIMATOR_EXIT_CRITICAL();
}

static void AttitudeEstimator_PublishSourceInvalid(
    const attitude_estimator_input_t *input)
{
    attitude_estimator_snapshot_t snapshot;
    uint32_t previous_sequence;

    ATTITUDE_ESTIMATOR_ENTER_CRITICAL();
    previous_sequence = g_attitude_estimator_snapshot.update_sequence;
    snapshot = g_attitude_estimator_snapshot;
    snapshot.imu_sample_count = input->sample_count;
    snapshot.timestamp_us = input->timestamp_us;
    snapshot.rejected_count = s_rejected_count;
    snapshot.flags = s_initialized ?
        ATTITUDE_ESTIMATOR_FLAG_INITIALIZED : 0U;
    g_attitude_estimator_snapshot.update_sequence = previous_sequence + 1U;
    g_attitude_estimator_snapshot = snapshot;
    g_attitude_estimator_snapshot.update_sequence = previous_sequence + 2U;
    ATTITUDE_ESTIMATOR_EXIT_CRITICAL();
}

void AttitudeEstimator_Init(void)
{
    attitude_estimator_snapshot_t initial;

    memset(&initial, 0, sizeof(initial));
    initial.version = ATTITUDE_ESTIMATOR_SNAPSHOT_VERSION;
    initial.size_bytes = (uint16_t) sizeof(initial);
    s_quaternion[0] = 1.0f;
    s_quaternion[1] = 0.0f;
    s_quaternion[2] = 0.0f;
    s_quaternion[3] = 0.0f;
    s_last_source_update_sequence = 0U;
    s_last_timestamp_us = 0U;
    s_processed_count = 0U;
    s_duplicate_count = 0U;
    s_rejected_count = 0U;
    s_timing_reset_count = 0U;
    s_initialized = false;
    s_have_source_sequence = false;
    ATTITUDE_ESTIMATOR_ENTER_CRITICAL();
    g_attitude_estimator_snapshot = initial;
    ATTITUDE_ESTIMATOR_EXIT_CRITICAL();
}

bool AttitudeEstimator_Process(const attitude_estimator_input_t *input)
{
    float accel_body_g[3];
    float gyro_body_dps[3];
    float accel_norm_g;
    float accel_weight;
    float dt_s = 0.0f;
    uint32_t delta_us;
    uint8_t flags = 0U;

    if (input == NULL) {
        s_rejected_count++;
        return false;
    }
    if (s_have_source_sequence &&
        (input->source_update_sequence ==
            s_last_source_update_sequence)) {
        s_duplicate_count++;
        return false;
    }
    s_have_source_sequence = true;
    s_last_source_update_sequence = input->source_update_sequence;

    if ((input->valid == 0U) || (input->ready == 0U)) {
        s_rejected_count++;
        AttitudeEstimator_PublishSourceInvalid(input);
        return false;
    }
    flags |= ATTITUDE_ESTIMATOR_FLAG_SOURCE_VALID;
    AttitudeEstimator_MapSensorToBody(input->accel_sensor_g, accel_body_g);
    AttitudeEstimator_MapSensorToBody(input->gyro_sensor_dps, gyro_body_dps);
    accel_norm_g = sqrtf(accel_body_g[0] * accel_body_g[0] +
        accel_body_g[1] * accel_body_g[1] +
        accel_body_g[2] * accel_body_g[2]);
    accel_weight = AttitudeEstimator_AccelWeight(accel_norm_g);

    if (!s_initialized) {
        if (accel_weight <= 0.0f) {
            s_rejected_count++;
            return false;
        }
        AttitudeEstimator_SetFromTilt(accel_body_g, 0.0f);
        s_initialized = true;
        flags |= ATTITUDE_ESTIMATOR_FLAG_ACCEL_USED;
    } else {
        delta_us = input->timestamp_us - s_last_timestamp_us;
        dt_s = (float) delta_us * 0.000001f;
        if ((dt_s < ATTITUDE_ESTIMATOR_MIN_DT_S) ||
            (dt_s > ATTITUDE_ESTIMATOR_MAX_DT_S)) {
            if (accel_weight <= 0.0f) {
                s_rejected_count++;
                s_last_timestamp_us = input->timestamp_us;
                return false;
            }
            AttitudeEstimator_SetFromTilt(
                accel_body_g, AttitudeEstimator_GetYawRad());
            s_timing_reset_count++;
            dt_s = 0.0f;
            flags |= ATTITUDE_ESTIMATOR_FLAG_TIMING_RESET |
                ATTITUDE_ESTIMATOR_FLAG_ACCEL_USED;
        } else {
            AttitudeEstimator_Integrate(accel_body_g, gyro_body_dps,
                accel_norm_g, accel_weight, dt_s);
            if (accel_weight > 0.0f) {
                flags |= ATTITUDE_ESTIMATOR_FLAG_ACCEL_USED;
            }
        }
    }

    s_last_timestamp_us = input->timestamp_us;
    s_processed_count++;
    flags |= ATTITUDE_ESTIMATOR_FLAG_INITIALIZED;
    AttitudeEstimator_Publish(input, accel_body_g, gyro_body_dps,
        accel_norm_g, accel_weight, dt_s, flags);
    return true;
}

bool AttitudeEstimator_GetSnapshot(attitude_estimator_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    ATTITUDE_ESTIMATOR_ENTER_CRITICAL();
    *snapshot = g_attitude_estimator_snapshot;
    ATTITUDE_ESTIMATOR_EXIT_CRITICAL();
    return (snapshot->flags & ATTITUDE_ESTIMATOR_FLAG_INITIALIZED) != 0U;
}
