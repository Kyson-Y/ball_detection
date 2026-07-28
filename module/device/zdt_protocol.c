#include "zdt_protocol.h"

#include <limits.h>
#include <stddef.h>

#define ZDT_FUNCTION_ENABLE            0xF3U
#define ZDT_FUNCTION_SPEED            0xF6U
#define ZDT_FUNCTION_POSITION         0xFDU
#define ZDT_FUNCTION_STOP             0xFEU
#define ZDT_FUNCTION_SYNCHRONOUS_START 0xFFU

static bool ZdtProtocol_AddressIsValid(uint8_t address)
{
    return address != 0U;
}

static uint32_t ZdtProtocol_AbsI32(int32_t value)
{
    if (value >= 0) {
        return (uint32_t) value;
    }

    return (uint32_t) (-(value + 1)) + 1U;
}

static void ZdtProtocol_PutU16Be(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t) (value >> 8);
    destination[1] = (uint8_t) value;
}

static void ZdtProtocol_PutU32Be(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t) (value >> 24);
    destination[1] = (uint8_t) (value >> 16);
    destination[2] = (uint8_t) (value >> 8);
    destination[3] = (uint8_t) value;
}

static uint16_t ZdtProtocol_GetU16Be(const uint8_t *source)
{
    return (uint16_t) (((uint16_t) source[0] << 8) |
        (uint16_t) source[1]);
}

static uint32_t ZdtProtocol_GetU32Be(const uint8_t *source)
{
    return ((uint32_t) source[0] << 24) |
        ((uint32_t) source[1] << 16) |
        ((uint32_t) source[2] << 8) |
        (uint32_t) source[3];
}

uint8_t ZdtProtocol_AccelerationCode(uint32_t acceleration_rpm_s)
{
    uint32_t interval_count;

    if (acceleration_rpm_s == 0U) {
        return 0U;
    }

    interval_count = (20000U + (acceleration_rpm_s / 2U)) /
        acceleration_rpm_s;
    if (interval_count > 255U) {
        interval_count = 255U;
    } else if (interval_count == 0U) {
        interval_count = 1U;
    }

    return (uint8_t) (256U - interval_count);
}

uint32_t ZdtProtocol_AccelerationRpmPerSecond(uint8_t code)
{
    uint32_t interval_count;

    if (code == 0U) {
        return 0U;
    }

    interval_count = 256U - (uint32_t) code;
    return (20000U + (interval_count / 2U)) / interval_count;
}

bool ZdtProtocol_BuildEnable(uint8_t address, bool enable,
    bool synchronize, zdt_protocol_frame_t *frame)
{
    if (!ZdtProtocol_AddressIsValid(address) || (frame == NULL)) {
        return false;
    }

    frame->bytes[0] = address;
    frame->bytes[1] = ZDT_FUNCTION_ENABLE;
    frame->bytes[2] = 0xABU;
    frame->bytes[3] = enable ? 1U : 0U;
    frame->bytes[4] = synchronize ? 1U : 0U;
    frame->bytes[5] = ZDT_PROTOCOL_CHECK_BYTE;
    frame->length = 6U;
    return true;
}

bool ZdtProtocol_BuildSpeed(uint8_t address, int16_t speed_rpm,
    uint32_t acceleration_rpm_s, bool synchronize,
    zdt_protocol_frame_t *frame)
{
    uint16_t magnitude;

    if (!ZdtProtocol_AddressIsValid(address) || (frame == NULL) ||
        (speed_rpm < -(int16_t) ZDT_PROTOCOL_MAX_SPEED_RPM) ||
        (speed_rpm > (int16_t) ZDT_PROTOCOL_MAX_SPEED_RPM)) {
        return false;
    }

    magnitude = (uint16_t) ((speed_rpm < 0) ? -speed_rpm : speed_rpm);
    frame->bytes[0] = address;
    frame->bytes[1] = ZDT_FUNCTION_SPEED;
    frame->bytes[2] = (speed_rpm < 0) ?
        (uint8_t) ZDT_DIRECTION_CCW : (uint8_t) ZDT_DIRECTION_CW;
    ZdtProtocol_PutU16Be(&frame->bytes[3], magnitude);
    frame->bytes[5] =
        ZdtProtocol_AccelerationCode(acceleration_rpm_s);
    frame->bytes[6] = synchronize ? 1U : 0U;
    frame->bytes[7] = ZDT_PROTOCOL_CHECK_BYTE;
    frame->length = 8U;
    return true;
}

