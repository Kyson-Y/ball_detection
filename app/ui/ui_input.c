#include "ui_input.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define UI_INPUT_INACTIVITY_TIMEOUT_MS 10000U
#define UI_INPUT_DEBOUNCE_MS              25U
#define UI_INPUT_LONG_PRESS_MS           800U
#define UI_INPUT_REPEAT_DELAY_MS         500U
#define UI_INPUT_REPEAT_PERIOD_MS        125U
#define UI_INPUT_EVENT_QUEUE_LENGTH       16U
#define UI_INPUT_NO_PHYSICAL_KEY        0xFFU
#define UI_INPUT_PHYSICAL_MASK          0x1FU

volatile uint32_t g_ui_debug_key_request;
volatile uint32_t g_ui_debug_event_kind_request;
volatile ui_input_diagnostics_t g_ui_input_diag;

/* Freeze the five logical assignments here after the physical key survey. */
static const ui_key_t s_default_physical_mapping[UI_PHYSICAL_KEY_COUNT] = {
    UI_KEY_UP,
    UI_KEY_DOWN,
    UI_KEY_LEFT,
    UI_KEY_RIGHT,
    UI_KEY_OK
};
static ui_key_t s_physical_mapping[UI_PHYSICAL_KEY_COUNT];
static ui_input_event_t s_event_queue[UI_INPUT_EVENT_QUEUE_LENGTH];
static uint8_t s_queue_head;
static uint8_t s_queue_tail;
static uint8_t s_queue_count;
static uint8_t s_candidate_mask;
static uint8_t s_stable_mask;
static uint8_t s_active_physical_index;
static ui_key_t s_active_logical_key;
static uint32_t s_candidate_since_ms;
static uint32_t s_press_start_ms;
static uint32_t s_last_repeat_ms;
static uint32_t s_last_activity_ms;
static bool s_long_press_emitted;
static bool s_timeout_emitted;
static bool s_conflict_latched;

static bool UiInput_IsDirectionKey(ui_key_t key)
{
    return (key == UI_KEY_UP) || (key == UI_KEY_DOWN) ||
        (key == UI_KEY_LEFT) || (key == UI_KEY_RIGHT);
}

static bool UiInput_IsSingleButton(uint8_t mask)
{
    return (mask != 0U) && ((mask & (uint8_t) (mask - 1U)) == 0U);
}

static uint8_t UiInput_GetButtonIndex(uint8_t mask)
{
    uint8_t index;

    for (index = 0U; index < UI_PHYSICAL_KEY_COUNT; index++) {
        if ((mask & (uint8_t) (1U << index)) != 0U) {
            return index;
        }
    }
    return UI_INPUT_NO_PHYSICAL_KEY;
}

static bool UiInput_QueueEvent(ui_key_t key, ui_event_kind_t kind,
    uint32_t now_ms)
{
    bool accepted = false;

    taskENTER_CRITICAL();
    if (s_queue_count < UI_INPUT_EVENT_QUEUE_LENGTH) {
        s_event_queue[s_queue_tail].key = key;
        s_event_queue[s_queue_tail].kind = kind;
        s_queue_tail = (uint8_t) ((s_queue_tail + 1U) %
            UI_INPUT_EVENT_QUEUE_LENGTH);
        s_queue_count++;
        g_ui_input_diag.queue_depth = s_queue_count;
        accepted = true;
    } else {
        g_ui_input_diag.queue_overflow_count++;
    }
    taskEXIT_CRITICAL();

    if (!accepted) {
        return false;
    }
    s_last_activity_ms = now_ms;
    s_timeout_emitted = false;
    if (kind == UI_EVENT_PRESS) {
        g_ui_input_diag.press_event_count++;
    } else if (kind == UI_EVENT_LONG_PRESS) {
        g_ui_input_diag.long_press_event_count++;
    } else if (kind == UI_EVENT_REPEAT) {
        g_ui_input_diag.repeat_event_count++;
    }
    return true;
}

static ui_input_event_t UiInput_DequeueEvent(void)
{
    ui_input_event_t event = { UI_KEY_NONE, UI_EVENT_NONE };

    taskENTER_CRITICAL();
    if (s_queue_count != 0U) {
        event = s_event_queue[s_queue_head];
        s_queue_head = (uint8_t) ((s_queue_head + 1U) %
            UI_INPUT_EVENT_QUEUE_LENGTH);
        s_queue_count--;
        g_ui_input_diag.queue_depth = s_queue_count;
    }
    taskEXIT_CRITICAL();
    return event;
}

