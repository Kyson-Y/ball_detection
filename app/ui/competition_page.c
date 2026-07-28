#include "competition_page.h"

#include <stddef.h>

#include "ssd1306.h"

#define COMPETITION_UI_LINE_CHARS 21U

typedef struct {
    char text[COMPETITION_UI_LINE_CHARS + 1U];
    uint8_t length;
} competition_ui_line_t;

static void Line_Clear(competition_ui_line_t *line)
{
    line->length = 0U;
    line->text[0] = '\0';
}

static void Line_Char(competition_ui_line_t *line, char value)
{
    if (line->length < COMPETITION_UI_LINE_CHARS) {
        line->text[line->length++] = value;
        line->text[line->length] = '\0';
    }
}

static void Line_Text(competition_ui_line_t *line, const char *text)
{
    while (text != NULL && *text != '\0' &&
        line->length < COMPETITION_UI_LINE_CHARS) {
        Line_Char(line, *text++);
    }
}

static void Line_U32(competition_ui_line_t *line, uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;

    do {
        digits[count++] = (char) ('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) {
        Line_Char(line, digits[--count]);
    }
}

static void Line_I32(competition_ui_line_t *line, int32_t value)
{
    uint32_t magnitude;

    if (value < 0) {
        Line_Char(line, '-');
        magnitude = 0U - (uint32_t) value;
    } else {
        magnitude = (uint32_t) value;
    }
    Line_U32(line, magnitude);
}

static void Line_Fixed1(competition_ui_line_t *line, float value)
{
    int32_t deci = (int32_t) (value * 10.0f +
        ((value >= 0.0f) ? 0.5f : -0.5f));
    uint32_t magnitude;

    if (deci < 0) {
        Line_Char(line, '-');
        magnitude = 0U - (uint32_t) deci;
    } else {
        magnitude = (uint32_t) deci;
    }
    Line_U32(line, magnitude / 10U);
    Line_Char(line, '.');
    Line_Char(line, (char) ('0' + magnitude % 10U));
}

static void Line_Voltage(competition_ui_line_t *line, uint32_t mv)
{
    Line_U32(line, mv / 1000U);
    Line_Char(line, '.');
    Line_Char(line, (char) ('0' + (mv / 100U) % 10U));
    Line_Char(line, (char) ('0' + (mv / 10U) % 10U));
    Line_Char(line, 'V');
}

static void Draw(uint8_t row, const competition_ui_line_t *line)
{
    Ssd1306_DrawText(0U, row, line->text);
}

static void DrawHeader(const char *name,
    const competition_page_data_t *data)
{
    competition_ui_line_t line;

    Line_Clear(&line);
    Line_Text(&line, name);
    Line_Text(&line, "  ");
    Line_Text(&line, CompetitionService_StateName(
        (competition_state_t) data->competition.state));
    Draw(0U, &line);
}

static void DrawFooter(const competition_page_data_t *data)
{
    competition_ui_line_t line;

    Line_Clear(&line);
    Line_Text(&line, "PAGE ");
    Line_U32(&line, (uint32_t) data->competition.page + 1U);
    Line_Text(&line, "/6");
    if (data->competition.save_pending != 0U) {
        Line_Text(&line, " SAVE");
    }
    Draw(7U, &line);
}

static void DrawLiveValues(const competition_page_data_t *data,
    uint8_t start_row)
{
    competition_ui_line_t line;

    Line_Clear(&line);
    Line_Text(&line, "BAT:");
    if (data->supply_valid != 0U) {
        Line_Voltage(&line, data->battery_mv);
    } else {
        Line_Text(&line, "--");
    }
    Line_Text(&line, " Y:");
    Line_Fixed1(&line, data->yaw_deg);
    Draw(start_row, &line);

    Line_Clear(&line);
    Line_Text(&line, "RPM L:");
    Line_Fixed1(&line, data->left_rpm);
    Line_Text(&line, " R:");
    Line_Fixed1(&line, data->right_rpm);
    Draw((uint8_t) (start_row + 1U), &line);
}

static void RenderTask(const competition_page_data_t *data)
{
    competition_ui_line_t line;

    DrawHeader("TASK", data);
    Line_Clear(&line);
    Line_Text(&line, "SLOT:T");
    Line_U32(&line, (uint32_t) data->competition.settings.task_slot + 1U);
    Line_Text(&line, " EMPTY");
    Draw(1U, &line);
    Line_Clear(&line);
    Line_Text(&line, "MISSION:PLACEHOLDER");
    Draw(2U, &line);
    DrawLiveValues(data, 4U);
    Line_Clear(&line);
    Line_Text(&line, "OUT:");
    Line_Text(&line, data->health.actuator_output_permitted ? "ON" : "LOCK");
    Draw(6U, &line);
    DrawFooter(data);
}

static void RenderRun(const competition_page_data_t *data)
{
    competition_ui_line_t line;

    DrawHeader("RUN", data);
    Line_Clear(&line);
    if (data->competition.state ==
        (uint8_t) COMPETITION_STATE_COUNTDOWN) {
        Line_Text(&line, "START IN:");
        Line_U32(&line,
            (data->competition.countdown_remaining_ms + 999U) / 1000U);
        Line_Text(&line, "s");
    } else {
        Line_Text(&line, "RESULT:");
        Line_Text(&line, CompetitionService_ResultName(
            (competition_result_t) data->competition.result));
    }
    Draw(1U, &line);
    DrawLiveValues(data, 2U);
    Line_Clear(&line);
    Line_Text(&line, "DIST:");
    Line_Fixed1(&line, data->distance_progress_mm);
    Line_Text(&line, "mm");
    Line_Text(&line, " STOP:");
    Line_U32(&line, data->competition.emergency_stop_count);
    Draw(4U, &line);
    Line_Clear(&line);
    Line_Text(&line, "REQ:");
    Line_U32(&line, data->competition.request_sequence);
    Draw(5U, &line);
    Line_Clear(&line);
    Line_Text(&line, "OUT:");
    Line_Text(&line, data->health.actuator_output_permitted ? "ACTIVE" : "SAFE");
    Draw(6U, &line);
    DrawFooter(data);
}

static void DrawSelectPrefix(competition_ui_line_t *line,
    const competition_page_data_t *data, uint8_t field)
{
    Line_Char(line, data->competition.cursor == field ? '>' : ' ');
    Line_Char(line, data->competition.cursor == field &&
        data->competition.editing != 0U ? '*' : ' ');
}

static void RenderTest(const competition_page_data_t *data)
{
    competition_ui_line_t line;

    DrawHeader("TEST", data);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 0U);
    Line_Text(&line, "MODE:");
    Line_Text(&line, data->competition.settings.test_action ==
        (uint8_t) COMPETITION_TEST_DISTANCE ? "DIST" : "TURN");
    Draw(1U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 1U);
    Line_Text(&line, "DIST:");
    Line_I32(&line, data->competition.settings.distance_mm);
    Line_Text(&line, "mm");
    Draw(2U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 2U);
    Line_Text(&line, "SPD:");
    Line_Fixed1(&line,
        (float) data->competition.settings.speed_deci_rpm * 0.1f);
    Line_Text(&line, "rpm");
    Draw(3U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 3U);
    Line_Text(&line, "ANG:");
    Line_Fixed1(&line,
        (float) data->competition.settings.angle_deci_deg * 0.1f);
    Line_Text(&line, "deg");
    Draw(4U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 4U);
    Line_Text(&line, "TRN:");
    Line_Fixed1(&line,
        (float) data->competition.settings.turn_speed_deci_rpm * 0.1f);
    Line_Text(&line, "rpm");
    Draw(5U, &line);
    Line_Clear(&line);
    Line_Text(&line, "DELAY:");
    Line_U32(&line, data->competition.settings.start_delay_s);
    Line_Text(&line, "s");
    Draw(6U, &line);
    DrawFooter(data);
}

