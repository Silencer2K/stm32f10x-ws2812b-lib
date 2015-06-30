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
static uint8_t Gamma[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03,
        0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x07,
        0x07, 0x07, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x0a, 0x0a, 0x0b, 0x0b, 0x0b, 0x0c, 0x0c,
        0x0d, 0x0d, 0x0e, 0x0e, 0x0f, 0x0f, 0x10, 0x10, 0x11, 0x11, 0x12, 0x12, 0x13, 0x13, 0x14,
        0x14, 0x15, 0x15, 0x16, 0x17, 0x17, 0x18, 0x18, 0x19, 0x1a, 0x1a, 0x1b, 0x1c, 0x1c, 0x1d,
        0x1e, 0x1e, 0x1f, 0x20, 0x20, 0x21, 0x22, 0x23, 0x23, 0x24, 0x25, 0x26, 0x26, 0x27, 0x28,
        0x29, 0x2a, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
        0x36, 0x37, 0x38, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43,
        0x44, 0x45, 0x46, 0x47, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x54,
        0x55, 0x56, 0x57, 0x58, 0x59, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x61, 0x62, 0x63, 0x64, 0x66,
        0x67, 0x68, 0x69, 0x6b, 0x6c, 0x6d, 0x6f, 0x70, 0x71, 0x73, 0x74, 0x75, 0x77, 0x78, 0x79,
        0x7b, 0x7c, 0x7e, 0x7f, 0x80, 0x82, 0x83, 0x85, 0x86, 0x88, 0x89, 0x8b, 0x8c, 0x8e, 0x8f,
        0x91, 0x92, 0x94, 0x95, 0x97, 0x98, 0x9a, 0x9b, 0x9d, 0x9e, 0xa0, 0xa2, 0xa3, 0xa5, 0xa6,
        0xa8, 0xaa, 0xab, 0xad, 0xaf, 0xb0, 0xb2, 0xb4, 0xb5, 0xb7, 0xb9, 0xba, 0xbc, 0xbe, 0xc0,
        0xc1, 0xc3, 0xc5, 0xc7, 0xc8, 0xca, 0xcc, 0xce, 0xcf, 0xd1, 0xd3, 0xd5, 0xd7, 0xd9, 0xda,
        0xdc, 0xde, 0xe0, 0xe2, 0xe4, 0xe6, 0xe8, 0xe9, 0xeb, 0xed, 0xef, 0xf1, 0xf3, 0xf5, 0xf7,
        0xf9, 0xfb, 0xfd, 0xff };
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
    int i;
    for (i = 0; i < 8; i++)
    {
#ifdef WS2812B_USE_GAMMA_CORRECTION
        pwm->r[i] = Gamma[rgb->r] & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
        pwm->g[i] = Gamma[rgb->g] & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
        pwm->b[i] = Gamma[rgb->b] & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
#else
        pwm->r[i] = rgb->r & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
        pwm->g[i] = rgb->g & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
        pwm->b[i] = rgb->b & (128 >> i) ? WS2812B_PULSE_HIGH : WS2812B_PULSE_LOW;
#endif
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
