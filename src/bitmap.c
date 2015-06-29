#include <stdint.h>

#include "bitmap.h"

void HSV2RGB(HSV_t *hsv, RGB_t *rgb)
{
    if (!hsv->s)
    {
        rgb->r = rgb->g = rgb->b = hsv->v;
    }
    else
    {
        int hue = HUE(hsv->h);

        int sector = hue / 60;
        int angle = sector & 1 ? 60 - hue % 60 : hue % 60;

        int high = hsv->v;
        int low = (255 - hsv->s) * high / 255;
        int middle = low + (high - low) * angle / 60;

        switch (sector)
        {
        case 0: // red -> yellow
            rgb->r = high;
            rgb->g = middle;
            rgb->b = low;

            break;

        case 1: // yellow -> green
            rgb->r = middle;
            rgb->g = high;
            rgb->b = low;

            break;

        case 2: // green -> cyan
            rgb->r = low;
            rgb->g = high;
            rgb->b = middle;

            break;

        case 3: // cyan -> blue
            rgb->r = low;
            rgb->g = middle;
            rgb->b = high;

            break;

        case 4: // blue -> magenta
            rgb->r = middle;
            rgb->g = low;
            rgb->b = high;

            break;

        case 5: // magenta -> red
            rgb->r = high;
            rgb->g = low;
            rgb->b = middle;
        }
    }
}
