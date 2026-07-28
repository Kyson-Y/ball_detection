#ifndef ECHO_BSP_BUTTONS_H
#define ECHO_BSP_BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_BUTTON_KEY1 = 0,
    BSP_BUTTON_KEY2 = 1,
    BSP_BUTTON_KEY3 = 2,
    BSP_BUTTON_KEY4 = 3,
    BSP_BUTTON_KEY5 = 4,
    BSP_BUTTON_COUNT = 5
} bsp_button_t;

#define BSP_BUTTON_MASK(button) ((uint8_t) (1U << (uint8_t) (button)))
#define BSP_BUTTON_MASK_ALL     ((uint8_t) 0x1FU)

void BSP_Buttons_Init(void);
uint8_t BSP_Buttons_ReadPressedMask(void);
bool BSP_Buttons_IsPressed(bsp_button_t button);

#endif
