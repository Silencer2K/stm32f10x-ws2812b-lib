#ifndef __WS2812B_H
#define __WS2812B_H

#include <stdint.h>
#include "bitmap.h"

void ws2812b_Init(void);

int ws2812b_IsReady(void);

void ws2812b_SendRGB(RGB_t *rgb, int count);
void ws2812b_SendHSV(HSV_t *hsv, int count);

#endif //__WS2812B_H
