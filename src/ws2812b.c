#include <stdint.h>
#include <string.h>

#include "bitmap.h"

#include "ws2812b.h"
#include "ws2812b_conf.h"

//------------------------------------------------------------
// Internal
//------------------------------------------------------------

#define MIN(a, b)   ({ typeof(a) a1 = a; typeof(b) b1 = b; a1 < b1 ? a1 : b1; })

#if defined(__ICCARM__)
__packed struct PWM
#else
struct __attribute__((packed)) PWM
#endif
{
    uint16_t g[8], r[8], b[8];
};

typedef struct PWM PWM_t;
typedef void (SrcFilter_t)(void **, PWM_t **, uint16_t *, uint16_t);

#ifdef WS2812B_USE_GAMMA_CORRECTION
#ifdef WS2812B_USE_PRECALCULATED_GAMMA_TABLE
static const uint8_t LEDGamma[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2,
        2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10,
        10, 11, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21,
        21, 22, 23, 23, 24, 24, 25, 26, 26, 27, 28, 28, 29, 30, 30, 31, 32, 32, 33, 34, 35, 35, 36,
        37, 38, 38, 39, 40, 41, 42, 42, 43, 44, 45, 46, 47, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
        56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 73, 74, 75, 76, 77, 78, 79,
        80, 81, 82, 84, 85, 86, 87, 88, 89, 91, 92, 93, 94, 95, 97, 98, 99, 100, 102, 103, 104, 105,
        107, 108, 109, 111, 112, 113, 115, 116, 117, 119, 120, 121, 123, 124, 126, 127, 128, 130,
        131, 133, 134, 136, 137, 139, 140, 142, 143, 145, 146, 148, 149, 151, 152, 154, 155, 157,
        158, 160, 162, 163, 165, 166, 168, 170, 171, 173, 175, 176, 178, 180, 181, 183, 185, 186,
        188, 190, 192, 193, 195, 197, 199, 200, 202, 204, 206, 207, 209, 211, 213, 215, 217, 218,
        220, 222, 224, 226, 228, 230, 232, 233, 235, 237, 239, 241, 243, 245, 247, 249, 251, 253,
        255 };
#else
static inline uint8_t LEDGamma(uint8_t v)
{
    return (v * v) >> 8;
}
#endif
#endif

static PWM_t DMABuffer[WS2812B_BUFFER_SIZE];

static SrcFilter_t *DMAFilter;
static void *DMASrc;
static uint16_t DMACount;

static void SrcFilterNull(void **src, PWM_t **pwm, uint16_t *count, uint16_t size)
{
    memset(*pwm, 0, size * sizeof(PWM_t));
    *pwm += size;
}

static void RGB2PWM(RGB_t *rgb, PWM_t *pwm)
{
    uint8_t r, g, b;

#ifdef WS2812B_USE_GAMMA_CORRECTION
#ifdef WS2812B_USE_PRECALCULATED_GAMMA_TABLE
    r = LEDGamma[rgb->r];
    g = LEDGamma[rgb->g];
    b = LEDGamma[rgb->b];
#else
    r = LEDGamma(rgb->r);
    g = LEDGamma(rgb->g);
    b = LEDGamma(rgb->b);
#endif
#else
    r = rgb->r;
    g = rgb->g;
    b = rgb->b;
#endif

    int i;
    for (i = 0; i < 8; i++)
    {
        pwm->r[i] = r & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
        pwm->g[i] = g & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
        pwm->b[i] = b & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;

    }
}

static void SrcFilterRGB(void **src, PWM_t **pwm, uint16_t *count, uint16_t size)
{
    RGB_t *rgb = *src;
    PWM_t *p = *pwm;

    *count -= size;

    while (size--)
    {
        RGB2PWM(rgb++, p++);
    }

    *src = rgb;
    *pwm = p;
}

static void SrcFilterHSV(void **src, PWM_t **pwm, uint16_t *count, uint16_t size)
{
    HSV_t *hsv = *src;
    PWM_t *p = *pwm;

    *count -= size;

    while (size--)
    {
        RGB_t rgb;

        HSV2RGB(hsv++, &rgb);
        RGB2PWM(&rgb, p++);
    }

    *src = hsv;
    *pwm = p;
}

static void DMASend(SrcFilter_t *filter, void *src, uint16_t count)
{
    if (ws2812b_IsReady())
    {
        ws2812b_Flags |= WS2812B_FLAG_BUSY;

        DMAFilter = filter;
        DMASrc = src;
        DMACount = count;

        PWM_t *pwm = DMABuffer;
        PWM_t *end = &DMABuffer[WS2812B_BUFFER_SIZE];

        // Start sequence
        SrcFilterNull(NULL, &pwm, NULL, WS2812B_START_SIZE);

        // RGB PWM data
        DMAFilter(&DMASrc, &pwm, &DMACount, MIN(DMACount, end - pwm));

        // Rest of buffer
        if (pwm < end)
            SrcFilterNull(NULL, &pwm, NULL, end - pwm);

        // Start transfer
        DMA_SetCurrDataCounter(WS2812B_DMA_CHANNEL, sizeof(DMABuffer) / sizeof(uint16_t));

        TIM_Cmd(WS2812B_TIM, ENABLE);
        DMA_Cmd(WS2812B_DMA_CHANNEL, ENABLE);
    }
}

