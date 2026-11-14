/*
 * 1 kHz-es szabályzási ciklus (TIM2 megszakítás)
 *  - mérés: potméter, motoráram, akkufeszültség, NTC
 *  - pálya: célpozíció felé mozgásidő vagy max. sebesség szerint
 *  - PID a pozícióra, alatta áramkorlát
 *  - védelmek: feszültség, hőmérséklet, túlterhelés, nFAULT, busz-watchdog
 *
 * Egységek: pozíció 0,1 fok (int), áram mA, feszültség 0,1 V.
 * PID: u[PWM-lépés] = (Kp*e - Kd*dpos) / 256 + (Ki * (integ / 64)) / 1024
 *      e és dpos 0,1 fokban, integ 0,1 fok * ms
 */
#include "ch32v00x.h"
#include "config.h"
#include "regs.h"
#include "hw.h"
#include "control.h"

volatile uint8_t  control_goal_changed = 0;
volatile uint16_t control_bus_idle_ms  = 0;
volatile uint8_t  control_err_latched  = 0;

static int32_t  sp_q16;          /* aktuális alapjel, 0,1 fok << 16 */
static int32_t  step_q16;        /* lépés ms-onként, 0 = ugrás */
static int32_t  integ;
static int16_t  prev_pos;
static int16_t  pos_hist[10];
static uint8_t  hist_i;
static uint16_t overload_ms;
static uint8_t  torque_was_on;

/* 10k NTC (B = 3950) 10k felhúzóval, 16 minta összege, -10..130 fok, 10 fokonként */
static const uint16_t ntc_table[] = {
    13970, 12616, 10944, 9105, 7293, 5671, 4322, 3259,
    2449, 1845, 1398, 1067, 823, 640, 503
};

static uint8_t ntc_to_c(uint32_t sum)
{
    if (sum > 16000) return 0;                       /* nincs beültetve */
    if (sum >= ntc_table[0]) return 0;
    for (uint8_t i = 1; i < sizeof(ntc_table) / sizeof(ntc_table[0]); i++) {
        if (sum >= ntc_table[i]) {
            int32_t t0 = -10 + 10 * (i - 1);
            int32_t t = t0 + 10 * (int32_t)(ntc_table[i - 1] - sum) /
                             (int32_t)(ntc_table[i - 1] - ntc_table[i]);
            return t < 0 ? 0 : (uint8_t)t;
        }
    }
    return 130;
}

