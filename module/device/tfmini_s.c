#include "tfmini_s.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define TFMINI_S_DATA_HEADER 0x59U
#define TFMINI_S_COMMAND_HEADER 0x5AU
#define TFMINI_S_MAX_COMMAND_BYTES 16U
#define TFMINI_S_MIN_COMMAND_BYTES 4U
#define TFMINI_S_COMMAND_ID_VERSION 0x01U
#define TFMINI_S_MAX_DISTANCE_CM 2000U
#define TFMINI_S_MIN_RELIABLE_STRENGTH 100U

typedef enum {
    TFMINI_S_PARSER_IDLE = 0U,
    TFMINI_S_PARSER_DATA = 1U,
    TFMINI_S_PARSER_COMMAND = 2U
} tfmini_s_parser_state_t;

typedef struct {
    tfmini_s_snapshot_t snapshot;
    uint8_t frame[TFMINI_S_MAX_COMMAND_BYTES];
    uint8_t frame_index;
    uint8_t expected_length;
    uint8_t parser_state;
    uint8_t has_data_frame;
    uint8_t online;
} tfmini_s_state_t;

static tfmini_s_state_t s_tfmini;
volatile tfmini_s_parser_diagnostics_t g_tfmini_s_parser_diag;

static uint16_t TfminiS_GetU16(const uint8_t *data)
{
    return (uint16_t) data[0] |
        (uint16_t) ((uint16_t) data[1] << 8);
}

static uint8_t TfminiS_Checksum(
    const uint8_t *data, uint8_t length_without_checksum)
{
    uint8_t checksum = 0U;
    uint8_t index;

    for (index = 0U; index < length_without_checksum; index++) {
        checksum = (uint8_t) (checksum + data[index]);
    }
    return checksum;
}

static int16_t TfminiS_ConvertTemperature(uint16_t raw)
{
    int32_t centi_c = ((int32_t) raw * 100) / 8 - 25600;

    if (centi_c > INT16_MAX) {
        return INT16_MAX;
    }
    if (centi_c < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t) centi_c;
}

static uint8_t TfminiS_ClassifyMeasurement(
    uint16_t distance_cm, uint16_t strength)
{
    if ((distance_cm == UINT16_MAX) ||
        (strength < TFMINI_S_MIN_RELIABLE_STRENGTH)) {
        return TFMINI_S_MEASUREMENT_WEAK_SIGNAL;
    }
    if (distance_cm == (uint16_t) (UINT16_MAX - 1U)) {
        return TFMINI_S_MEASUREMENT_SIGNAL_SATURATION;
    }
    if (distance_cm == (uint16_t) (UINT16_MAX - 3U)) {
        return TFMINI_S_MEASUREMENT_AMBIENT_SATURATION;
    }
    if (distance_cm > TFMINI_S_MAX_DISTANCE_CM) {
        return TFMINI_S_MEASUREMENT_OUT_OF_RANGE;
    }
    return TFMINI_S_MEASUREMENT_VALID;
}

static void TfminiS_UpdateFramePeriod(uint32_t timestamp_us)
{
    if (s_tfmini.has_data_frame != 0U) {
        uint32_t period_us =
            timestamp_us - s_tfmini.snapshot.timestamp_us;

        if (period_us != 0U) {
            if (s_tfmini.snapshot.frame_period_us == 0U) {
                s_tfmini.snapshot.frame_period_us = period_us;
            } else if (period_us >=
                s_tfmini.snapshot.frame_period_us) {
                s_tfmini.snapshot.frame_period_us +=
                    (period_us - s_tfmini.snapshot.frame_period_us) / 8U;
            } else {
                s_tfmini.snapshot.frame_period_us -=
                    (s_tfmini.snapshot.frame_period_us - period_us) / 8U;
            }
            if (s_tfmini.snapshot.frame_period_us != 0U) {
                s_tfmini.snapshot.frame_rate_millihz =
                    1000000000UL /
                    s_tfmini.snapshot.frame_period_us;
            }
        }
    }
}

static void TfminiS_AcceptDataFrame(uint32_t timestamp_us)
{
    uint16_t distance_cm = TfminiS_GetU16(&s_tfmini.frame[2]);
    uint16_t strength = TfminiS_GetU16(&s_tfmini.frame[4]);
    uint16_t temperature_raw = TfminiS_GetU16(&s_tfmini.frame[6]);
    uint8_t status = TfminiS_ClassifyMeasurement(distance_cm, strength);

    TfminiS_UpdateFramePeriod(timestamp_us);
    s_tfmini.snapshot.sample_sequence++;
    s_tfmini.snapshot.timestamp_us = timestamp_us;
    s_tfmini.snapshot.distance_cm = distance_cm;
    s_tfmini.snapshot.strength = strength;
    s_tfmini.snapshot.temperature_centi_c =
        TfminiS_ConvertTemperature(temperature_raw);
    s_tfmini.snapshot.status = status;
    s_tfmini.snapshot.data_frame_count++;
    if (status == TFMINI_S_MEASUREMENT_VALID) {
        s_tfmini.snapshot.valid_measurement_count++;
    } else {
        s_tfmini.snapshot.invalid_measurement_count++;
    }
    s_tfmini.has_data_frame = 1U;
    s_tfmini.online = 1U;
}

static void TfminiS_CompleteDataFrame(uint32_t timestamp_us)
{
    if (TfminiS_Checksum(s_tfmini.frame,
            TFMINI_S_DATA_FRAME_BYTES - 1U) ==
        s_tfmini.frame[TFMINI_S_DATA_FRAME_BYTES - 1U]) {
        TfminiS_AcceptDataFrame(timestamp_us);
    } else {
        s_tfmini.snapshot.checksum_error_count++;
        g_tfmini_s_parser_diag.resync_count++;
    }
}

