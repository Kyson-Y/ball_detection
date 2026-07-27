#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "zdt_protocol.h"

static void AssertFrame(const zdt_protocol_frame_t *frame,
    const uint8_t *expected, uint8_t length)
{
    assert(frame->length == length);
    assert(memcmp(frame->bytes, expected, length) == 0);
}

static void TestAccelerationConversion(void)
{
    assert(ZdtProtocol_AccelerationCode(0U) == 0U);
    assert(ZdtProtocol_AccelerationCode(500U) == 0xD8U);
    assert(ZdtProtocol_AccelerationRpmPerSecond(0xD8U) == 500U);
    assert(ZdtProtocol_AccelerationRpmPerSecond(10U) == 81U);
    assert(ZdtProtocol_AccelerationCode(1U) == 1U);
    assert(ZdtProtocol_AccelerationCode(50000U) == 255U);
}

static void TestCommandEncoding(void)
{
    static const uint8_t enable[] =
        { 0x01U, 0xF3U, 0xABU, 0x01U, 0x00U, 0x6BU };
    static const uint8_t speed[] =
        { 0x01U, 0xF6U, 0x01U, 0x03U, 0xE8U, 0xD8U, 0x00U, 0x6BU };
    static const uint8_t position[] = {
        0x01U, 0xFDU, 0x00U, 0x03U, 0xE8U, 0x00U,
        0x00U, 0x00U, 0x0CU, 0x80U, 0x00U, 0x00U, 0x6BU
    };
    static const uint8_t stop[] =
        { 0x01U, 0xFEU, 0x98U, 0x00U, 0x6BU };
    static const uint8_t query_position[] =
        { 0x01U, 0x36U, 0x6BU };
    static const uint8_t query_system_status[] =
        { 0x01U, 0x43U, 0x7AU, 0x6BU };
    static const uint8_t query_driver_config[] =
        { 0x01U, 0x42U, 0x6CU, 0x6BU };
    static const uint8_t synchronous_start[] =
        { 0x00U, 0xFFU, 0x66U, 0x6BU };
    zdt_protocol_frame_t frame;

    assert(ZdtProtocol_BuildEnable(1U, true, false, &frame));
    AssertFrame(&frame, enable, (uint8_t) sizeof(enable));

    assert(ZdtProtocol_BuildSpeed(1U, -1000, 500U, false, &frame));
    AssertFrame(&frame, speed, (uint8_t) sizeof(speed));

    assert(ZdtProtocol_BuildPosition(1U, 3200, 1000U, 0U,
        ZDT_POSITION_RELATIVE_TO_LAST_TARGET, false, &frame));
    AssertFrame(&frame, position, (uint8_t) sizeof(position));

    assert(ZdtProtocol_BuildStop(1U, false, &frame));
    AssertFrame(&frame, stop, (uint8_t) sizeof(stop));

    assert(ZdtProtocol_BuildQuery(
        1U, ZDT_QUERY_REALTIME_POSITION, &frame));
    AssertFrame(&frame, query_position,
        (uint8_t) sizeof(query_position));

    assert(ZdtProtocol_BuildQuery(
        1U, ZDT_QUERY_SYSTEM_STATUS, &frame));
    AssertFrame(&frame, query_system_status,
        (uint8_t) sizeof(query_system_status));

    assert(ZdtProtocol_BuildQuery(
        1U, ZDT_QUERY_DRIVER_CONFIG, &frame));
    AssertFrame(&frame, query_driver_config,
        (uint8_t) sizeof(query_driver_config));

    assert(ZdtProtocol_BuildSynchronousStart(0U, &frame));
    AssertFrame(&frame, synchronous_start,
        (uint8_t) sizeof(synchronous_start));

    assert(!ZdtProtocol_BuildSpeed(1U, 3001, 500U, false, &frame));
    assert(!ZdtProtocol_BuildEnable(0U, true, false, &frame));
}

