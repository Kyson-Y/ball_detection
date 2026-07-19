#include <stddef.h>
#include <stdint.h>

#ifndef TFMINI_S_TEST_WASM
#include <stdio.h>
#endif

#include "tfmini_s.h"

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            return __LINE__; \
        } \
    } while (0)

#ifdef TFMINI_S_TEST_WASM
void *memset(void *destination, int value, size_t length)
{
    uint8_t *bytes = (uint8_t *) destination;
    size_t index;

    for (index = 0U; index < length; index++) {
        bytes[index] = (uint8_t) value;
    }
    return destination;
}
#endif

static uint8_t Checksum(const uint8_t *data, uint8_t length)
{
    uint8_t sum = 0U;
    uint8_t index;

    for (index = 0U; index < length; index++) {
        sum = (uint8_t) (sum + data[index]);
    }
    return sum;
}

static void Feed(const uint8_t *data, uint8_t length, uint32_t timestamp_us)
{
    uint8_t index;

    for (index = 0U; index < length; index++) {
        TfminiS_ProcessByte(data[index], timestamp_us);
    }
}

static void BuildDataFrame(uint8_t frame[TFMINI_S_DATA_FRAME_BYTES],
    uint16_t distance_cm, uint16_t strength, uint16_t temperature_raw)
{
    frame[0] = 0x59U;
    frame[1] = 0x59U;
    frame[2] = (uint8_t) distance_cm;
    frame[3] = (uint8_t) (distance_cm >> 8);
    frame[4] = (uint8_t) strength;
    frame[5] = (uint8_t) (strength >> 8);
    frame[6] = (uint8_t) temperature_raw;
    frame[7] = (uint8_t) (temperature_raw >> 8);
    frame[8] = Checksum(frame, 8U);
}

int TfminiS_ParserTest_Run(void)
{
    uint8_t query[TFMINI_S_VERSION_QUERY_BYTES];
    uint8_t data_frame[TFMINI_S_DATA_FRAME_BYTES];
    uint8_t version_response[7] = {
        0x5AU, 0x07U, 0x01U, 0x01U, 0x08U, 0x03U, 0U
    };
    tfmini_s_snapshot_t snapshot;

    TfminiS_Init();
    CHECK(TfminiS_BuildFirmwareVersionQuery(query) == 4U);
    CHECK(query[0] == 0x5AU);
    CHECK(query[1] == 0x04U);
    CHECK(query[2] == 0x01U);
    CHECK(query[3] == 0x5FU);

    TfminiS_ProcessByte(0x12U, 0U);
    TfminiS_ProcessByte(0x59U, 0U);
    TfminiS_ProcessByte(0x00U, 0U);

    BuildDataFrame(data_frame, 123U, 500U, 2240U);
    Feed(data_frame, sizeof(data_frame), 100000U);
    TfminiS_GetSnapshot(100000U, &snapshot);
    CHECK(snapshot.sample_sequence == 1U);
    CHECK(snapshot.distance_cm == 123U);
    CHECK(snapshot.strength == 500U);
    CHECK(snapshot.temperature_centi_c == 2400);
    CHECK(snapshot.status == TFMINI_S_MEASUREMENT_VALID);
    CHECK(snapshot.online == 1U);

    BuildDataFrame(data_frame, 124U, 510U, 2240U);
    Feed(data_frame, sizeof(data_frame), 110000U);
    TfminiS_GetSnapshot(110000U, &snapshot);
    CHECK(snapshot.sample_sequence == 2U);
    CHECK(snapshot.frame_period_us == 10000U);
    CHECK(snapshot.frame_rate_millihz == 100000U);

    data_frame[8]++;
    Feed(data_frame, sizeof(data_frame), 120000U);
    TfminiS_GetSnapshot(120000U, &snapshot);
    CHECK(snapshot.sample_sequence == 2U);
    CHECK(snapshot.checksum_error_count == 1U);

    BuildDataFrame(data_frame, UINT16_MAX, 50U, 2240U);
    Feed(data_frame, sizeof(data_frame), 130000U);
    TfminiS_GetSnapshot(130000U, &snapshot);
    CHECK(snapshot.sample_sequence == 3U);
    CHECK(snapshot.status == TFMINI_S_MEASUREMENT_WEAK_SIGNAL);
    CHECK(snapshot.invalid_measurement_count == 1U);

    version_response[6] = Checksum(version_response, 6U);
    Feed(version_response, sizeof(version_response), 140000U);
    TfminiS_GetSnapshot(140000U, &snapshot);
    CHECK(snapshot.firmware_version_valid == 1U);
    CHECK(snapshot.firmware_version_raw[0] == 1U);
    CHECK(snapshot.firmware_version_raw[1] == 8U);
    CHECK(snapshot.firmware_version_raw[2] == 3U);
    CHECK(snapshot.command_frame_count == 1U);

    TfminiS_Update(400001U);
    TfminiS_GetSnapshot(400001U, &snapshot);
    CHECK(snapshot.online == 0U);
    CHECK(snapshot.timeout_count == 1U);

    return 0;
}

#ifndef TFMINI_S_TEST_WASM
int main(void)
{
    int result = TfminiS_ParserTest_Run();

    if (result == 0) {
        puts("tfmini_s_parser_test: PASS");
    }
    return result;
}
#endif
