#ifndef ECHO_BALL_VISION_H
#define ECHO_BALL_VISION_H

#include <stdbool.h>
#include <stdint.h>

#define BALL_VISION_PACKET_BYTES 22U

#define BALL_VISION_FLAG_DETECTED            (1U << 0)
#define BALL_VISION_FLAG_VELOCITY_VALID      (1U << 1)
#define BALL_VISION_FLAG_PREDICTED           (1U << 2)
#define BALL_VISION_FLAG_LOW_CONFIDENCE      (1U << 3)
#define BALL_VISION_FLAG_REFERENCE_MISMATCH  (1U << 4)
#define BALL_VISION_FLAG_CALIBRATION_VALID   (1U << 5)
#define BALL_VISION_FLAG_TEMPERATURE_WARNING (1U << 6)

typedef struct {
    uint32_t update_sequence;
    uint32_t capture_time_ms;
    uint32_t received_at_us;
    uint32_t receive_age_us;
    uint32_t valid_packet_count;
    uint32_t crc_error_count;
    uint32_t format_error_count;
    uint32_t duplicate_packet_count;
    uint32_t dropped_packet_count;
    uint32_t out_of_order_packet_count;
    uint32_t timeout_count;
    int16_t position_decimm;
    int16_t velocity_mm_s;
    uint16_t packet_sequence;
    uint16_t measurement_age_ms;
    uint8_t flags;
    uint8_t confidence;
    uint8_t online;
    uint8_t control_valid;
} ball_vision_snapshot_t;

typedef struct {
    ball_vision_snapshot_t snapshot;
    uint32_t rx_byte_count;
    uint32_t parser_resync_count;
    uint8_t parser_index;
    uint8_t initialized;
    uint8_t reserved[2];
} ball_vision_diagnostics_t;

extern volatile ball_vision_diagnostics_t g_ball_vision_diag;

void BallVision_Init(void);
void BallVision_ProcessByte(uint8_t byte, uint32_t now_us);
void BallVision_Update(uint32_t now_us);
bool BallVision_GetSnapshot(uint32_t now_us,
    ball_vision_snapshot_t *snapshot);

#endif
