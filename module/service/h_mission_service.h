#ifndef ECHO_H_MISSION_SERVICE_H
#define ECHO_H_MISSION_SERVICE_H

#include <stdint.h>

typedef enum {
    H_MISSION_LINE_LAP = 0U,
    H_MISSION_BALL_STEP,
    H_MISSION_AB_CENTER,
    H_MISSION_LAP_CENTER,
    H_MISSION_LAP_HOLD,
    H_MISSION_COUNT
} h_mission_id_t;

typedef struct {
    uint32_t start_count[H_MISSION_COUNT];
    uint32_t service_count[H_MISSION_COUNT];
    uint32_t stop_count[H_MISSION_COUNT];
    uint8_t active_mission;
    uint8_t initialized;
    uint8_t reserved[2];
} h_mission_diagnostics_t;

extern volatile h_mission_diagnostics_t g_h_mission_diag;

void HMissionService_Init(void);
const char *HMissionService_Code(uint8_t slot);
const char *HMissionService_Name(uint8_t slot);

#endif
