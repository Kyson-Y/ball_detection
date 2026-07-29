#include "ssd1306.h"

#include <stddef.h>
#include <string.h>

#include "bsp_i2c.h"

#define SSD1306_ADDRESS_PRIMARY   0x3CU
#define SSD1306_ADDRESS_SECONDARY 0x3DU
#define SSD1306_CONTROL_COMMAND   0x00U
#define SSD1306_CONTROL_DATA      0x40U
#define SSD1306_DATA_CHUNK_BYTES  7U
#define SSD1306_DATA_CHUNKS_PER_STEP 19U
#define SSD1306_FONT_WIDTH        5U
#define SSD1306_CHAR_WIDTH        6U

typedef struct {
    char character;
    uint8_t columns[SSD1306_FONT_WIDTH];
} ssd1306_glyph_t;

static const ssd1306_glyph_t s_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'=', {0x14, 0x14, 0x14, 0x14, 0x14}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}},
    {'%', {0x63, 0x13, 0x08, 0x64, 0x63}},
    {'*', {0x14, 0x08, 0x3E, 0x08, 0x14}},
    {'>', {0x00, 0x41, 0x22, 0x14, 0x08}},
    {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'a', {0x20, 0x54, 0x54, 0x54, 0x78}},
    {'b', {0x7F, 0x48, 0x44, 0x44, 0x38}},
    {'c', {0x38, 0x44, 0x44, 0x44, 0x20}},
    {'d', {0x38, 0x44, 0x44, 0x48, 0x7F}},
    {'e', {0x38, 0x54, 0x54, 0x54, 0x18}},
    {'f', {0x08, 0x7E, 0x09, 0x01, 0x02}},
    {'g', {0x0C, 0x52, 0x52, 0x52, 0x3E}},
    {'h', {0x7F, 0x08, 0x04, 0x04, 0x78}},
    {'i', {0x00, 0x44, 0x7D, 0x40, 0x00}},
    {'j', {0x20, 0x40, 0x44, 0x3D, 0x00}},
    {'k', {0x7F, 0x10, 0x28, 0x44, 0x00}},
    {'l', {0x00, 0x41, 0x7F, 0x40, 0x00}},
    {'m', {0x7C, 0x04, 0x18, 0x04, 0x78}},
    {'n', {0x7C, 0x08, 0x04, 0x04, 0x78}},
    {'o', {0x38, 0x44, 0x44, 0x44, 0x38}},
    {'p', {0x7C, 0x14, 0x14, 0x14, 0x08}},
    {'q', {0x08, 0x14, 0x14, 0x18, 0x7C}},
    {'r', {0x7C, 0x08, 0x04, 0x04, 0x08}},
    {'s', {0x48, 0x54, 0x54, 0x54, 0x20}},
    {'t', {0x04, 0x3F, 0x44, 0x40, 0x20}},
    {'u', {0x3C, 0x40, 0x40, 0x20, 0x7C}},
    {'v', {0x1C, 0x20, 0x40, 0x20, 0x1C}},
    {'w', {0x3C, 0x40, 0x30, 0x40, 0x3C}},
    {'x', {0x44, 0x28, 0x10, 0x28, 0x44}},
    {'y', {0x0C, 0x50, 0x50, 0x50, 0x3C}},
    {'z', {0x44, 0x64, 0x54, 0x4C, 0x44}}
};

volatile ssd1306_diagnostics_t g_ssd1306_diag;

static uint8_t s_framebuffer[SSD1306_WIDTH * SSD1306_PAGES];
static uint8_t s_sent_framebuffer[SSD1306_WIDTH * SSD1306_PAGES];
static uint8_t s_address;
static uint8_t s_refresh_page;
static uint8_t s_refresh_column;
static uint8_t s_refresh_page_command_pending;
static uint8_t s_refresh_active;
static uint8_t s_sent_framebuffer_valid;

static bool Ssd1306_AdvanceToDirtyPage(void)
{
    while ((s_refresh_page < SSD1306_PAGES) &&
        (s_sent_framebuffer_valid != 0U) &&
        (memcmp(&s_framebuffer[(uint16_t) s_refresh_page * SSD1306_WIDTH],
            &s_sent_framebuffer[(uint16_t) s_refresh_page * SSD1306_WIDTH],
            SSD1306_WIDTH) == 0)) {
        s_refresh_page++;
    }
    return s_refresh_page < SSD1306_PAGES;
}

static ssd1306_refresh_step_result_t Ssd1306_CompleteRefresh(void)
{
    s_refresh_active = 0U;
    s_sent_framebuffer_valid = 1U;
    g_ssd1306_diag.refresh_count++;
    return SSD1306_REFRESH_STEP_COMPLETE;
}

static const uint8_t *Ssd1306_FindGlyph(char character)
{
    size_t index;

    for (index = 0U; index < (sizeof(s_font) / sizeof(s_font[0])); index++) {
        if (s_font[index].character == character) {
            return s_font[index].columns;
        }
    }
    return s_font[16U].columns;
}