static void DMASendNext(PWM_t *pwm, PWM_t *end)
{
    if (!DMAFilter)
    {
        // Stop transfer
        TIM_Cmd(WS2812B_TIM, DISABLE);
        DMA_Cmd(WS2812B_DMA_CHANNEL, DISABLE);

        ws2812b_Flags &= ~WS2812B_FLAG_BUSY;
    }
    else if (!DMACount)
    {
        // Rest of buffer
        SrcFilterNull(NULL, &pwm, NULL, end - pwm);

        DMAFilter = NULL;
    }
    else
    {
        // RGB PWM data
        DMAFilter(&DMASrc, &pwm, &DMACount, MIN(DMACount, end - pwm));

        // Rest of buffer
        if (pwm < end)
            SrcFilterNull(NULL, &pwm, NULL, end - pwm);

        // Start transfer
        DMA_SetCurrDataCounter(WS2812B_DMA_CHANNEL, sizeof(DMABuffer) / sizeof(uint16_t));
    }
}

void WS2812B_DMA_HANDLER(void)
{
    if (DMA_GetITStatus(WS2812B_DMA_IT_HT) == SET)
    {
        DMASendNext(DMABuffer, &DMABuffer[WS2812B_BUFFER_SIZE / 2]);
        DMA_ClearITPendingBit(WS2812B_DMA_IT_HT);
    }

    if (DMA_GetITStatus(WS2812B_DMA_IT_TC) == SET)
    {
        DMASendNext(&DMABuffer[WS2812B_BUFFER_SIZE / 2], &DMABuffer[WS2812B_BUFFER_SIZE]);
        DMA_ClearITPendingBit(WS2812B_DMA_IT_TC);
    }
}

//------------------------------------------------------------
// Interface
//------------------------------------------------------------

volatile uint8_t ws2812b_Flags;

void ws2812b_Init(void)
{
    // Turn on peripheral clock
    RCC_APB1PeriphClockCmd(WS2812B_ABP1_RCC, ENABLE);
    RCC_APB2PeriphClockCmd(WS2812B_ABP2_RCC, ENABLE);

    RCC_AHBPeriphClockCmd(WS2812B_AHB_RCC, ENABLE);

    // Initialize GPIO pin
    GPIO_InitTypeDef GPIO_InitStruct;

    //GPIO_StructInit(&GPIO_InitStruct);

    GPIO_InitStruct.GPIO_Pin = WS2812B_GPIO_PIN;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;

    GPIO_Init(WS2812B_GPIO, &GPIO_InitStruct);

    // Initialize timer clock
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;

    //TIM_TimeBaseStructInit(&TIM_TimeBaseInitStruct);

    TIM_TimeBaseInitStruct.TIM_Prescaler = (SystemCoreClock / WS2812B_FREQUENCY) - 1;
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStruct.TIM_Period = WS2812B_PERIOD - 1;
    TIM_TimeBaseInitStruct.TIM_ClockDivision = TIM_CKD_DIV1;

    TIM_TimeBaseInit(WS2812B_TIM, &TIM_TimeBaseInitStruct);

    // Initialize timer PWM
    TIM_OCInitTypeDef TIM_OCInitStruct;

    //TIM_OCStructInit(&TIM_OCInitStruct);

    TIM_OCInitStruct.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStruct.TIM_Pulse = 0;
    TIM_OCInitStruct.TIM_OCPolarity = TIM_OCPolarity_High;

    WS2812B_TIM_OCINIT(WS2812B_TIM, &TIM_OCInitStruct);
    WS2812B_TIM_OCPRELOAD(WS2812B_TIM, TIM_OCPreload_Enable);

    // Initialize DMA channel
    DMA_InitTypeDef DMA_InitStruct;

    //DMA_StructInit(&DMA_InitStruct);

    DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t) & WS2812B_TIM->CCR1;
    DMA_InitStruct.DMA_MemoryBaseAddr = (uint32_t) DMABuffer;
    DMA_InitStruct.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStruct.DMA_BufferSize = sizeof(DMABuffer) / sizeof(uint16_t);
    DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStruct.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStruct.DMA_Priority = DMA_Priority_High;
    DMA_InitStruct.DMA_M2M = DMA_M2M_Disable;

    DMA_Init(WS2812B_DMA_CHANNEL, &DMA_InitStruct);

    // Turn on timer DMA requests
    TIM_DMACmd(WS2812B_TIM, WS2812B_TIM_DMA_CC, ENABLE);

    // Initialize DMA interrupt
    NVIC_InitTypeDef NVIC_InitStruct;

    NVIC_InitStruct.NVIC_IRQChannel = WS2812B_DMA_IRQ;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = WS2812B_IRQ_PRIO;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = WS2812B_IRQ_SUBPRIO;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;

    NVIC_Init(&NVIC_InitStruct);

    // Enable DMA interrupt
    DMA_ITConfig(WS2812B_DMA_CHANNEL, DMA_IT_HT | DMA_IT_TC, ENABLE);
}

void ws2812b_SendRGB(RGB_t *rgb, uint16_t count)
{
    DMASend(&SrcFilterRGB, rgb, count);
}

void ws2812b_SendHSV(HSV_t *hsv, uint16_t count)
{
    DMASend(&SrcFilterHSV, hsv, count);
}
