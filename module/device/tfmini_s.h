#ifndef ECHO_TFMINI_S_H
#define ECHO_TFMINI_S_H

#include <stdbool.h>
#include <stdint.h>

#define TFMINI_S_DATA_FRAME_BYTES 9U
#define TFMINI_S_VERSION_QUERY_BYTES 4U
#define TFMINI_S_I2C_QUERY_BYTES 5U
#define TFMINI_S_SET_INTERFACE_BYTES 5U
#define TFMINI_S_SAVE_SETTINGS_BYTES 4U
#define TFMINI_S_OFFLINE_TIMEOUT_US 250000U

typedef enum {
    TFMINI_S_MEASUREMENT_NONE = 0U,
    TFMINI_S_MEASUREMENT_VALID = 1U,
    TFMINI_S_MEASUREMENT_WEAK_SIGNAL = 2U,
    TFMINI_S_MEASUREMENT_SIGNAL_SATURATION = 3U,
    TFMINI_S_MEASUREMENT_AMBIENT_SATURATION = 4U,
    TFMINI_S_MEASUREMENT_OUT_OF_RANGE = 5U
} tfmini_s_measurement_status_t;

typedef struct {
    uint32_t sample_sequence;
    uint32_t timestamp_us;
    uint32_t age_us;
    uint32_t frame_period_us;
    uint32_t frame_rate_millihz;
    uint32_t data_frame_count;
    uint32_t valid_measurement_count;
    uint32_t invalid_measurement_count;
    uint32_t checksum_error_count;
    uint32_t command_frame_count;
    uint32_t command_checksum_error_count;
    uint32_t timeout_count;
    uint16_t distance_cm;
    uint16_t strength;
    int16_t temperature_centi_c;
    uint8_t status;
    uint8_t online;
    uint8_t firmware_version_valid;
    uint8_t firmware_version_raw[3];
} tfmini_s_snapshot_t;

typedef struct {
    uint32_t rx_byte_count;
    uint32_t ignored_byte_count;
    uint32_t resync_count;
    uint32_t version_response_count;
} tfmini_s_parser_diagnostics_t;

extern volatile tfmini_s_parser_diagnostics_t g_tfmini_s_parser_diag;

void TfminiS_Init(void);
void TfminiS_ProcessByte(uint8_t byte, uint32_t timestamp_us);
void TfminiS_Update(uint32_t timestamp_us);
void TfminiS_GetSnapshot(
    uint32_t timestamp_us, tfmini_s_snapshot_t *snapshot);
bool TfminiS_HasFirmwareVersion(void);
uint8_t TfminiS_BuildFirmwareVersionQuery(
    uint8_t command[TFMINI_S_VERSION_QUERY_BYTES]);
uint8_t TfminiS_BuildI2cMeasurementQuery(
    uint8_t command[TFMINI_S_I2C_QUERY_BYTES]);
uint8_t TfminiS_BuildSetI2cCommand(
    uint8_t command[TFMINI_S_SET_INTERFACE_BYTES]);
uint8_t TfminiS_BuildSaveSettingsCommand(
    uint8_t command[TFMINI_S_SAVE_SETTINGS_BYTES]);

#endif
