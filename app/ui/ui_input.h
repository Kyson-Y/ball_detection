#ifndef ECHO_UI_INPUT_H
#define ECHO_UI_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_KEY_NONE = 0,
    UI_KEY_UP = 1,
    UI_KEY_DOWN = 2,
    UI_KEY_LEFT = 3,
    UI_KEY_RIGHT = 4,
    UI_KEY_OK = 5
} ui_key_t;

typedef enum {
    UI_EVENT_NONE = 0,
    UI_EVENT_PRESS = 1,
    UI_EVENT_LONG_PRESS = 2,
    UI_EVENT_REPEAT = 3,
    UI_EVENT_TIMEOUT = 4
} ui_event_kind_t;

typedef struct {
    ui_key_t key;
    ui_event_kind_t kind;
} ui_input_event_t;

#define UI_PHYSICAL_KEY_COUNT 5U

typedef struct {
    uint32_t sample_count;
    uint32_t press_event_count;
    uint32_t long_press_event_count;
    uint32_t repeat_event_count;
    uint32_t conflict_count;
    uint32_t queue_overflow_count;
    uint32_t physical_press_count[UI_PHYSICAL_KEY_COUNT];
    uint8_t raw_pressed_mask;
    uint8_t stable_pressed_mask;
    uint8_t active_physical_key;
    uint8_t last_physical_key;
    uint8_t queue_depth;
} ui_input_diagnostics_t;

/* Write key 1..5 from Watch; kind 0 defaults to a single PRESS. */
extern volatile uint32_t g_ui_debug_key_request;
extern volatile uint32_t g_ui_debug_event_kind_request;
extern volatile ui_input_diagnostics_t g_ui_input_diag;

void UiInput_Init(void);
void UiInput_ServicePhysical(uint8_t pressed_mask, uint32_t now_ms);
bool UiInput_SetPhysicalKeyMapping(uint8_t physical_key_number,
    ui_key_t logical_key);
ui_key_t UiInput_GetPhysicalKeyMapping(uint8_t physical_key_number);
bool UiInput_Inject(ui_key_t key);
bool UiInput_InjectEvent(ui_key_t key, ui_event_kind_t kind);
ui_input_event_t UiInput_PollEvent(void);
const char *UiInput_KeyName(ui_key_t key);
const char *UiInput_EventName(ui_event_kind_t kind);

#endif