static void UiInput_ClearActiveButton(void)
{
    s_active_physical_index = UI_INPUT_NO_PHYSICAL_KEY;
    s_active_logical_key = UI_KEY_NONE;
    s_long_press_emitted = false;
    g_ui_input_diag.active_physical_key = 0U;
}

static void UiInput_AcceptStableMask(uint8_t stable_mask,
    uint32_t now_ms)
{
    bool emit_ok_press_on_release = false;
    uint8_t physical_index;

    s_stable_mask = stable_mask;
    g_ui_input_diag.stable_pressed_mask = stable_mask;

    if (stable_mask == 0U) {
        emit_ok_press_on_release = (s_active_logical_key == UI_KEY_OK) &&
            !s_long_press_emitted && !s_conflict_latched;
        UiInput_ClearActiveButton();
        s_conflict_latched = false;
        if (emit_ok_press_on_release) {
            (void) UiInput_QueueEvent(UI_KEY_OK, UI_EVENT_PRESS, now_ms);
        }
        return;
    }
    if (s_conflict_latched) {
        return;
    }
    UiInput_ClearActiveButton();
    if (!UiInput_IsSingleButton(stable_mask)) {
        g_ui_input_diag.conflict_count++;
        s_conflict_latched = true;
        return;
    }

    physical_index = UiInput_GetButtonIndex(stable_mask);
    if (physical_index >= UI_PHYSICAL_KEY_COUNT) {
        return;
    }
    s_active_physical_index = physical_index;
    s_active_logical_key = s_physical_mapping[physical_index];
    s_press_start_ms = now_ms;
    s_last_repeat_ms = now_ms;
    g_ui_input_diag.active_physical_key = (uint8_t) (physical_index + 1U);
    g_ui_input_diag.last_physical_key = (uint8_t) (physical_index + 1U);
    g_ui_input_diag.physical_press_count[physical_index]++;

    if ((s_active_logical_key != UI_KEY_NONE) &&
        (s_active_logical_key != UI_KEY_OK)) {
        (void) UiInput_QueueEvent(s_active_logical_key,
            UI_EVENT_PRESS, now_ms);
    }
}

void UiInput_Init(void)
{
    uint32_t now_ms = (uint32_t) xTaskGetTickCount();

    g_ui_debug_key_request = (uint32_t) UI_KEY_NONE;
    g_ui_debug_event_kind_request = (uint32_t) UI_EVENT_NONE;
    memset((void *) &g_ui_input_diag, 0, sizeof(g_ui_input_diag));
    memcpy(s_physical_mapping, s_default_physical_mapping,
        sizeof(s_physical_mapping));
    memset(s_event_queue, 0, sizeof(s_event_queue));
    s_queue_head = 0U;
    s_queue_tail = 0U;
    s_queue_count = 0U;
    s_candidate_mask = 0U;
    s_stable_mask = 0U;
    s_candidate_since_ms = now_ms;
    s_press_start_ms = now_ms;
    s_last_repeat_ms = now_ms;
    s_last_activity_ms = now_ms;
    s_timeout_emitted = false;
    s_conflict_latched = false;
    UiInput_ClearActiveButton();
}

void UiInput_ServicePhysical(uint8_t pressed_mask, uint32_t now_ms)
{
    uint8_t normalized_mask = pressed_mask & UI_INPUT_PHYSICAL_MASK;

    g_ui_input_diag.sample_count++;
    g_ui_input_diag.raw_pressed_mask = normalized_mask;

    if (normalized_mask != s_candidate_mask) {
        s_candidate_mask = normalized_mask;
        s_candidate_since_ms = now_ms;
        return;
    }
    if ((normalized_mask != s_stable_mask) &&
        ((uint32_t) (now_ms - s_candidate_since_ms) >=
            UI_INPUT_DEBOUNCE_MS)) {
        UiInput_AcceptStableMask(normalized_mask, now_ms);
        return;
    }
    if ((s_active_physical_index >= UI_PHYSICAL_KEY_COUNT) ||
        (s_active_logical_key == UI_KEY_NONE)) {
        return;
    }

    if (!s_long_press_emitted &&
        ((uint32_t) (now_ms - s_press_start_ms) >=
            UI_INPUT_LONG_PRESS_MS)) {
        s_long_press_emitted = true;
        (void) UiInput_QueueEvent(s_active_logical_key,
            UI_EVENT_LONG_PRESS, now_ms);
    }
    if (UiInput_IsDirectionKey(s_active_logical_key) &&
        ((uint32_t) (now_ms - s_press_start_ms) >=
            UI_INPUT_REPEAT_DELAY_MS) &&
        ((uint32_t) (now_ms - s_last_repeat_ms) >=
            UI_INPUT_REPEAT_PERIOD_MS)) {
        s_last_repeat_ms = now_ms;
        (void) UiInput_QueueEvent(s_active_logical_key,
            UI_EVENT_REPEAT, now_ms);
    }
}

