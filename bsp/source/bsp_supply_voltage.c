#include "bsp_supply_voltage.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "bsp_time.h"
#include "ti_msp_dl_config.h"

#define BSP_SUPPLY_VOLTAGE_ADC_TIMEOUT_US 200U
#define BSP_SUPPLY_VOLTAGE_FILTER_SHIFT   3U

volatile bsp_supply_voltage_diagnostics_t g_bsp_supply_voltage_diag;

static uint16_t BSP_SupplyVoltage_RawToAdcMv(uint16_t raw)
{
    return (uint16_t) (((uint32_t) raw *
        BSP_SUPPLY_VOLTAGE_ADC_REFERENCE_MV +
        BSP_SUPPLY_VOLTAGE_ADC_FULL_SCALE / 2U) /
        BSP_SUPPLY_VOLTAGE_ADC_FULL_SCALE);
}

uint32_t BSP_SupplyVoltage_RawToBatteryMv(uint16_t raw)
{
    const uint64_t numerator = (uint64_t) raw *
        BSP_SUPPLY_VOLTAGE_ADC_REFERENCE_MV *
        (BSP_SUPPLY_VOLTAGE_DIVIDER_TOP_OHM +
         BSP_SUPPLY_VOLTAGE_DIVIDER_BOTTOM_OHM);
    const uint64_t denominator =
        (uint64_t) BSP_SUPPLY_VOLTAGE_ADC_FULL_SCALE *
        BSP_SUPPLY_VOLTAGE_DIVIDER_BOTTOM_OHM;

    return (uint32_t) ((numerator + denominator / 2U) / denominator);
}

void BSP_SupplyVoltage_Init(void)
{
    memset((void *) &g_bsp_supply_voltage_diag, 0,
        sizeof(g_bsp_supply_voltage_diag));
    g_bsp_supply_voltage_diag.minimum_raw = UINT16_MAX;
    g_bsp_supply_voltage_diag.initialized = 1U;
}

bool BSP_SupplyVoltage_Sample(bsp_supply_voltage_sample_t *sample)
{
    uint32_t start_us;
    uint16_t raw;
    uint16_t filtered_raw;
    int32_t filter_error_q8;

    if ((sample == NULL) ||
        (g_bsp_supply_voltage_diag.initialized == 0U)) {
        return false;
    }

    DL_ADC12_clearInterruptStatus(SUPPLY_ADC_INST,
        DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
    DL_ADC12_enableConversions(SUPPLY_ADC_INST);
    DL_ADC12_startConversion(SUPPLY_ADC_INST);
    start_us = BSP_Time_GetUs();

    while ((DL_ADC12_getRawInterruptStatus(SUPPLY_ADC_INST,
                DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED) &
            DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED) == 0U) {
        if ((uint32_t) (BSP_Time_GetUs() - start_us) >=
            BSP_SUPPLY_VOLTAGE_ADC_TIMEOUT_US) {
            DL_ADC12_stopConversion(SUPPLY_ADC_INST);
            DL_ADC12_disableConversions(SUPPLY_ADC_INST);
            DL_ADC12_clearInterruptStatus(SUPPLY_ADC_INST,
                DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
            g_bsp_supply_voltage_diag.conversion_timeout_count++;
            g_bsp_supply_voltage_diag.last_conversion_ok = 0U;
            return false;
        }
    }

    raw = (uint16_t) DL_ADC12_getMemResult(
        SUPPLY_ADC_INST, SUPPLY_ADC_ADCMEM_0);
    DL_ADC12_stopConversion(SUPPLY_ADC_INST);
    DL_ADC12_disableConversions(SUPPLY_ADC_INST);
    DL_ADC12_clearInterruptStatus(SUPPLY_ADC_INST,
        DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);

    if (g_bsp_supply_voltage_diag.sample_count == 0U) {
        g_bsp_supply_voltage_diag.filtered_raw_q8 =
            (uint32_t) raw << 8;
        g_bsp_supply_voltage_diag.minimum_raw = raw;
        g_bsp_supply_voltage_diag.maximum_raw = raw;
    } else {
        filter_error_q8 = (int32_t) ((uint32_t) raw << 8) -
            (int32_t) g_bsp_supply_voltage_diag.filtered_raw_q8;
        g_bsp_supply_voltage_diag.filtered_raw_q8 = (uint32_t) (
            (int32_t) g_bsp_supply_voltage_diag.filtered_raw_q8 +
            filter_error_q8 / (int32_t) (1U <<
                BSP_SUPPLY_VOLTAGE_FILTER_SHIFT));
        if (raw < g_bsp_supply_voltage_diag.minimum_raw) {
            g_bsp_supply_voltage_diag.minimum_raw = raw;
        }
        if (raw > g_bsp_supply_voltage_diag.maximum_raw) {
            g_bsp_supply_voltage_diag.maximum_raw = raw;
        }
    }

    filtered_raw = (uint16_t) (
        (g_bsp_supply_voltage_diag.filtered_raw_q8 + 128U) >> 8);
    g_bsp_supply_voltage_diag.sample_sequence++;
    g_bsp_supply_voltage_diag.sample_count++;
    g_bsp_supply_voltage_diag.raw = raw;
    g_bsp_supply_voltage_diag.filtered_raw = filtered_raw;
    g_bsp_supply_voltage_diag.adc_input_mv =
        BSP_SupplyVoltage_RawToAdcMv(filtered_raw);
    g_bsp_supply_voltage_diag.battery_mv =
        BSP_SupplyVoltage_RawToBatteryMv(filtered_raw);
    g_bsp_supply_voltage_diag.last_conversion_ok = 1U;

    sample->sample_sequence =
        g_bsp_supply_voltage_diag.sample_sequence;
    sample->raw = raw;
    sample->filtered_raw = filtered_raw;
    sample->adc_input_mv = g_bsp_supply_voltage_diag.adc_input_mv;
    sample->reserved = 0U;
    sample->battery_mv = g_bsp_supply_voltage_diag.battery_mv;
    sample->sample_count = g_bsp_supply_voltage_diag.sample_count;
    sample->conversion_timeout_count =
        g_bsp_supply_voltage_diag.conversion_timeout_count;
    return true;
}
