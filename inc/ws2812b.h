#ifndef __WS2812B_H
#define __WS2812B_H

#include <stdint.h>
#include "bitmap.h"

#define WS2812B_FLAG_BUSY   0x01

#define ws2812b_IsReady()   !(ws2812b_Flags & WS2812B_FLAG_BUSY)

extern volatile uint8_t ws2812b_Flags;

void ws2812b_Init(void);

void ws2812b_SendRGB(RGB_t *rgb, uint16_t count);
void ws2812b_SendHSV(HSV_t *hsv, uint16_t count);

#endif //__WS2812B_H
