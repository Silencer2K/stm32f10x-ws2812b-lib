STM32F10x WS2812B library
-------------------------

Synopsis:

    #include "ws2812b.h"

    #define NUM_LEDS    300

    RGB_t leds[NUM_LEDS];

    int main() {
        ws2812b_Init();

        while (1) {
            while (!ws2812b_IsReady()); // wait

            //
            // Fill leds buffer
            //

            ws2812b_SendRGB(leds, NUM_LEDS);
        }
    }

HSV color space:

    #include "ws2812b.h"

    #define NUM_LEDS    300

    HSV_t leds[NUM_LEDS];

    int main() {
        ws2812b_Init();

        while (1) {
            while (!ws2812b_IsReady()); // wait

            //
            // Fill leds buffer
            //

            ws2812b_SendHSV(leds, NUM_LEDS);
        }
    }
