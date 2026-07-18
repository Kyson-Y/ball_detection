#ifndef ECHO_BSP_SUPPLY_VOLTAGE_H
#define ECHO_BSP_SUPPLY_VOLTAGE_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_SUPPLY_VOLTAGE_ADC_FULL_SCALE 4095U
#define BSP_SUPPLY_VOLTAGE_ADC_REFERENCE_MV 3300U
#define BSP_SUPPLY_VOLTAGE_DIVIDER_TOP_OHM 100000U
#define BSP_SUPPLY_VOLTAGE_DIVIDER_BOTTOM_OHM 22000U

typedef struct {
    uint32_t sample_sequence;
    uint16_t raw;
    uint16_t filtered_raw;
    uint16_t adc_input_mv;
    uint16_t reserved;
    uint32_t battery_mv;
    uint32_t sample_count;
    uint32_t conversion_timeout_count;
} bsp_supply_voltage_sample_t;

typedef struct {
    uint32_t sample_sequence;
    uint32_t sample_count;
    uint32_t conversion_timeout_count;
    uint32_t filtered_raw_q8;
    uint16_t raw;
    uint16_t filtered_raw;
    uint16_t adc_input_mv;
    uint16_t minimum_raw;
    uint16_t maximum_raw;
    uint32_t battery_mv;
    uint8_t initialized;
    uint8_t last_conversion_ok;
    uint8_t reserved[2];
} bsp_supply_voltage_diagnostics_t;

extern volatile bsp_supply_voltage_diagnostics_t g_bsp_supply_voltage_diag;

void BSP_SupplyVoltage_Init(void);
bool BSP_SupplyVoltage_Sample(bsp_supply_voltage_sample_t *sample);
uint32_t BSP_SupplyVoltage_RawToBatteryMv(uint16_t raw);

#endif
