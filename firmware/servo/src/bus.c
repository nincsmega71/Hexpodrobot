/*
 * Egyvezetékes half-duplex busz-protokoll (Dynamixel 1.0 / Feetech SCS-stílus)
 *
 *   FF FF ID LEN INS P1..Pn CHK      LEN = n + 2
 *   CHK = ~(ID + LEN + INS + P1 + .. + Pn) & 0xFF
 *
 * A vétel megszakításban fut (állapotgép), a feldolgozás és a válasz a főciklusban.
 */
#include "ch32v00x.h"
#include "debug.h"
#include "config.h"
#include "regs.h"
#include "hw.h"
#include "control.h"
#include "bus.h"

typedef struct {
    uint8_t id, len, ins, chk_ok;
    uint8_t p[BUS_MAX_PARAMS];
    uint8_t np;
} packet_t;

static volatile packet_t pkt;
static volatile uint8_t  pkt_ready;

/* REG_WRITE puffer */
static uint8_t  pend_addr, pend_len, pend_valid;
static uint8_t  pend_data[16];

static uint8_t  save_pending;
static uint8_t  reinit_uart;
static uint8_t  new_id;

/* ---- vétel ---------------------------------------------------------------- */
enum { S_H1, S_H2, S_ID, S_LEN, S_INS, S_PAR, S_CHK };
static uint8_t  st = S_H1, r_id, r_len, r_ins, r_np, r_sum;
static uint8_t  r_p[BUS_MAX_PARAMS];

static void rx_byte(uint8_t b)
{
    switch (st) {
    case S_H1: if (b == 0xFF) st = S_H2; break;
    case S_H2: st = (b == 0xFF) ? S_ID : S_H1; break;
    case S_ID:
        if (b == 0xFF) break;                      /* további fejlécbájt */
        r_id = b; r_sum = b; st = S_LEN; break;
    case S_LEN:
        if (b < 2 || b > BUS_MAX_PARAMS + 2) { st = S_H1; break; }
        r_len = b; r_sum += b; st = S_INS; break;
    case S_INS:
        r_ins = b; r_sum += b; r_np = 0;
        st = (r_len == 2) ? S_CHK : S_PAR; break;
    case S_PAR:
        r_p[r_np++] = b; r_sum += b;
        if (r_np == r_len - 2) st = S_CHK;
        break;
    case S_CHK: {
        st = S_H1;
        uint8_t my = regs[REG_ID];
        if ((r_id == my || r_id == BUS_BROADCAST_ID) && !pkt_ready) {
            pkt.id = r_id; pkt.len = r_len; pkt.ins = r_ins; pkt.np = r_np;
            uint8_t want = (uint8_t)~r_sum;
            pkt.chk_ok = (want == b);
            for (uint8_t i = 0; i < r_np; i++) pkt.p[i] = r_p[i];
            pkt_ready = 1;
        }
        break;
    }
    }
}

void USART1_IRQHandler(void) __attribute__((interrupt));
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        rx_byte((uint8_t)USART_ReceiveData(USART1));
    }
    if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(USART1);           /* túlcsordulás törlése */
        st = S_H1;
    }
}

/* ---- adás ------------------------------------------------------------------ */
static void tx_status(uint8_t err, const uint8_t *data, uint8_t n)
{
    uint8_t id = regs[REG_ID];
    uint8_t len = (uint8_t)(n + 2);
    uint8_t sum = (uint8_t)(id + len + err);
    for (uint8_t i = 0; i < n; i++) sum += data[i];

    USART1->CTLR1 &= ~USART_CTLR1_RE;              /* saját visszhang kikapcsolása */
    uint8_t hdr[5] = { 0xFF, 0xFF, id, len, err };
    for (uint8_t i = 0; i < 5; i++) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, hdr[i]);
    }
    for (uint8_t i = 0; i < n; i++) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, data[i]);
    }
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, (uint8_t)~sum);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    USART1->CTLR1 |= USART_CTLR1_RE;
}

static void reply(uint8_t err, const uint8_t *data, uint8_t n)
{
    Delay_Us((uint32_t)regs[REG_RETURN_DELAY] * 2);
    tx_status(err | regs[REG_ERROR], data, n);
}

/* ---- írás ------------------------------------------------------------------ */
static uint8_t do_write(uint8_t addr, const uint8_t *d, uint8_t n)
{
    uint8_t e = regs_writable(addr, n, regs[REG_TORQUE_ENABLE]);
    /* a nyomatékbit kikapcsolása és a többi RAM-regiszter mindig írható */
    if (e) return e;

    uint8_t warn = 0;
    NVIC_DisableIRQ(TIM2_IRQn);                    /* 16 bites értékek atomikusan */
    for (uint8_t i = 0; i < n; i++) {
        uint8_t a = (uint8_t)(addr + i);
        if (a == REG_ID) { new_id = d[i]; continue; }       /* válasz után lép életbe */
        if (a == REG_BAUD) { regs[a] = d[i]; reinit_uart = 1; continue; }
        if (a == REG_TORQUE_ENABLE && d[i] && !regs[a]) control_clear_errors();
        regs[a] = d[i];
    }
    if (addr <= REG_GOAL_POS + 1 && addr + n > REG_GOAL_POS) {
        int16_t g  = (int16_t)regs_get16(REG_GOAL_POS);
        int16_t lo = (int16_t)regs_get16(REG_MIN_ANGLE);
        int16_t hi = (int16_t)regs_get16(REG_MAX_ANGLE);
        if (g < lo || g > hi) warn = ERR_ANGLE_LIMIT;
        control_goal_changed = 1;
    }
    if (addr < REG_EEPROM_END) save_pending = 1;
    NVIC_EnableIRQ(TIM2_IRQn);
    return warn;
}