static void TestResponseDecoding(void)
{
    static const uint8_t accepted[] =
        { 0x01U, 0xF6U, 0x02U, 0x6BU };
    static const uint8_t speed[] =
        { 0x01U, 0x35U, 0x01U, 0x05U, 0xDCU, 0x6BU };
    static const uint8_t position[] =
        { 0x01U, 0x36U, 0x01U, 0x00U, 0x01U, 0x00U, 0x00U, 0x6BU };
    static const uint8_t status[] =
        { 0x01U, 0x3AU, 0x03U, 0x6BU };
    static const uint8_t system_status[] = {
        0x01U, 0x43U, 0x1FU, 0x09U, 0x5CU, 0x67U, 0x00U, 0x03U,
        0x43U, 0xEBU, 0x01U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x00U, 0x08U, 0x03U, 0x03U, 0x6BU
    };
    static const uint8_t driver_config[] = {
        0x01U, 0x42U, 0x21U, 0x15U, 0x19U, 0x02U, 0x02U, 0x02U,
        0x00U, 0x10U, 0x01U, 0x00U, 0x04U, 0xB0U, 0x0BU, 0x80U,
        0x0FU, 0xA0U, 0x05U, 0x07U, 0x01U, 0x00U, 0x01U, 0x01U,
        0x00U, 0x08U, 0x08U, 0x98U, 0x07U, 0xD0U, 0x00U, 0x08U,
        0x6BU
    };
    zdt_protocol_reply_t reply;
    zdt_protocol_system_status_t full_status;
    zdt_protocol_driver_config_t config;
    int32_t position_counts;
    int16_t speed_rpm;
    uint8_t status_flags;

    assert(ZdtProtocol_ParseReply(
        accepted, (uint8_t) sizeof(accepted), &reply));
    assert(reply.address == 1U);
    assert(reply.function == 0xF6U);
    assert(reply.status == ZDT_REPLY_ACCEPTED);

    assert(ZdtProtocol_ParseSpeed(
        speed, (uint8_t) sizeof(speed), 1U, &speed_rpm));
    assert(speed_rpm == -1500);

    assert(ZdtProtocol_ParsePosition(
        position, (uint8_t) sizeof(position), 1U, &position_counts));
    assert(position_counts == -65536);

    assert(ZdtProtocol_ParseMotorStatus(
        status, (uint8_t) sizeof(status), 1U, &status_flags));
    assert(status_flags == 0x03U);

    assert(ZdtProtocol_ParseSystemStatus(system_status,
        (uint8_t) sizeof(system_status), 1U, &full_status));
    assert(full_status.bus_voltage_mv == 23655U);
    assert(full_status.phase_current_ma == 3U);
    assert(full_status.encoder_linear == 17387U);
    assert(full_status.target_position_counts == -65536);
    assert(full_status.speed_rpm == 0);
    assert(full_status.position_counts == 65536);
    assert(full_status.position_error_counts == -8);
    assert(full_status.homing_status_flags == 0x03U);
    assert(full_status.motor_status_flags == 0x03U);

    assert(ZdtProtocol_ParseDriverConfig(driver_config,
        (uint8_t) sizeof(driver_config), 1U, &config));
    assert(config.motor_type == 0x19U);
    assert(config.communication_port_mode == 2U);
    assert(config.microstep == 16U);
    assert(config.open_loop_current_ma == 1200U);
    assert(config.closed_loop_current_ma == 2944U);
    assert(config.maximum_output_voltage_setting == 4000U);
    assert(config.uart_baud_code == 5U);
    assert(config.address == 1U);
    assert(config.stall_current_ma == 2200U);
    assert(config.stall_time_ms == 2000U);
    assert(config.position_window_tenths_degree == 8U);
    assert(!ZdtProtocol_ParseSpeed(
        speed, (uint8_t) sizeof(speed), 2U, &speed_rpm));
}

int main(void)
{
    TestAccelerationConversion();
    TestCommandEncoding();
    TestResponseDecoding();
    return 0;
}