static bool Ssd1306_WriteCommandBytes(
    uint8_t address, const uint8_t *commands, uint8_t length)
{
    uint8_t packet[BSP_I2C_MAX_WRITE_BYTES];
    uint8_t index;

    if ((commands == NULL) || (length == 0U) ||
        (length >= BSP_I2C_MAX_WRITE_BYTES)) {
        return false;
    }

    packet[0] = SSD1306_CONTROL_COMMAND;
    for (index = 0U; index < length; index++) {
        packet[index + 1U] = commands[index];
    }
    return (BSP_I2C_Write(address, packet, (uint16_t) length + 1U) ==
        BSP_I2C_RESULT_OK);
}

static bool Ssd1306_WriteCommand1(uint8_t command)
{
    return Ssd1306_WriteCommandBytes(s_address, &command, 1U);
}

static bool Ssd1306_WriteCommand2(uint8_t command, uint8_t parameter)
{
    uint8_t commands[2];

    commands[0] = command;
    commands[1] = parameter;
    return Ssd1306_WriteCommandBytes(s_address, commands, 2U);
}

static bool Ssd1306_Probe(uint8_t address)
{
    uint8_t commands[1];

    commands[0] = 0xE3U;
    return Ssd1306_WriteCommandBytes(address, commands, 1U);
}

static bool Ssd1306_Configure(void)
{
    if (!Ssd1306_WriteCommand1(0xAEU) ||
        !Ssd1306_WriteCommand2(0xD5U, 0x80U) ||
        !Ssd1306_WriteCommand2(0xA8U, 0x3FU) ||
        !Ssd1306_WriteCommand2(0xD3U, 0x00U) ||
        !Ssd1306_WriteCommand1(0x40U) ||
        !Ssd1306_WriteCommand2(0x8DU, 0x14U) ||
        !Ssd1306_WriteCommand2(0x20U, 0x02U) ||
        !Ssd1306_WriteCommand1(0xA1U) ||
        !Ssd1306_WriteCommand1(0xC8U) ||
        !Ssd1306_WriteCommand2(0xDAU, 0x12U) ||
        !Ssd1306_WriteCommand2(0x81U, 0x7FU) ||
        !Ssd1306_WriteCommand2(0xD9U, 0xF1U) ||
        !Ssd1306_WriteCommand2(0xDBU, 0x30U) ||
        !Ssd1306_WriteCommand1(0xA4U) ||
        !Ssd1306_WriteCommand1(0xA6U) ||
        !Ssd1306_WriteCommand1(0x2EU)) {
        return false;
    }

    Ssd1306_Clear();
    if (!Ssd1306_Refresh()) {
        return false;
    }
    return Ssd1306_WriteCommand1(0xAFU);
}

bool Ssd1306_Init(void)
{
    g_ssd1306_diag.init_attempt_count++;
    g_ssd1306_diag.online = 0U;
    s_address = 0U;
    s_sent_framebuffer_valid = 0U;

    if (Ssd1306_Probe(SSD1306_ADDRESS_PRIMARY)) {
        s_address = SSD1306_ADDRESS_PRIMARY;
    } else if (Ssd1306_Probe(SSD1306_ADDRESS_SECONDARY)) {
        s_address = SSD1306_ADDRESS_SECONDARY;
    } else {
        g_ssd1306_diag.address = 0U;
        return false;
    }

    g_ssd1306_diag.address = s_address;
    if (!Ssd1306_Configure()) {
        g_ssd1306_diag.command_fail_count++;
        s_address = 0U;
        g_ssd1306_diag.online = 0U;
        return false;
    }

    g_ssd1306_diag.init_success_count++;
    g_ssd1306_diag.online = 1U;
    return true;
}

void Ssd1306_Clear(void)
{
    (void) memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

void Ssd1306_DrawText(uint8_t x, uint8_t page, const char *text)
{
    uint8_t cursor = x;

    if ((text == NULL) || (page >= SSD1306_PAGES)) {
        return;
    }

    while ((*text != '\0') &&
        ((uint16_t) cursor + SSD1306_CHAR_WIDTH <= SSD1306_WIDTH)) {
        const uint8_t *glyph = Ssd1306_FindGlyph(*text);
        uint8_t column;

        for (column = 0U; column < SSD1306_FONT_WIDTH; column++) {
            s_framebuffer[
                ((uint16_t) page * SSD1306_WIDTH) + cursor + column] =
                glyph[column];
        }
        s_framebuffer[((uint16_t) page * SSD1306_WIDTH) + cursor +
            SSD1306_FONT_WIDTH] = 0U;
        cursor = (uint8_t) (cursor + SSD1306_CHAR_WIDTH);
        text++;
    }
}

static void Ssd1306_SetPixel(uint8_t x, uint8_t y)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        return;
    }
    s_framebuffer[(uint16_t) (y / 8U) * SSD1306_WIDTH + x] |=
        (uint8_t) (1U << (y % 8U));
}