bool ZdtProtocol_BuildPosition(uint8_t address, int32_t pulse_count,
    uint16_t speed_rpm, uint32_t acceleration_rpm_s,
    zdt_position_mode_t mode, bool synchronize,
    zdt_protocol_frame_t *frame)
{
    if (!ZdtProtocol_AddressIsValid(address) || (frame == NULL) ||
        (speed_rpm > ZDT_PROTOCOL_MAX_SPEED_RPM) ||
        ((uint32_t) mode >
            (uint32_t) ZDT_POSITION_RELATIVE_TO_CURRENT)) {
        return false;
    }

    frame->bytes[0] = address;
    frame->bytes[1] = ZDT_FUNCTION_POSITION;
    frame->bytes[2] = (pulse_count < 0) ?
        (uint8_t) ZDT_DIRECTION_CCW : (uint8_t) ZDT_DIRECTION_CW;
    ZdtProtocol_PutU16Be(&frame->bytes[3], speed_rpm);
    frame->bytes[5] =
        ZdtProtocol_AccelerationCode(acceleration_rpm_s);
    ZdtProtocol_PutU32Be(
        &frame->bytes[6], ZdtProtocol_AbsI32(pulse_count));
    frame->bytes[10] = (uint8_t) mode;
    frame->bytes[11] = synchronize ? 1U : 0U;
    frame->bytes[12] = ZDT_PROTOCOL_CHECK_BYTE;
    frame->length = 13U;
    return true;
}

bool ZdtProtocol_BuildStop(uint8_t address, bool synchronize,
    zdt_protocol_frame_t *frame)
{
    if (!ZdtProtocol_AddressIsValid(address) || (frame == NULL)) {
        return false;
    }

    frame->bytes[0] = address;
    frame->bytes[1] = ZDT_FUNCTION_STOP;
    frame->bytes[2] = 0x98U;
    frame->bytes[3] = synchronize ? 1U : 0U;
    frame->bytes[4] = ZDT_PROTOCOL_CHECK_BYTE;
    frame->length = 5U;
    return true;
}

bool ZdtProtocol_BuildSynchronousStart(uint8_t address,
    zdt_protocol_frame_t *frame)
{
    if (frame == NULL) {
        return false;
    }

    frame->bytes[0] = address;
    frame->bytes[1] = ZDT_FUNCTION_SYNCHRONOUS_START;
    frame->bytes[2] = 0x66U;
    frame->bytes[3] = ZDT_PROTOCOL_CHECK_BYTE;
    frame->length = 4U;
    return true;
}

bool ZdtProtocol_BuildQuery(uint8_t address, zdt_query_t query,
    zdt_protocol_frame_t *frame)
{
    if (!ZdtProtocol_AddressIsValid(address) || (frame == NULL)) {
        return false;
    }

    switch (query) {
        case ZDT_QUERY_FIRMWARE_VERSION:
        case ZDT_QUERY_REALTIME_SPEED:
        case ZDT_QUERY_REALTIME_POSITION:
        case ZDT_QUERY_MOTOR_STATUS:
        case ZDT_QUERY_DRIVER_CONFIG:
        case ZDT_QUERY_SYSTEM_STATUS:
            break;
        default:
            return false;
    }

    frame->bytes[0] = address;
    frame->bytes[1] = (uint8_t) query;
    if (query == ZDT_QUERY_DRIVER_CONFIG) {
        frame->bytes[2] = 0x6CU;
        frame->bytes[3] = ZDT_PROTOCOL_CHECK_BYTE;
        frame->length = 4U;
    } else if (query == ZDT_QUERY_SYSTEM_STATUS) {
        frame->bytes[2] = 0x7AU;
        frame->bytes[3] = ZDT_PROTOCOL_CHECK_BYTE;
        frame->length = 4U;
    } else {
        frame->bytes[2] = ZDT_PROTOCOL_CHECK_BYTE;
        frame->length = 3U;
    }
    return true;
}

bool ZdtProtocol_ParseReply(const uint8_t *data, uint8_t length,
    zdt_protocol_reply_t *reply)
{
    if ((data == NULL) || (reply == NULL) || (length != 4U) ||
        !ZdtProtocol_AddressIsValid(data[0]) ||
        (data[3] != ZDT_PROTOCOL_CHECK_BYTE)) {
        return false;
    }

    if ((data[2] != (uint8_t) ZDT_REPLY_ACCEPTED) &&
        (data[2] != (uint8_t) ZDT_REPLY_REACHED) &&
        (data[2] != (uint8_t) ZDT_REPLY_CONDITION_REJECTED) &&
        (data[2] != (uint8_t) ZDT_REPLY_COMMAND_ERROR)) {
        return false;
    }

    reply->address = data[0];
    reply->function = data[1];
    reply->status = (zdt_reply_status_t) data[2];
    return true;
}

bool ZdtProtocol_ParseSpeed(const uint8_t *data, uint8_t length,
    uint8_t expected_address, int16_t *speed_rpm)
{
    uint16_t magnitude;

    if ((data == NULL) || (speed_rpm == NULL) || (length != 6U) ||
        (data[0] != expected_address) ||
        (data[1] != (uint8_t) ZDT_QUERY_REALTIME_SPEED) ||
        (data[2] > 1U) ||
        (data[5] != ZDT_PROTOCOL_CHECK_BYTE)) {
        return false;
    }

    magnitude = ZdtProtocol_GetU16Be(&data[3]);
    if (magnitude > (uint16_t) INT16_MAX) {
        return false;
    }
    *speed_rpm = (data[2] != 0U) ?
        -(int16_t) magnitude : (int16_t) magnitude;
    return true;
}

