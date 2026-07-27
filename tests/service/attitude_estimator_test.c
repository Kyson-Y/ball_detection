#include <assert.h>
#include <math.h>
#include <string.h>

#include "attitude_estimator.h"

static attitude_estimator_input_t TestInput(
    uint32_t sequence, uint32_t timestamp_us)
{
    attitude_estimator_input_t input;

    memset(&input, 0, sizeof(input));
    input.source_update_sequence = sequence;
    input.sample_count = sequence / 2U;
    input.timestamp_us = timestamp_us;
    input.accel_sensor_g[2] = 1.0f;
    input.valid = 1U;
    input.ready = 1U;
    return input;
}

static void TestInstalledAxisMapping(void)
{
    attitude_estimator_input_t input;
    attitude_estimator_snapshot_t snapshot;

    AttitudeEstimator_Init();
    input = TestInput(2U, 1000000U);
    input.accel_sensor_g[0] = 1.0f;
    input.accel_sensor_g[2] = 0.0f;
    assert(AttitudeEstimator_Process(&input));
    assert(AttitudeEstimator_GetSnapshot(&snapshot));
    assert(snapshot.roll_deg > 89.0f);
    assert(fabsf(snapshot.pitch_deg) < 0.1f);

    AttitudeEstimator_Init();
    input = TestInput(2U, 1000000U);
    input.accel_sensor_g[1] = 1.0f;
    input.accel_sensor_g[2] = 0.0f;
    assert(AttitudeEstimator_Process(&input));
    assert(AttitudeEstimator_GetSnapshot(&snapshot));
    assert(snapshot.pitch_deg > 89.0f);
    assert(fabsf(snapshot.roll_deg) < 0.1f);
}

static void TestRelativeYawIntegration(void)
{
    attitude_estimator_input_t input;
    attitude_estimator_snapshot_t snapshot;
    uint32_t index;

    AttitudeEstimator_Init();
    input = TestInput(2U, 1000000U);
    assert(AttitudeEstimator_Process(&input));
    for (index = 1U; index <= 100U; index++) {
        input = TestInput(2U + index * 2U,
            1000000U + index * 10000U);
        input.gyro_sensor_dps[2] = 90.0f;
        assert(AttitudeEstimator_Process(&input));
    }
    assert(AttitudeEstimator_GetSnapshot(&snapshot));
    assert(snapshot.yaw_deg > 89.0f);
    assert(snapshot.yaw_deg < 91.0f);
    assert(snapshot.processed_count == 101U);
    assert(snapshot.timing_reset_count == 0U);
}

static void TestAccelerationAndTimingGates(void)
{
    attitude_estimator_input_t input;
    attitude_estimator_snapshot_t snapshot;

    AttitudeEstimator_Init();
    input = TestInput(2U, 1000000U);
    assert(AttitudeEstimator_Process(&input));

    input = TestInput(4U, 1010000U);
    input.accel_sensor_g[2] = 1.20f;
    assert(AttitudeEstimator_Process(&input));
    assert(AttitudeEstimator_GetSnapshot(&snapshot));
    assert(snapshot.accel_weight == 0.0f);
    assert((snapshot.flags & ATTITUDE_ESTIMATOR_FLAG_ACCEL_USED) == 0U);

    input = TestInput(6U, 1110000U);
    assert(AttitudeEstimator_Process(&input));
    assert(AttitudeEstimator_GetSnapshot(&snapshot));
    assert(snapshot.timing_reset_count == 1U);
    assert((snapshot.flags & ATTITUDE_ESTIMATOR_FLAG_TIMING_RESET) != 0U);
}

static void TestSourceInvalidation(void)
{
    attitude_estimator_input_t input;
    attitude_estimator_snapshot_t snapshot;

    AttitudeEstimator_Init();
    input = TestInput(2U, 1000000U);
    assert(AttitudeEstimator_Process(&input));
    input = TestInput(4U, 1010000U);
    input.valid = 0U;
    assert(!AttitudeEstimator_Process(&input));
    assert(AttitudeEstimator_GetSnapshot(&snapshot));
    assert((snapshot.flags & ATTITUDE_ESTIMATOR_FLAG_INITIALIZED) != 0U);
    assert((snapshot.flags & ATTITUDE_ESTIMATOR_FLAG_SOURCE_VALID) == 0U);
    assert(snapshot.rejected_count == 1U);
}

int main(void)
{
    TestInstalledAxisMapping();
    TestRelativeYawIntegration();
    TestAccelerationAndTimingGates();
    TestSourceInvalidation();
    return 0;
}
