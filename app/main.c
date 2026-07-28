#include "FreeRTOS.h"
#include "app_tasks.h"
#include "bsp_buttons.h"
#include "bsp_encoder.h"
#include "bsp_esp_uart.h"
#include "bsp_reflectance.h"
#include "bsp_reset.h"
#include "bsp_supply_voltage.h"
#include "bsp_tfmini_uart.h"
#include "bsp_time.h"
#include "attitude_estimator.h"
#include "chassis_actuator.h"
#include "command_service.h"
#include "competition_service.h"
#include "esp_uart_link_test.h"
#include "imu_service.h"
#include "motor_profile.h"
#include "mpu6050.h"
#include "parameter_service.h"
#include "rtos_diagnostics.h"
#include "serial_rx.h"
#include "serial_tx.h"
#include "rtos_hooks.h"
#include "system_health.h"
#include "task.h"
#include "tfmini_s.h"
#include "tfmini_transport_config.h"
#include "ti_msp_dl_config.h"
#include "vehicle_bringup_config.h"
#include "zdt_stepper.h"

int main(void)
{
    BSP_Reset_Capture();
    SYSCFG_DL_init();
    MotorProfile_Init();
    ChassisActuator_Init();
    BSP_Time_Init();
    BSP_Buttons_Init();
#if ECHO_ENABLE_ESP_LINK
    BSP_EspUart_Init();
    EspUartLinkTest_Init();
#endif
    BSP_Encoder_Init();
#if ECHO_ENABLE_REFLECTANCE
    BSP_Reflectance_Init();
#endif
    BSP_SupplyVoltage_Init();
#if ECHO_ENABLE_TFMINI
    TfminiS_Init();
#endif
#if ECHO_ENABLE_IMU
    Mpu6050_InitDiagnostics();
    ImuService_Init();
    AttitudeEstimator_Init();
#endif
#if TFMINI_S_ENABLE_UART_TO_I2C_MIGRATION
    BSP_TfminiUart_Init();
#endif
    ZdtStepper_Init();
    SerialTx_Init();
    SerialRx_Init();
    ParameterService_Init();
    CompetitionService_Init();
    CommandService_Init();
    RtosDiagnostics_Init(BSP_TIME_FREQUENCY_HZ);
    SystemHealth_Init();
    AppTasks_CreateAll();
    vTaskStartScheduler();

    RtosFault_Halt(RTOS_FAULT_SCHEDULER_RETURNED, NULL, NULL, 0);
}