bool ZdtProtocol_ParsePosition(const uint8_t *data, uint8_t length,
    uint8_t expected_address, int32_t *position_counts)
{
    uint32_t magnitude;

    if ((data == NULL) || (position_counts == NULL) || (length != 8U) ||
        (data[0] != expected_address) ||
        (data[1] != (uint8_t) ZDT_QUERY_REALTIME_POSITION) ||
        (data[2] > 1U) ||
        (data[7] != ZDT_PROTOCOL_CHECK_BYTE)) {
        return false;
    }

    magnitude = ZdtProtocol_GetU32Be(&data[3]);
    if (magnitude > (uint32_t) INT32_MAX) {
        return false;
    }
    *position_counts = (data[2] != 0U) ?
        -(int32_t) magnitude : (int32_t) magnitude;
    return true;
}

bool ZdtProtocol_ParseMotorStatus(const uint8_t *data, uint8_t length,
    uint8_t expected_address, uint8_t *status_flags)
{
    if ((data == NULL) || (status_flags == NULL) || (length != 4U) ||
        (data[0] != expected_address) ||
        (data[1] != (uint8_t) ZDT_QUERY_MOTOR_STATUS) ||
        (data[3] != ZDT_PROTOCOL_CHECK_BYTE)) {
        return false;
    }

    *status_flags = data[2];
    return true;
}

static bool ZdtProtocol_ParseSignedU32(
    uint8_t sign, const uint8_t *data, int32_t *value)
{
    uint32_t magnitude;

    if ((sign > 1U) || (data == NULL) || (value == NULL)) {
        return false;
    }
    magnitude = ZdtProtocol_GetU32Be(data);
    if (magnitude > (uint32_t) INT32_MAX) {
        return false;
    }
    *value = (sign != 0U) ? -(int32_t) magnitude : (int32_t) magnitude;
    return true;
}

bool ZdtProtocol_ParseSystemStatus(const uint8_t *data, uint8_t length,
    uint8_t expected_address, zdt_protocol_system_status_t *status)
{
    uint16_t speed_magnitude;

    if ((data == NULL) || (status == NULL) || (length != 31U) ||
        (data[0] != expected_address) ||
        (data[1] != (uint8_t) ZDT_QUERY_SYSTEM_STATUS) ||
        (data[2] != 0x1FU) || (data[3] != 0x09U) ||
        (data[30] != ZDT_PROTOCOL_CHECK_BYTE) ||
        (data[15] > 1U) || (data[18] > 1U) || (data[23] > 1U)) {
        return false;
    }
    speed_magnitude = ZdtProtocol_GetU16Be(&data[16]);
    if (speed_magnitude > (uint16_t) INT16_MAX ||
        !ZdtProtocol_ParseSignedU32(
            data[10], &data[11], &status->target_position_counts) ||
        !ZdtProtocol_ParseSignedU32(
            data[18], &data[19], &status->position_counts) ||
        !ZdtProtocol_ParseSignedU32(
            data[23], &data[24], &status->position_error_counts)) {
        return false;
    }

    status->bus_voltage_mv = ZdtProtocol_GetU16Be(&data[4]);
    status->phase_current_ma = ZdtProtocol_GetU16Be(&data[6]);
    status->encoder_linear = ZdtProtocol_GetU16Be(&data[8]);
    status->speed_rpm = (data[15] != 0U) ?
        -(int16_t) speed_magnitude : (int16_t) speed_magnitude;
    status->homing_status_flags = data[28];
    status->motor_status_flags = data[29];
    return true;
}

bool ZdtProtocol_ParseDriverConfig(const uint8_t *data, uint8_t length,
    uint8_t expected_address, zdt_protocol_driver_config_t *config)
{
    if ((data == NULL) || (config == NULL) || (length != 33U) ||
        (data[0] != expected_address) ||
        (data[1] != (uint8_t) ZDT_QUERY_DRIVER_CONFIG) ||
        (data[2] != 0x21U) || (data[3] != 0x15U) ||
        (data[32] != ZDT_PROTOCOL_CHECK_BYTE)) {
        return false;
    }

    config->motor_type = data[4];
    config->pulse_port_mode = data[5];
    config->communication_port_mode = data[6];
    config->enable_active_level = data[7];
    config->direction_active_level = data[8];
    config->microstep = data[9];
    config->interpolation_enabled = data[10];
    config->reserved = data[11];
    config->open_loop_current_ma = ZdtProtocol_GetU16Be(&data[12]);
    config->closed_loop_current_ma = ZdtProtocol_GetU16Be(&data[14]);
    config->maximum_output_voltage_setting =
        ZdtProtocol_GetU16Be(&data[16]);
    config->uart_baud_code = data[18];
    config->can_rate_code = data[19];
    config->address = data[20];
    config->checksum_mode = data[21];
    config->response_mode = data[22];
    config->stall_protection_mode = data[23];
    config->stall_speed_rpm = ZdtProtocol_GetU16Be(&data[24]);
    config->stall_current_ma = ZdtProtocol_GetU16Be(&data[26]);
    config->stall_time_ms = ZdtProtocol_GetU16Be(&data[28]);
    config->position_window_tenths_degree =
        ZdtProtocol_GetU16Be(&data[30]);
    return true;
}
