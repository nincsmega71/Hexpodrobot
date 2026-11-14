#include "config.h"
#include "hw.h"
#include "ch32v00x.h"

volatile uint16_t adc_buf[ADC_CH_COUNT * ADC_OVERSAMPLE];

static void gpio(GPIO_TypeDef *port, uint16_t pin, GPIOMode_TypeDef mode)
{
    GPIO_InitTypeDef g = {0};
    g.GPIO_Pin   = pin;
    g.GPIO_Mode  = mode;
    g.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(port, &g);
}

static void pwm_init(void)
{
    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Period        = PWM_TOP;
    tb.TIM_Prescaler     = 0;
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM1, &tb);

    TIM_OCInitTypeDef oc = {0};
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse       = 0;
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC3Init(TIM1, &oc);                      /* PC3 = TIM1_CH3 */
    TIM_OC3PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);
}

static void adc_init(void)
{
    RCC_ADCCLKConfig(RCC_PCLK2_Div4);            /* 12 MHz */

    DMA_InitTypeDef d = {0};
    d.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
    d.DMA_MemoryBaseAddr     = (uint32_t)adc_buf;
    d.DMA_DIR                = DMA_DIR_PeripheralSRC;
    d.DMA_BufferSize         = ADC_CH_COUNT * ADC_OVERSAMPLE;
    d.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    d.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    d.DMA_Mode               = DMA_Mode_Circular;
    d.DMA_Priority           = DMA_Priority_High;
    d.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &d);
    DMA_Cmd(DMA1_Channel1, ENABLE);

    ADC_InitTypeDef a = {0};
    a.ADC_Mode               = ADC_Mode_Independent;
    a.ADC_ScanConvMode       = ENABLE;
    a.ADC_ContinuousConvMode = ENABLE;
    a.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    a.ADC_DataAlign          = ADC_DataAlign_Right;
    a.ADC_NbrOfChannel       = ADC_CH_COUNT;
    ADC_Init(ADC1, &a);

    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_73Cycles); /* PA2 pot   */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 2, ADC_SampleTime_73Cycles); /* PA1 áram  */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_2, 3, ADC_SampleTime_73Cycles); /* PC4 akku  */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_3, 4, ADC_SampleTime_73Cycles); /* PD2 NTC   */

    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

static void tick_init(void)
{
    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Period        = 1000000UL / CONTROL_HZ - 1;
    tb.TIM_Prescaler     = 48 - 1;               /* 1 MHz */
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tb);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef n = {0};
    n.NVIC_IRQChannel                   = TIM2_IRQn;
    n.NVIC_IRQChannelPreemptionPriority = 1;     /* a busz-vétel megszakíthatja */
    n.NVIC_IRQChannelSubPriority        = 0;
    n.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&n);
    TIM_Cmd(TIM2, ENABLE);
}

void hw_uart_init(uint32_t baud)
{
    USART_InitTypeDef u = {0};
    u.USART_BaudRate            = baud;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &u);
    USART_HalfDuplexCmd(USART1, ENABLE);         /* egyvezetékes busz a PD5-ön */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    NVIC_InitTypeDef n = {0};
    n.NVIC_IRQChannel                   = USART1_IRQn;
    n.NVIC_IRQChannelPreemptionPriority = 0;     /* legmagasabb: 10 us/bájt */
    n.NVIC_IRQChannelSubPriority        = 0;
    n.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&n);
    USART_Cmd(USART1, ENABLE);
}

void hw_init(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO |
                           RCC_APB2Periph_TIM1  | RCC_APB2Periph_ADC1 |
                           RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    gpio(GPIOC, GPIO_Pin_3, GPIO_Mode_AF_PP);    /* PWM -> EN       */
    gpio(GPIOC, GPIO_Pin_2, GPIO_Mode_Out_PP);   /* PH              */
    gpio(GPIOC, GPIO_Pin_0, GPIO_Mode_Out_PP);   /* nSLEEP          */
    gpio(GPIOC, GPIO_Pin_5, GPIO_Mode_Out_PP);   /* LED             */
    gpio(GPIOC, GPIO_Pin_1, GPIO_Mode_IPU);      /* nFAULT          */
    gpio(GPIOA, GPIO_Pin_2, GPIO_Mode_AIN);      /* pot             */
    gpio(GPIOA, GPIO_Pin_1, GPIO_Mode_AIN);      /* IPROPI          */
    gpio(GPIOC, GPIO_Pin_4, GPIO_Mode_AIN);      /* akku            */
    gpio(GPIOD, GPIO_Pin_2, GPIO_Mode_AIN);      /* NTC             */
    gpio(GPIOD, GPIO_Pin_5, GPIO_Mode_AF_PP);    /* USART1_TX, half-duplex: adás után elengedi */

    GPIO_ResetBits(GPIOC, GPIO_Pin_0 | GPIO_Pin_2 | GPIO_Pin_5);

    pwm_init();
    adc_init();
    tick_init();
}

void motor_enable(uint8_t on)
{
    if (on) GPIO_SetBits(GPIOC, GPIO_Pin_0);
    else {
        TIM_SetCompare3(TIM1, 0);
        GPIO_ResetBits(GPIOC, GPIO_Pin_0);
    }
}

void motor_set(int32_t duty)
{
    if (duty > (int32_t)PWM_TOP)  duty = PWM_TOP;
    if (duty < -(int32_t)PWM_TOP) duty = -(int32_t)PWM_TOP;
    if (duty >= 0) {
        GPIO_SetBits(GPIOC, GPIO_Pin_2);
        TIM_SetCompare3(TIM1, (uint16_t)duty);
    } else {
        GPIO_ResetBits(GPIOC, GPIO_Pin_2);
        TIM_SetCompare3(TIM1, (uint16_t)(-duty));
    }
}

uint8_t motor_fault(void)
{
    return GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_1) == Bit_RESET;
}

void led_set(uint8_t on)
{
    if (on) GPIO_SetBits(GPIOC, GPIO_Pin_5);
    else    GPIO_ResetBits(GPIOC, GPIO_Pin_5);
}

uint32_t adc_sum(uint8_t idx)
{
    uint32_t s = 0;
    for (uint32_t i = idx; i < ADC_CH_COUNT * ADC_OVERSAMPLE; i += ADC_CH_COUNT)
        s += adc_buf[i];
    return s;
}