static void RenderSettings(const competition_page_data_t *data)
{
    competition_ui_line_t line;
    uint8_t cursor = data->competition.cursor;

    if (data->competition.advanced_mode != 0U) {
        DrawHeader("SET ADV", data);
        Line_Clear(&line);
        Line_Text(&line, "PARAM:");
        Line_Text(&line, data->advanced_metadata != NULL ?
            data->advanced_metadata->name : "--");
        Draw(1U, &line);
        Line_Clear(&line);
        Line_Text(&line, data->competition.editing ? "VALUE*>" : "VALUE:");
        Line_Fixed1(&line, data->competition.editing ?
            data->competition.advanced_draft_value : data->advanced_value);
        Draw(2U, &line);
        Line_Clear(&line);
        Line_Text(&line, "INDEX:");
        Line_U32(&line,
            (uint32_t) data->competition.advanced_parameter_index + 1U);
        Line_Text(&line, "/4");
        Draw(3U, &line);
        DrawFooter(data);
        return;
    }

    DrawHeader("SET", data);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 0U);
    Line_Text(&line, "TASK:T");
    Line_U32(&line, (uint32_t) data->competition.settings.task_slot + 1U);
    Draw(1U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 1U);
    Line_Text(&line, "DIST:");
    Line_I32(&line, data->competition.settings.distance_mm);
    Line_Text(&line, "mm");
    Draw(2U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 2U);
    Line_Text(&line, "SPD:");
    Line_Fixed1(&line,
        (float) data->competition.settings.speed_deci_rpm * 0.1f);
    Draw(3U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 3U);
    Line_Text(&line, "ANG:");
    Line_Fixed1(&line,
        (float) data->competition.settings.angle_deci_deg * 0.1f);
    Draw(4U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 4U);
    Line_Text(&line, "TRN:");
    Line_Fixed1(&line,
        (float) data->competition.settings.turn_speed_deci_rpm * 0.1f);
    Draw(5U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 5U);
    Line_Text(&line, "DELAY:");
    Line_U32(&line, data->competition.settings.start_delay_s);
    Line_Text(&line, "s C:");
    Line_U32(&line, cursor + 1U);
    Draw(6U, &line);
    DrawFooter(data);
}

