#ifndef ECHO_SERVICE_TASK_H
#define ECHO_SERVICE_TASK_H

#include <stdint.h>

typedef struct {
    uint32_t state;
    uint32_t observed_valid_frame_count;
    uint32_t switch_attempt_count;
    uint32_t switch_sent_count;
    uint32_t save_attempt_count;
    uint32_t save_sent_count;
    uint32_t command_response_count;
    uint32_t completed_count;
    uint32_t last_transition_tick;
} tfmini_i2c_migration_diagnostics_t;

extern volatile tfmini_i2c_migration_diagnostics_t
    g_tfmini_i2c_migration_diag;

void ServiceTask_Entry(void *context);

#endif
