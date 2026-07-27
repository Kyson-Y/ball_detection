#ifndef ECHO_ATTITUDE_ESTIMATOR_H
#define ECHO_ATTITUDE_ESTIMATOR_H

#include <stdbool.h>
#include <stdint.h>

#define ATTITUDE_ESTIMATOR_SNAPSHOT_VERSION 1U
#define ATTITUDE_ESTIMATOR_SAMPLE_RATE_HZ   100U

#define ATTITUDE_ESTIMATOR_FLAG_INITIALIZED  (1U << 0)
#define ATTITUDE_ESTIMATOR_FLAG_SOURCE_VALID (1U << 1)
#define ATTITUDE_ESTIMATOR_FLAG_ACCEL_USED   (1U << 2)
#define ATTITUDE_ESTIMATOR_FLAG_TIMING_RESET (1U << 3)

typedef struct {
    uint32_t source_update_sequence;
    uint32_t sample_count;
    uint32_t timestamp_us;
    float accel_sensor_g[3];
    float gyro_sensor_dps[3];
    uint8_t valid;
    uint8_t ready;
} attitude_estimator_input_t;

typedef struct {
    uint16_t version;
    uint16_t size_bytes;
    uint32_t update_sequence;
    uint32_t imu_sample_count;
    uint32_t timestamp_us;
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
    float axis_rate_dps[3];
    float accel_norm_g;
    float accel_weight;
    float dt_s;
    uint32_t processed_count;
    uint32_t duplicate_count;
    uint32_t rejected_count;
    uint32_t timing_reset_count;
    uint8_t flags;
    uint8_t reserved[3];
} attitude_estimator_snapshot_t;

extern volatile attitude_estimator_snapshot_t g_attitude_estimator_snapshot;

void AttitudeEstimator_Init(void);
bool AttitudeEstimator_Process(const attitude_estimator_input_t *input);
bool AttitudeEstimator_GetSnapshot(attitude_estimator_snapshot_t *snapshot);

#endif