void control_clear_errors(void)
{
    control_err_latched = 0;
    overload_ms = 0;
}

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void control_step(void)
{
    /* ---- mérés ---- */
    uint8_t dir    = regs[REG_DIRECTION];
    int32_t zero   = regs_get16(REG_POT_ZERO);
    int32_t scale  = regs_get16(REG_POT_SCALE);
    int32_t raw    = (int32_t)adc_sum(ADC_IDX_POT) - zero;
    int32_t pos    = raw * scale / (int32_t)ADC_FULL_SUM;
    if (dir) pos = -pos;

    uint32_t curr_mv = adc_sum(ADC_IDX_CURR) * VREF_MV / ADC_FULL_SUM;
    int32_t  curr_ma = (int32_t)(curr_mv * 1000UL / IPROPI_MV_PER_A);
    uint32_t vbat_mv = adc_sum(ADC_IDX_VBAT) * VREF_MV / ADC_FULL_SUM
                       * (VBAT_R1_KOHM + VBAT_R2_KOHM) / VBAT_R2_KOHM;
    uint8_t  vbat_x10 = (uint8_t)(vbat_mv / 100);
    uint8_t  temp_c   = ntc_to_c(adc_sum(ADC_IDX_NTC));

    /* sebesség 10 ms-os ablakon: 0,1 fok / 10 ms = fok/s */
    int16_t old = pos_hist[hist_i];
    pos_hist[hist_i] = (int16_t)pos;
    hist_i = (uint8_t)((hist_i + 1) % 10);
    int16_t speed = (int16_t)(pos - old);

    /* ---- védelmek ---- */
    uint8_t torque = regs[REG_TORQUE_ENABLE];
    if (vbat_x10 < VBAT_MIN_X10 || vbat_x10 > VBAT_MAX_X10) control_err_latched |= ERR_VOLTAGE;
    if (regs[REG_MAX_TEMP] && temp_c > regs[REG_MAX_TEMP])  control_err_latched |= ERR_OVERHEAT;
    if (torque && torque_was_on && motor_fault())           control_err_latched |= ERR_DRIVER;
    if (control_bus_idle_ms < 0xFFFF) control_bus_idle_ms++;
    if (torque && regs[REG_WATCHDOG] &&
        control_bus_idle_ms > (uint16_t)regs[REG_WATCHDOG] * 10) control_err_latched |= ERR_DRIVER;

    if (control_err_latched & ERR_TORQUE_OFF_MASK) {
        torque = 0;
        regs[REG_TORQUE_ENABLE] = 0;
    }

    /* ---- nyomaték be/ki ---- */
    if (torque && !torque_was_on) {
        sp_q16 = pos << 16;                          /* nincs ugrás bekapcsoláskor */
        integ = 0;
        prev_pos = (int16_t)pos;
        overload_ms = 0;
        control_goal_changed = 1;
        motor_enable(1);
    } else if (!torque && torque_was_on) {
        motor_enable(0);
    }
    torque_was_on = torque;

    int32_t goal = (int16_t)regs_get16(REG_GOAL_POS);
    goal = clamp(goal, (int16_t)regs_get16(REG_MIN_ANGLE), (int16_t)regs_get16(REG_MAX_ANGLE));

    if (!torque) {
        sp_q16 = pos << 16;
        motor_set(0);
    } else {
        /* ---- pálya ---- */
        if (control_goal_changed) {
            control_goal_changed = 0;
            uint16_t move_ms   = regs_get16(REG_MOVE_TIME);
            uint16_t max_speed = regs_get16(REG_MAX_SPEED);
            int32_t  dist_q16  = (goal << 16) - sp_q16;
            if (move_ms)        step_q16 = (dist_q16 < 0 ? -dist_q16 : dist_q16) / move_ms;
            else if (max_speed) step_q16 = ((int32_t)(max_speed > 30000 ? 30000 : max_speed) << 16) / 100; /* fok/s -> 0,1 fok/ms */
            else                step_q16 = 0;
        }
        int32_t goal_q16 = goal << 16;
        if (step_q16 == 0) sp_q16 = goal_q16;
        else if (sp_q16 < goal_q16) sp_q16 = (goal_q16 - sp_q16 > step_q16) ? sp_q16 + step_q16 : goal_q16;
        else if (sp_q16 > goal_q16) sp_q16 = (sp_q16 - goal_q16 > step_q16) ? sp_q16 - step_q16 : goal_q16;

        /* ---- PID ---- */
        int32_t sp  = sp_q16 >> 16;
        int32_t err = sp - pos;
        int32_t dp  = pos - prev_pos;
        int32_t kp  = regs_get16(REG_KP), ki = regs_get16(REG_KI), kd = regs_get16(REG_KD);
        int32_t max_duty = (int32_t)regs_get16(REG_MAX_PWM) * PWM_TOP / 1000;

        int32_t u = (kp * err - kd * dp) / 256 + (ki * (integ / 64)) / 1024;
        if (u > -max_duty && u < max_duty) integ = clamp(integ + err, -1000000, 1000000);
        u = clamp(u, -max_duty, max_duty);

        /* ---- áramkorlát ---- */
        int32_t limit = regs_get16(REG_CURRENT_LIMIT);
        if (limit && curr_ma > limit) {
            u = u * limit / curr_ma;
            if (++overload_ms > OVERLOAD_MS) {
                control_err_latched |= ERR_OVERLOAD;
                regs[REG_TORQUE_ENABLE] = 0;
            }
        } else if (overload_ms) {
            overload_ms--;
        }

        motor_set(dir ? -u : u);
        if (u < 0) curr_ma = -curr_ma;
    }
    prev_pos = (int16_t)pos;

    /* ---- állapot a regiszterekbe ---- */
    regs_set16(REG_PRESENT_POS, (uint16_t)(int16_t)pos);
    regs_set16(REG_PRESENT_SPEED, (uint16_t)speed);
    regs_set16(REG_PRESENT_CURRENT, (uint16_t)(int16_t)curr_ma);
    regs[REG_VOLTAGE]     = vbat_x10;
    regs[REG_TEMPERATURE] = temp_c;
    regs[REG_ERROR]       = control_err_latched;
    regs[REG_MOVING]      = (torque && ((sp_q16 >> 16) != goal || speed > 5 || speed < -5)) ? 1 : 0;
}

void TIM2_IRQHandler(void) __attribute__((interrupt));
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        control_step();
    }
}