static void AppendStatus(competition_ui_line_t *line, uint8_t ready)
{
    Line_Text(line, ready ? "OK" : "--");
}

static void RenderHealth(const competition_page_data_t *data)
{
    competition_ui_line_t line;

    DrawHeader("HEALTH", data);
    Line_Clear(&line);
    Line_Text(&line, "BAT:"); AppendStatus(&line, data->supply_valid);
    Line_Text(&line, " MPU:"); AppendStatus(&line, data->imu_ready);
    Line_Text(&line, " ENC:"); AppendStatus(&line, data->encoders_ready);
    Draw(1U, &line);
    Line_Clear(&line);
    Line_Text(&line, "OLED:"); AppendStatus(&line, data->health.oled_online);
    Line_Text(&line, " I2C E:"); Line_U32(&line, data->health.i2c_error_count);
    Draw(2U, &line);
    Line_Clear(&line);
    Line_Text(&line, "IR:");
    AppendStatus(&line, data->reflectance_mask == 0xFFU);
    Line_Text(&line, " MASK:"); Line_U32(&line, data->reflectance_mask);
    Draw(3U, &line);
    Line_Clear(&line);
    Line_Text(&line, "ESP:"); AppendStatus(&line, data->esp_ready);
    Line_Text(&line, " RTT:"); Line_U32(&line, data->esp_rtt_us / 1000U);
    Line_Text(&line, "ms");
    Draw(4U, &line);
    Line_Clear(&line);
    Line_Text(&line, "LIDAR:"); AppendStatus(&line, data->lidar_online);
    Line_Text(&line, " IRON:--");
    Draw(5U, &line);
    Line_Clear(&line);
    Line_Text(&line, "Z1:"); AppendStatus(&line, data->zdt_gen1_online);
    Line_Text(&line, " Z2:"); AppendStatus(&line, data->zdt_gen2_online);
    Line_Text(&line, " CAM:"); AppendStatus(&line, data->esp_ready);
    Draw(6U, &line);
    DrawFooter(data);
}

static void RenderSystem(const competition_page_data_t *data)
{
    competition_ui_line_t line;

    DrawHeader("SYS", data);
    Line_Clear(&line);
    Line_Text(&line, "FW:COMP-20260728"); Draw(1U, &line);
    Line_Clear(&line);
    Line_Text(&line, "UP:"); Line_U32(&line, data->uptime_s);
    Line_Text(&line, "s TEMP:"); Line_Fixed1(&line, data->temperature_c);
    Draw(2U, &line);
    Line_Clear(&line);
    Line_Text(&line, "HEALTH:");
    Line_Text(&line, SystemHealth_LevelName(
        (system_health_level_t) data->health.level));
    Draw(3U, &line);
    Line_Clear(&line);
    Line_Text(&line, "HEAP:"); Line_U32(&line, data->health.heap_free_bytes);
    Draw(4U, &line);
    Line_Clear(&line);
    Line_Text(&line, "FLASH:");
    Line_Text(&line, data->competition.result ==
        (uint8_t) COMPETITION_RESULT_STORAGE_FAULT ? "ERR" : "OK");
    Line_Text(&line, " GEN:");
    Line_U32(&line, g_competition_storage_diag.generation);
    Draw(5U, &line);
    Line_Clear(&line);
    Line_Text(&line, "OUT:");
    Line_Text(&line, data->health.actuator_output_permitted ? "ON" : "LOCK");
    Line_Text(&line, " ESP:"); AppendStatus(&line, data->esp_ready);
    Draw(6U, &line);
    DrawFooter(data);
}

void CompetitionPage_Render(const competition_page_data_t *data)
{
    if (data == NULL) {
        return;
    }
    Ssd1306_Clear();
    switch ((competition_page_t) data->competition.page) {
        case COMPETITION_PAGE_TASK: RenderTask(data); break;
        case COMPETITION_PAGE_RUN: RenderRun(data); break;
        case COMPETITION_PAGE_TEST: RenderTest(data); break;
        case COMPETITION_PAGE_SETTINGS: RenderSettings(data); break;
        case COMPETITION_PAGE_HEALTH: RenderHealth(data); break;
        case COMPETITION_PAGE_SYSTEM: RenderSystem(data); break;
        default: RenderTask(data); break;
    }
}
