#ifndef HW_H
#define HW_H

#include <stdint.h>
#include "config.h"

/* ADC-csatornák sorrendje a DMA-pufferben */
#define ADC_IDX_POT    0
#define ADC_IDX_CURR   1
#define ADC_IDX_VBAT   2
#define ADC_IDX_NTC    3

extern volatile uint16_t adc_buf[ADC_CH_COUNT * ADC_OVERSAMPLE];

void     hw_init(void);
void     hw_uart_init(uint32_t baud);
void     motor_enable(uint8_t on);       /* nSLEEP */
void     motor_set(int32_t duty);        /* -PWM_TOP .. +PWM_TOP, 0 = fék */
uint8_t  motor_fault(void);              /* 1, ha nFAULT aktív */
void     led_set(uint8_t on);

uint32_t adc_sum(uint8_t idx);           /* ADC_OVERSAMPLE minta összege */

#endif
