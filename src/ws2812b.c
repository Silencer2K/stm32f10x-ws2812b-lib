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
    int i;
    for (i = 0; i < 8; i++)
    {
        pwm->r[i] = rgb->r & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
        pwm->g[i] = rgb->g & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
        pwm->b[i] = rgb->b & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
    }
}

static void SrcFilterRGB(void **src, PWM_t **pwm, uint16_t *count, uint16_t size)
{
    RGB_t *rgb = *src;
    PWM_t *p = *pwm;

    *count -= size;

    while (size--)
    {
        RGB2PWM(rgb, p);

        rgb++;
        p++;
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

        HSV2RGB(hsv, &rgb);
        RGB2PWM(&rgb, p);

        hsv++;
        p++;
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
