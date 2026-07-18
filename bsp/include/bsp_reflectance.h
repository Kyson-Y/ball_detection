#ifndef ECHO_BSP_REFLECTANCE_H
#define ECHO_BSP_REFLECTANCE_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_REFLECTANCE_CHANNEL_COUNT 8U

typedef struct {
    uint32_t scan_sequence;
    uint16_t raw[BSP_REFLECTANCE_CHANNEL_COUNT];
    uint16_t minimum_raw;
    uint16_t maximum_raw;
    uint32_t conversion_timeout_count;
    uint32_t incomplete_scan_count;
    uint32_t sample_count;
} bsp_reflectance_sample_t;

typedef struct {
    uint32_t scan_sequence;
    uint16_t raw[BSP_REFLECTANCE_CHANNEL_COUNT];
    uint16_t minimum_raw;
    uint16_t maximum_raw;
    uint32_t conversion_timeout_count;
    uint32_t incomplete_scan_count;
    uint32_t sample_count;
    uint8_t selected_channel;
    uint8_t valid_channel_mask;
    uint8_t initialized;
    uint8_t last_conversion_ok;
} bsp_reflectance_diagnostics_t;

/* Watch/debug readers must treat this as read-only. */
extern volatile bsp_reflectance_diagnostics_t g_bsp_reflectance_diag;

void BSP_Reflectance_Init(void);

/*
 * Samples the channel selected during the previous service period,
 * then advances the 74HC4051 address. Returns true once all eight channels
 * have produced a complete scan.
 */
bool BSP_Reflectance_Service(bsp_reflectance_sample_t *sample);

#endif
