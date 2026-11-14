/*
 * Regisztertábla – lásd docs/busz-protokoll.md
 * 0x00–0x27: beállítások (flash-be mentve), 0x28–0x41: futásidejű (RAM)
 * Többbájtos értékek little-endian sorrendűek.
 */
#ifndef REGS_H
#define REGS_H

#include <stdint.h>

/* Beállítások (EEPROM-terület) */
#define REG_MODEL           0x00  /* 2  R  */
#define REG_FW_VERSION      0x02  /* 1  R  */
#define REG_ID              0x05  /* 1  RW */
#define REG_BAUD            0x06  /* 1  RW 0 = 1 Mbit/s, 1 = 500 kbit/s */
#define REG_RETURN_DELAY    0x07  /* 1  RW 2 us egység */
#define REG_MIN_ANGLE       0x08  /* 2  RW 0,1 fok, előjeles */
#define REG_MAX_ANGLE       0x0A  /* 2  RW */
#define REG_POT_ZERO        0x0C  /* 2  RW ADC-összeg (0..16368) a 0 foknál */
#define REG_DIRECTION       0x0E  /* 1  RW 0 / 1 */
#define REG_KP              0x10  /* 2  RW x1/256 */
#define REG_KI              0x12  /* 2  RW x1/256 */
#define REG_KD              0x14  /* 2  RW x1/256 */
#define REG_MAX_PWM         0x16  /* 2  RW ezrelék */
#define REG_CURRENT_LIMIT   0x18  /* 2  RW mA */
#define REG_MAX_TEMP        0x1A  /* 1  RW fok C */
#define REG_WATCHDOG        0x1B  /* 1  RW 10 ms egység, 0 = ki */
#define REG_POT_SCALE       0x1C  /* 2  RW 0,1 fok a teljes ADC-tartományra */
#define REG_EEPROM_END      0x28

/* Futásidejű (RAM-terület) */
#define REG_TORQUE_ENABLE   0x28  /* 1  RW */
#define REG_LED             0x29  /* 1  RW */
#define REG_GOAL_POS        0x2A  /* 2  RW 0,1 fok */
#define REG_MOVE_TIME       0x2C  /* 2  RW ms, 0 = max. sebesség */
#define REG_MAX_SPEED       0x2E  /* 2  RW fok/s, 0 = korlátlan */
#define REG_PRESENT_POS     0x38  /* 2  R  0,1 fok */
#define REG_PRESENT_SPEED   0x3A  /* 2  R  fok/s */
#define REG_PRESENT_CURRENT 0x3C  /* 2  R  mA, előjeles */
#define REG_VOLTAGE         0x3E  /* 1  R  0,1 V */
#define REG_TEMPERATURE     0x3F  /* 1  R  fok C */
#define REG_ERROR           0x40  /* 1  R  hibabitek */
#define REG_MOVING          0x41  /* 1  R  0 / 1 */
#define REG_COUNT           0x42

/* Hibabitek */
#define ERR_VOLTAGE         (1u << 0)
#define ERR_ANGLE_LIMIT     (1u << 1)
#define ERR_OVERHEAT        (1u << 2)
#define ERR_RANGE           (1u << 3)
#define ERR_CHECKSUM        (1u << 4)
#define ERR_OVERLOAD        (1u << 5)
#define ERR_INSTRUCTION     (1u << 6)
#define ERR_DRIVER          (1u << 7)

/* A nyomatékot kikapcsoló hibák */
#define ERR_TORQUE_OFF_MASK (ERR_VOLTAGE | ERR_OVERHEAT | ERR_OVERLOAD | ERR_DRIVER)

extern volatile uint8_t regs[REG_COUNT];

void     regs_defaults(void);
void     regs_load(void);         /* flash -> regs, ha érvényes */
void     regs_save(void);         /* regs (0x00..0x27) -> flash */
uint16_t regs_get16(uint8_t addr);
void     regs_set16(uint8_t addr, uint16_t v);
uint8_t  regs_writable(uint8_t addr, uint8_t len, uint8_t torque_on);

#endif