bool UiInput_SetPhysicalKeyMapping(uint8_t physical_key_number,
    ui_key_t logical_key)
{
    if ((physical_key_number == 0U) ||
        (physical_key_number > UI_PHYSICAL_KEY_COUNT) ||
        (logical_key > UI_KEY_OK)) {
        return false;
    }
    taskENTER_CRITICAL();
    s_physical_mapping[physical_key_number - 1U] = logical_key;
    taskEXIT_CRITICAL();
    return true;
}

ui_key_t UiInput_GetPhysicalKeyMapping(uint8_t physical_key_number)
{
    ui_key_t logical_key;

    if ((physical_key_number == 0U) ||
        (physical_key_number > UI_PHYSICAL_KEY_COUNT)) {
        return UI_KEY_NONE;
    }
    taskENTER_CRITICAL();
    logical_key = s_physical_mapping[physical_key_number - 1U];
    taskEXIT_CRITICAL();
    return logical_key;
}

bool UiInput_InjectEvent(ui_key_t key, ui_event_kind_t kind)
{
    bool accepted = false;

    if ((key <= UI_KEY_NONE) || (key > UI_KEY_OK) ||
        (kind < UI_EVENT_PRESS) || (kind > UI_EVENT_REPEAT)) {
        return false;
    }

    taskENTER_CRITICAL();
    if (g_ui_debug_key_request == (uint32_t) UI_KEY_NONE) {
        g_ui_debug_key_request = (uint32_t) key;
        g_ui_debug_event_kind_request = (uint32_t) kind;
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

bool UiInput_Inject(ui_key_t key)
{
    return UiInput_InjectEvent(key, UI_EVENT_PRESS);
}

ui_input_event_t UiInput_PollEvent(void)
{
    ui_input_event_t event = { UI_KEY_NONE, UI_EVENT_NONE };
    uint32_t key_request;
    uint32_t kind_request;
    uint32_t now_ms = (uint32_t) xTaskGetTickCount();

    taskENTER_CRITICAL();
    key_request = g_ui_debug_key_request;
    kind_request = g_ui_debug_event_kind_request;
    g_ui_debug_key_request = (uint32_t) UI_KEY_NONE;
    g_ui_debug_event_kind_request = (uint32_t) UI_EVENT_NONE;
    taskEXIT_CRITICAL();

    if ((key_request > (uint32_t) UI_KEY_NONE) &&
        (key_request <= (uint32_t) UI_KEY_OK)) {
        if (kind_request == (uint32_t) UI_EVENT_NONE) {
            kind_request = (uint32_t) UI_EVENT_PRESS;
        }
        if ((kind_request >= (uint32_t) UI_EVENT_PRESS) &&
            (kind_request <= (uint32_t) UI_EVENT_REPEAT)) {
            event.key = (ui_key_t) key_request;
            event.kind = (ui_event_kind_t) kind_request;
            s_last_activity_ms = now_ms;
            s_timeout_emitted = false;
            return event;
        }
    }

    event = UiInput_DequeueEvent();
    if (event.kind != UI_EVENT_NONE) {
        return event;
    }

    if (!s_timeout_emitted &&
        ((uint32_t) (now_ms - s_last_activity_ms) >=
            UI_INPUT_INACTIVITY_TIMEOUT_MS)) {
        event.kind = UI_EVENT_TIMEOUT;
        s_timeout_emitted = true;
    }
    return event;
}

const char *UiInput_KeyName(ui_key_t key)
{
    switch (key) {
        case UI_KEY_UP:
            return "UP";
        case UI_KEY_DOWN:
            return "DOWN";
        case UI_KEY_LEFT:
            return "LEFT";
        case UI_KEY_RIGHT:
            return "RIGHT";
        case UI_KEY_OK:
            return "OK";
        default:
            return "NONE";
    }
}

const char *UiInput_EventName(ui_event_kind_t kind)
{
    switch (kind) {
        case UI_EVENT_PRESS:
            return "PRESS";
        case UI_EVENT_LONG_PRESS:
            return "LONG";
        case UI_EVENT_REPEAT:
            return "REPEAT";
        case UI_EVENT_TIMEOUT:
            return "TIMEOUT";
        default:
            return "NONE";
    }
}