static void TfminiS_CompleteCommandFrame(void)
{
    uint8_t length = s_tfmini.expected_length;

    if (TfminiS_Checksum(s_tfmini.frame, (uint8_t) (length - 1U)) !=
        s_tfmini.frame[length - 1U]) {
        s_tfmini.snapshot.command_checksum_error_count++;
        g_tfmini_s_parser_diag.resync_count++;
        return;
    }

    s_tfmini.snapshot.command_frame_count++;
    if ((length == 7U) &&
        (s_tfmini.frame[2] == TFMINI_S_COMMAND_ID_VERSION)) {
        s_tfmini.snapshot.firmware_version_raw[0] = s_tfmini.frame[3];
        s_tfmini.snapshot.firmware_version_raw[1] = s_tfmini.frame[4];
        s_tfmini.snapshot.firmware_version_raw[2] = s_tfmini.frame[5];
        s_tfmini.snapshot.firmware_version_valid = 1U;
        g_tfmini_s_parser_diag.version_response_count++;
    }
}

static void TfminiS_ResetParser(void)
{
    s_tfmini.frame_index = 0U;
    s_tfmini.expected_length = 0U;
    s_tfmini.parser_state = TFMINI_S_PARSER_IDLE;
}

static void TfminiS_StartFrame(uint8_t byte)
{
    if (byte == TFMINI_S_DATA_HEADER) {
        s_tfmini.frame[0] = byte;
        s_tfmini.frame_index = 1U;
        s_tfmini.expected_length = TFMINI_S_DATA_FRAME_BYTES;
        s_tfmini.parser_state = TFMINI_S_PARSER_DATA;
    } else if (byte == TFMINI_S_COMMAND_HEADER) {
        s_tfmini.frame[0] = byte;
        s_tfmini.frame_index = 1U;
        s_tfmini.expected_length = 0U;
        s_tfmini.parser_state = TFMINI_S_PARSER_COMMAND;
    } else {
        g_tfmini_s_parser_diag.ignored_byte_count++;
    }
}

void TfminiS_Init(void)
{
    memset(&s_tfmini, 0, sizeof(s_tfmini));
    memset((void *) &g_tfmini_s_parser_diag, 0,
        sizeof(g_tfmini_s_parser_diag));
    s_tfmini.snapshot.status = TFMINI_S_MEASUREMENT_NONE;
}

void TfminiS_ProcessByte(uint8_t byte, uint32_t timestamp_us)
{
    g_tfmini_s_parser_diag.rx_byte_count++;

    if (s_tfmini.parser_state == TFMINI_S_PARSER_IDLE) {
        TfminiS_StartFrame(byte);
        return;
    }

    if (s_tfmini.parser_state == TFMINI_S_PARSER_DATA) {
        if ((s_tfmini.frame_index == 1U) &&
            (byte != TFMINI_S_DATA_HEADER)) {
            g_tfmini_s_parser_diag.resync_count++;
            TfminiS_ResetParser();
            TfminiS_StartFrame(byte);
            return;
        }

        s_tfmini.frame[s_tfmini.frame_index] = byte;
        s_tfmini.frame_index++;
        if (s_tfmini.frame_index >= TFMINI_S_DATA_FRAME_BYTES) {
            TfminiS_CompleteDataFrame(timestamp_us);
            TfminiS_ResetParser();
        }
        return;
    }

    if (s_tfmini.frame_index == 1U) {
        if ((byte < TFMINI_S_MIN_COMMAND_BYTES) ||
            (byte > TFMINI_S_MAX_COMMAND_BYTES)) {
            g_tfmini_s_parser_diag.resync_count++;
            TfminiS_ResetParser();
            TfminiS_StartFrame(byte);
            return;
        }
        s_tfmini.expected_length = byte;
    }

    s_tfmini.frame[s_tfmini.frame_index] = byte;
    s_tfmini.frame_index++;
    if ((s_tfmini.expected_length != 0U) &&
        (s_tfmini.frame_index >= s_tfmini.expected_length)) {
        TfminiS_CompleteCommandFrame();
        TfminiS_ResetParser();
    }
}

void TfminiS_Update(uint32_t timestamp_us)
{
    if ((s_tfmini.online != 0U) &&
        ((timestamp_us - s_tfmini.snapshot.timestamp_us) >
            TFMINI_S_OFFLINE_TIMEOUT_US)) {
        s_tfmini.online = 0U;
        s_tfmini.snapshot.timeout_count++;
    }
}

void TfminiS_GetSnapshot(
    uint32_t timestamp_us, tfmini_s_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    *snapshot = s_tfmini.snapshot;
    if (s_tfmini.has_data_frame != 0U) {
        snapshot->age_us = timestamp_us - snapshot->timestamp_us;
    } else {
        snapshot->age_us = UINT32_MAX;
    }
    snapshot->online = s_tfmini.online;
}

bool TfminiS_HasFirmwareVersion(void)
{
    return (s_tfmini.snapshot.firmware_version_valid != 0U);
}

uint8_t TfminiS_BuildFirmwareVersionQuery(
    uint8_t command[TFMINI_S_VERSION_QUERY_BYTES])
{
    if (command == NULL) {
        return 0U;
    }
    command[0] = TFMINI_S_COMMAND_HEADER;
    command[1] = TFMINI_S_VERSION_QUERY_BYTES;
    command[2] = TFMINI_S_COMMAND_ID_VERSION;
    command[3] = TfminiS_Checksum(command, 3U);
    return TFMINI_S_VERSION_QUERY_BYTES;
}
