#ifndef __BITMAP_H
#define __BITMAP_H

#include <stdint.h>

struct RGB
{
    uint8_t r, g, b;
};

struct HSV
{
    int16_t h;
    uint8_t s, v;
};

typedef struct RGB RGB_t;
typedef struct HSV HSV_t;

#define HUE(h)  ({ typeof(h) h1 = h % 360; h1 < 0 ? 360 + h1 : h1; })

void HSV2RGB(HSV_t *hsv, RGB_t *rgb);

#endif //__BITMAP_H
