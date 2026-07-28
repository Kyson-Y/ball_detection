#ifndef ECHO_COMPETITION_SERVICE_H
#define ECHO_COMPETITION_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "competition_storage.h"
#include "ui_input.h"

#define COMPETITION_TASK_SLOT_COUNT 5U
#define COMPETITION_PAGE_COUNT      6U

typedef enum {
    COMPETITION_STATE_READY = 0U,
    COMPETITION_STATE_ARMED,
    COMPETITION_STATE_COUNTDOWN,
    COMPETITION_STATE_RUNNING,
    COMPETITION_STATE_RESULT,
    COMPETITION_STATE_ABORTED,
    COMPETITION_STATE_FAULT
} competition_state_t;

typedef enum {
    COMPETITION_PAGE_TASK = 0U,
    COMPETITION_PAGE_RUN,
    COMPETITION_PAGE_TEST,
    COMPETITION_PAGE_SETTINGS,
    COMPETITION_PAGE_HEALTH,
    COMPETITION_PAGE_SYSTEM
} competition_page_t;

typedef enum {
    COMPETITION_RESULT_NONE = 0U,
    COMPETITION_RESULT_OK,
    COMPETITION_RESULT_ABORT,
    COMPETITION_RESULT_NO_TASK,
    COMPETITION_RESULT_REJECTED,
    COMPETITION_RESULT_MOTION_FAULT,
    COMPETITION_RESULT_STORAGE_FAULT
} competition_result_t;

typedef enum {
    COMPETITION_MISSION_RUNNING = 0U,
    COMPETITION_MISSION_COMPLETE,
    COMPETITION_MISSION_FAULT
} competition_mission_status_t;

typedef bool (*competition_mission_start_fn_t)(void *context);
typedef competition_mission_status_t (*competition_mission_service_fn_t)(
    void *context, uint32_t now_ms);
typedef void (*competition_mission_stop_fn_t)(void *context);

typedef struct {
    competition_mission_start_fn_t start;
    competition_mission_service_fn_t service;
    competition_mission_stop_fn_t stop;
    void *context;
} competition_mission_t;

typedef struct {
    competition_settings_t settings;
    uint32_t transition_count;
    uint32_t emergency_stop_count;
    uint32_t request_sequence;
    uint32_t countdown_remaining_ms;
    float advanced_draft_value;
    uint8_t state;
    uint8_t page;
    uint8_t cursor;
    uint8_t editing;
    uint8_t advanced_mode;
    uint8_t advanced_parameter_index;
    uint8_t result;
    uint8_t save_pending;
    uint8_t motion_applied;
    uint8_t reserved[3];
} competition_service_snapshot_t;

extern volatile competition_service_snapshot_t g_competition_service;

void CompetitionService_Init(void);
void CompetitionService_Service(uint32_t now_ms);
void CompetitionService_HandleEvent(ui_input_event_t event,
    uint32_t now_ms);
void CompetitionService_ServicePhysicalButtons(uint8_t pressed_mask);
bool CompetitionService_RegisterMission(uint8_t slot,
    const competition_mission_t *mission);
void CompetitionService_GetSnapshot(
    competition_service_snapshot_t *snapshot);
const char *CompetitionService_StateName(competition_state_t state);
const char *CompetitionService_ResultName(competition_result_t result);

#endif
