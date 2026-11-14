#include "regs.h"
#include "config.h"
#include "ch32v00x.h"
#include <string.h>

volatile uint8_t regs[REG_COUNT];

/* 0 = nem létező, 1 = csak olvasható, 2 = írható */
static const uint8_t access_map[REG_COUNT] = {
    /* 0x00 */ 1, 1, 1, 0, 0, 2, 2, 2,
    /* 0x08 */ 2, 2, 2, 2, 2, 2, 2, 0,
    /* 0x10 */ 2, 2, 2, 2, 2, 2, 2, 2,
    /* 0x18 */ 2, 2, 2, 2, 2, 2, 0, 0,
    /* 0x20 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x28 */ 2, 2, 2, 2, 2, 2, 2, 2,
    /* 0x30 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x38 */ 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x40 */ 1, 1,
};

uint16_t regs_get16(uint8_t addr)
{
    return (uint16_t)regs[addr] | ((uint16_t)regs[addr + 1] << 8);
}

void regs_set16(uint8_t addr, uint16_t v)
{
    regs[addr]     = (uint8_t)v;
    regs[addr + 1] = (uint8_t)(v >> 8);
}

void regs_defaults(void)
{
    memset((void *)regs, 0, sizeof(regs));
    regs_set16(REG_MODEL, MODEL_NUMBER);
    regs[REG_FW_VERSION]   = FW_VERSION;
    regs[REG_ID]           = 1;
    regs[REG_BAUD]         = 0;
    regs[REG_RETURN_DELAY] = 10;                 /* 20 us */
    regs_set16(REG_MIN_ANGLE, (uint16_t)(int16_t)-1350);
    regs_set16(REG_MAX_ANGLE, 1350);
    regs_set16(REG_POT_ZERO, ADC_FULL_SUM / 2);  /* középállás */
    regs[REG_DIRECTION]    = 0;
    regs_set16(REG_KP, 6 * 256);                 /* kiinduló érték, hangolni kell */
    regs_set16(REG_KI, 0);
    regs_set16(REG_KD, 20 * 256);
    regs_set16(REG_MAX_PWM, 800);                /* 8,4 V * 0,8 = 6,7 V effektív */
    regs_set16(REG_CURRENT_LIMIT, 2500);
    regs[REG_MAX_TEMP]     = 70;
    regs[REG_WATCHDOG]     = 20;                 /* 200 ms */
    regs_set16(REG_POT_SCALE, 3000);             /* 300 fok a teljes tartományra */
}

/* Flash-elrendezés: [magic 16 bit][0x00..0x27 bájtok][ellenőrző összeg 16 bit] */
#define SAVE_LEN   REG_EEPROM_END

static uint16_t sum16(const uint8_t *p, uint32_t n)
{
    uint16_t s = 0x5A5A;
    while (n--) s = (uint16_t)((s << 1) | (s >> 15)) ^ *p++;
    return s;
}

void regs_load(void)
{
    regs_defaults();
    const volatile uint16_t *f = (const volatile uint16_t *)SETTINGS_FLASH_ADDR;
    if (f[0] != SETTINGS_MAGIC) return;

    uint8_t buf[SAVE_LEN];
    const volatile uint8_t *fb = (const volatile uint8_t *)(SETTINGS_FLASH_ADDR + 2);
    for (uint32_t i = 0; i < SAVE_LEN; i++) buf[i] = fb[i];
    uint16_t stored = (uint16_t)fb[SAVE_LEN] | ((uint16_t)fb[SAVE_LEN + 1] << 8);
    if (stored != sum16(buf, SAVE_LEN)) return;

    for (uint32_t i = 0; i < SAVE_LEN; i++) {
        if (access_map[i] == 2) regs[i] = buf[i];   /* modell, verzió a firmware-ből */
    }
}

void regs_save(void)
{
    uint8_t buf[SAVE_LEN + 2];
    for (uint32_t i = 0; i < SAVE_LEN; i++) buf[i] = regs[i];
    uint16_t s = sum16(buf, SAVE_LEN);
    buf[SAVE_LEN]     = (uint8_t)s;
    buf[SAVE_LEN + 1] = (uint8_t)(s >> 8);

    __disable_irq();
    FLASH_Unlock();
    FLASH_ErasePage(SETTINGS_FLASH_ADDR);
    FLASH_ProgramHalfWord(SETTINGS_FLASH_ADDR, SETTINGS_MAGIC);
    for (uint32_t i = 0; i < sizeof(buf); i += 2) {
        uint16_t hw = buf[i] | ((i + 1 < sizeof(buf)) ? ((uint16_t)buf[i + 1] << 8) : 0xFF00);
        FLASH_ProgramHalfWord(SETTINGS_FLASH_ADDR + 2 + i, hw);
    }
    FLASH_Lock();
    __enable_irq();
}

/* 0 = rendben, egyébként hibabit */
uint8_t regs_writable(uint8_t addr, uint8_t len, uint8_t torque_on)
{
    if (len == 0 || (uint16_t)addr + len > REG_COUNT) return ERR_RANGE;
    for (uint8_t i = 0; i < len; i++) {
        if (access_map[addr + i] != 2) return ERR_RANGE;
        if (addr + i < REG_EEPROM_END && torque_on) return ERR_RANGE;
    }
    return 0;
}
