#ifndef ECHO_VEHICLE_BRINGUP_CONFIG_H
#define ECHO_VEHICLE_BRINGUP_CONFIG_H

/* Formal installed-hardware baseline for the assembled 513X chassis. */
#define ECHO_ENABLE_REFLECTANCE 1U
#define ECHO_ENABLE_TFMINI      0U
#define ECHO_ENABLE_ESP_LINK    1U
#define ECHO_ENABLE_OLED        1U
#define ECHO_ENABLE_IMU         1U
#define ECHO_IMU_DIAGNOSTIC_CAPTURE 0U

/* Installed MPU6050 six-face calibration at approximately 30.1--30.5 C. */
#define ECHO_IMU_ACCEL_BIAS_X_G  0.00825236f
#define ECHO_IMU_ACCEL_BIAS_Y_G -0.01317945f
#define ECHO_IMU_ACCEL_BIAS_Z_G  0.07864387f
#define ECHO_IMU_ACCEL_SCALE_X   1.00036323f
#define ECHO_IMU_ACCEL_SCALE_Y   0.99782687f
#define ECHO_IMU_ACCEL_SCALE_Z   0.99201797f

#endif
