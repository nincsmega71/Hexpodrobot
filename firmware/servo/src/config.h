/*
 * Hexpodrobot – busz-szervó firmware
 * MCU: CH32V003F4U6 (QFN-20), 48 MHz HSI
 * H-híd: DRV8876, PH/EN mód (PMODE = GND)
 *
 * Minden hardverfüggő állandó itt van. Élesztés előtt ellenőrizd
 * a megjegyzésben "ELLENŐRIZNI" jelölt értékeket.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ---- Firmware azonosító ------------------------------------------------ */
#define FW_VERSION          0x01
#define MODEL_DS3218        0x0301
#define MODEL_MG996R        0x0302
#ifndef MODEL_NUMBER
#define MODEL_NUMBER        MODEL_DS3218   /* platformio.ini-ben felülírható */
#endif

/* ---- Lábkiosztás (portnevek; a QFN-20 fizikai lábszámait az adatlapból) -- */
/* PD5  USART1_TX  half-duplex busz                                          */
/* PC3  TIM1_CH3   PWM -> DRV8876 EN                                         */
/* PC2  GPIO       -> DRV8876 PH (irány)                                     */
/* PC0  GPIO       -> DRV8876 nSLEEP                                         */
/* PC1  GPIO in    <- DRV8876 nFAULT (belső felhúzó)                         */
/* PC5  GPIO       -> állapot-LED                                            */
/* PA2  ADC CH0    <- potméter csúszka                                       */
/* PA1  ADC CH1    <- DRV8876 IPROPI                                         */
/* PC4  ADC CH2    <- akkufeszültség-osztó (100k / 33k)                      */
/* PD2  ADC CH3    <- NTC (10k NTC GND felé, 10k felhúzó 3,3 V-ra), opció    */

/* ---- PWM ---------------------------------------------------------------- */
#define PWM_FREQ_HZ         20000
#define PWM_TOP             (48000000UL / PWM_FREQ_HZ - 1)   /* 2399 */

/* ---- Szabályzási ciklus ------------------------------------------------- */
#define CONTROL_HZ          1000

/* ---- ADC ---------------------------------------------------------------- */
#define ADC_CH_COUNT        4
#define ADC_OVERSAMPLE      16          /* potméter átlagolás ciklusonként   */
#define ADC_FULL_SUM        (1023UL * ADC_OVERSAMPLE)  /* 16368             */
#define VREF_MV             3300

/* IPROPI: I_mA = U_mV * 1000 / IPROPI_MV_PER_A
 * DRV8876 A_IPROPI ~1000 uA/A, R_IPROPI = 1 kOhm -> ~1000 mV/A
 * ELLENŐRIZNI a DRV8876 adatlapjában (A_IPROPI típusérték). */
#define IPROPI_MV_PER_A     1000

/* Akkufeszültség-osztó: Vbat = Vadc * (R1 + R2) / R2 */
#define VBAT_R1_KOHM        100
#define VBAT_R2_KOHM        33

/* ---- Védelmek ----------------------------------------------------------- */
#define VBAT_MIN_X10        60          /* 6,0 V  */
#define VBAT_MAX_X10        86          /* 8,6 V  */
#define OVERLOAD_MS         500         /* ennyi ideig tartó áramkorlát = hiba */

/* ---- Flash (beállítások) ------------------------------------------------ */
/* Az utolsó 1 KB-os lap. ELLENŐRIZNI: a CH32V003 normál törlési egysége. */
#define SETTINGS_FLASH_ADDR 0x08003C00UL
#define SETTINGS_MAGIC      0xB5E1

/* ---- Busz --------------------------------------------------------------- */
#define BUS_BROADCAST_ID    0xFE
#define BUS_MAX_PARAMS      128
#define BUS_RESP_GAP_US     20          /* SYNC_READ időrés-tartalék */

#endif
