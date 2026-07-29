#include "h_mission_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "chassis_actuator.h"
#include "competition_service.h"

typedef struct {
    uint8_t slot;
    uint8_t running;
} h_mission_context_t;

volatile h_mission_diagnostics_t g_h_mission_diag;

static h_mission_context_t s_context[H_MISSION_COUNT];

static bool HMission_Start(void *context)
{
    h_mission_context_t *mission = (h_mission_context_t *) context;

    if (mission == NULL || mission->slot >= H_MISSION_COUNT) {
        return false;
    }
    ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_NONE);
    mission->running = 1U;
    g_h_mission_diag.active_mission = mission->slot;
    g_h_mission_diag.start_count[mission->slot]++;
    return true;
}

static competition_mission_status_t HMission_Service(void *context,
    uint32_t now_ms)
{
    h_mission_context_t *mission = (h_mission_context_t *) context;

    (void) now_ms;
    if (mission == NULL || mission->slot >= H_MISSION_COUNT ||
        mission->running == 0U) {
        return COMPETITION_MISSION_FAULT;
    }
    g_h_mission_diag.service_count[mission->slot]++;
    return COMPETITION_MISSION_RUNNING;
}

static void HMission_Stop(void *context)
{
    h_mission_context_t *mission = (h_mission_context_t *) context;

    if (mission == NULL || mission->slot >= H_MISSION_COUNT) {
        return;
    }
    mission->running = 0U;
    g_h_mission_diag.stop_count[mission->slot]++;
    g_h_mission_diag.active_mission = H_MISSION_COUNT;
}

void HMissionService_Init(void)
{
    uint8_t slot;

    memset((void *) &g_h_mission_diag, 0, sizeof(g_h_mission_diag));
    memset(s_context, 0, sizeof(s_context));
    g_h_mission_diag.active_mission = H_MISSION_COUNT;
    for (slot = 0U; slot < H_MISSION_COUNT; slot++) {
        competition_mission_t mission;

        s_context[slot].slot = slot;
        mission.start = HMission_Start;
        mission.service = HMission_Service;
        mission.stop = HMission_Stop;
        mission.context = &s_context[slot];
        if (!CompetitionService_RegisterMission(slot, &mission)) {
            return;
        }
    }
    g_h_mission_diag.initialized = 1U;
}

const char *HMissionService_Code(uint8_t slot)
{
    static const char *const codes[H_MISSION_COUNT] = {
        "H2", "H3", "H4", "H5", "H6"
    };

    return slot < H_MISSION_COUNT ? codes[slot] : "H?";
}

const char *HMissionService_Name(uint8_t slot)
{
    static const char *const names[H_MISSION_COUNT] = {
        "LINE LAP",
        "BALL STEP",
        "AB CENTER",
        "LAP CENTER",
        "LAP HOLD"
    };

    return slot < H_MISSION_COUNT ? names[slot] : "UNKNOWN";
}