void Ssd1306_DrawTextScaled(uint8_t x, uint8_t y, const char *text,
    uint8_t scale)
{
    uint16_t cursor = x;

    if (text == NULL || scale == 0U) {
        return;
    }
    while (*text != '\0' &&
        cursor + (uint16_t) SSD1306_CHAR_WIDTH * scale <= SSD1306_WIDTH) {
        const uint8_t *glyph = Ssd1306_FindGlyph(*text);
        uint8_t column;

        for (column = 0U; column < SSD1306_FONT_WIDTH; column++) {
            uint8_t row;

            for (row = 0U; row < 7U; row++) {
                if ((glyph[column] & (uint8_t) (1U << row)) != 0U) {
                    uint8_t dx;
                    uint8_t dy;

                    for (dx = 0U; dx < scale; dx++) {
                        for (dy = 0U; dy < scale; dy++) {
                            Ssd1306_SetPixel(
                                (uint8_t) (cursor +
                                    (uint16_t) column * scale + dx),
                                (uint8_t) (y +
                                    (uint16_t) row * scale + dy));
                        }
                    }
                }
            }
        }
        cursor += (uint16_t) SSD1306_CHAR_WIDTH * scale;
        text++;
    }
}

void Ssd1306_BeginRefresh(void)
{
    s_refresh_page = 0U;
    s_refresh_column = 0U;
    s_refresh_page_command_pending = 1U;
    s_refresh_active = (s_address != 0U) ? 1U : 0U;
}

ssd1306_refresh_step_result_t Ssd1306_RefreshStep(void)
{
    uint8_t packet[BSP_I2C_MAX_WRITE_BYTES];
    uint8_t transfer_count;

    if ((s_address == 0U) || (s_refresh_active == 0U)) {
        return SSD1306_REFRESH_STEP_FAILED;
    }
    if (!Ssd1306_AdvanceToDirtyPage()) {
        return Ssd1306_CompleteRefresh();
    }

    if (s_refresh_page_command_pending != 0U) {
        packet[0] = (uint8_t) (0xB0U + s_refresh_page);
        packet[1] = 0x00U;
        packet[2] = 0x10U;
        if (!Ssd1306_WriteCommandBytes(s_address, packet, 3U)) {
            g_ssd1306_diag.refresh_fail_count++;
            Ssd1306_MarkOffline();
            return SSD1306_REFRESH_STEP_FAILED;
        }
        s_refresh_page_command_pending = 0U;
    }

    transfer_count = 0U;
    while ((s_refresh_column < SSD1306_WIDTH) &&
        (transfer_count < SSD1306_DATA_CHUNKS_PER_STEP)) {
        uint8_t remaining =
            (uint8_t) (SSD1306_WIDTH - s_refresh_column);
        uint8_t chunk = (remaining > SSD1306_DATA_CHUNK_BYTES) ?
            SSD1306_DATA_CHUNK_BYTES : remaining;
        uint8_t index;

        packet[0] = SSD1306_CONTROL_DATA;
        for (index = 0U; index < chunk; index++) {
            packet[index + 1U] = s_framebuffer[
                ((uint16_t) s_refresh_page * SSD1306_WIDTH) +
                    s_refresh_column + index];
        }
        if (BSP_I2C_Write(s_address, packet, (uint16_t) chunk + 1U) !=
            BSP_I2C_RESULT_OK) {
            g_ssd1306_diag.refresh_fail_count++;
            Ssd1306_MarkOffline();
            return SSD1306_REFRESH_STEP_FAILED;
        }
        s_refresh_column = (uint8_t) (s_refresh_column + chunk);
        transfer_count++;
    }

    if (s_refresh_column >= SSD1306_WIDTH) {
        (void) memcpy(
            &s_sent_framebuffer[(uint16_t) s_refresh_page * SSD1306_WIDTH],
            &s_framebuffer[(uint16_t) s_refresh_page * SSD1306_WIDTH],
            SSD1306_WIDTH);
        s_refresh_page++;
        s_refresh_column = 0U;
        s_refresh_page_command_pending = 1U;
        if (!Ssd1306_AdvanceToDirtyPage()) {
            return Ssd1306_CompleteRefresh();
        }
    }
    return SSD1306_REFRESH_STEP_PENDING;
}

bool Ssd1306_Refresh(void)
{
    ssd1306_refresh_step_result_t result;

    Ssd1306_BeginRefresh();
    do {
        result = Ssd1306_RefreshStep();
    } while (result == SSD1306_REFRESH_STEP_PENDING);

    return result == SSD1306_REFRESH_STEP_COMPLETE;
}

bool Ssd1306_IsOnline(void)
{
    return (g_ssd1306_diag.online != 0U);
}

uint8_t Ssd1306_GetAddress(void)
{
    return s_address;
}

void Ssd1306_MarkOffline(void)
{
    g_ssd1306_diag.online = 0U;
    g_ssd1306_diag.address = 0U;
    s_address = 0U;
    s_refresh_active = 0U;
    s_sent_framebuffer_valid = 0U;
}