static void apply_id_change(void)
{
    if (new_id && new_id < BUS_BROADCAST_ID) {
        regs[REG_ID] = new_id;
        save_pending = 1;
    }
    new_id = 0;
    if (reinit_uart) {
        reinit_uart = 0;
        hw_uart_init(regs[REG_BAUD] == 1 ? 500000 : 1000000);
    }
}

/* ---- feldolgozás ---------------------------------------------------------- */
static void process(void)
{
    packet_t q;
    q.id = pkt.id; q.ins = pkt.ins; q.np = pkt.np; q.chk_ok = pkt.chk_ok;
    for (uint8_t i = 0; i < q.np; i++) q.p[i] = pkt.p[i];
    pkt_ready = 0;

    uint8_t bcast = (q.id == BUS_BROADCAST_ID);
    control_bus_idle_ms = 0;

    if (!q.chk_ok) { if (!bcast) reply(ERR_CHECKSUM, 0, 0); return; }

    switch (q.ins) {
    case INS_PING:
        if (!bcast) reply(0, 0, 0);
        break;

    case INS_READ: {
        if (bcast) break;
        if (q.np != 2 || q.p[1] == 0 || (uint16_t)q.p[0] + q.p[1] > REG_COUNT) {
            reply(ERR_RANGE, 0, 0); break;
        }
        uint8_t buf[REG_COUNT];
        __disable_irq();
        for (uint8_t i = 0; i < q.p[1]; i++) buf[i] = regs[q.p[0] + i];
        __enable_irq();
        reply(0, buf, q.p[1]);
        break;
    }

    case INS_WRITE: {
        if (q.np < 2) { if (!bcast) reply(ERR_RANGE, 0, 0); break; }
        uint8_t e = do_write(q.p[0], &q.p[1], (uint8_t)(q.np - 1));
        if (!bcast) reply(e, 0, 0);
        apply_id_change();
        break;
    }

    case INS_REG_WRITE: {
        uint8_t n = (uint8_t)(q.np - 1);
        uint8_t e = (q.np < 2 || n > sizeof(pend_data)) ? ERR_RANGE
                  : regs_writable(q.p[0], n, regs[REG_TORQUE_ENABLE]);
        if (!e) {
            pend_addr = q.p[0]; pend_len = n; pend_valid = 1;
            for (uint8_t i = 0; i < n; i++) pend_data[i] = q.p[1 + i];
        }
        if (!bcast) reply(e, 0, 0);
        break;
    }

    case INS_ACTION:
        if (pend_valid) {
            pend_valid = 0;
            do_write(pend_addr, pend_data, pend_len);
            apply_id_change();
        }
        break;

    case INS_FACTORY_RESET:
        if (regs[REG_TORQUE_ENABLE]) { if (!bcast) reply(ERR_RANGE, 0, 0); break; }
        if (!bcast) reply(0, 0, 0);
        regs_defaults();
        control_clear_errors();
        regs_save();
        break;

    case INS_SYNC_WRITE: {
        /* p: addr, len, (ID, adat[len]) * n */
        if (q.np < 2 || q.p[1] == 0) break;
        uint8_t addr = q.p[0], len = q.p[1];
        for (uint16_t i = 2; i + len < q.np; i += (uint16_t)(len + 1)) {
            if (q.p[i] == regs[REG_ID]) {
                do_write(addr, &q.p[i + 1], len);
                apply_id_change();
                break;
            }
        }
        break;
    }

    case INS_SYNC_READ: {
        /* p: addr, len, ID1..IDn; mindenki a saját időrésében válaszol */
        if (q.np < 3) break;
        uint8_t addr = q.p[0], len = q.p[1];
        if (len == 0 || (uint16_t)addr + len > REG_COUNT) break;
        for (uint8_t i = 2; i < q.np; i++) {
            if (q.p[i] == regs[REG_ID]) {
                uint32_t slot = (uint32_t)(6 + len) * 10 + BUS_RESP_GAP_US;
                if (regs[REG_BAUD] == 1) slot = (uint32_t)(6 + len) * 20 + BUS_RESP_GAP_US;
                uint8_t buf[REG_COUNT];
                __disable_irq();
                for (uint8_t k = 0; k < len; k++) buf[k] = regs[addr + k];
                __enable_irq();
                Delay_Us((uint32_t)regs[REG_RETURN_DELAY] * 2 + (uint32_t)(i - 2) * slot);
                tx_status(regs[REG_ERROR], buf, len);
                break;
            }
        }
        break;
    }

    default:
        if (!bcast) reply(ERR_INSTRUCTION, 0, 0);
        break;
    }
}

void bus_init(void)
{
    hw_uart_init(regs[REG_BAUD] == 1 ? 500000 : 1000000);
}

void bus_poll(void)
{
    if (pkt_ready) process();
}

uint8_t bus_save_pending(void) { return save_pending; }
void    bus_save_done(void)    { save_pending = 0; }
