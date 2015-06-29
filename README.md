STM32F10x WS2812B library
-------------------------

Synopsis:

    #include <stm32f10x.h>
    #include "ws2812b.h"

    #define USE_HSV     1
    #define NUM_LEDS    150

    #ifdef USE_HSV
    HSV_t leds[NUM_LEDS];
    #else
    RGB_t leds[NUM_LEDS];
    #endif

    int main() {
        ws2812b_Init();

        while (1) {
            while (!ws2812b_IsReady()); // wait

            //
            // Fill leds buffer
            //

    #ifdef USE_HSV
            ws2812b_SendHSV(leds, NUM_LEDS);
    #else
            ws2812b_SendRGB(leds, NUM_LEDS);
    #endif
        }
    }
