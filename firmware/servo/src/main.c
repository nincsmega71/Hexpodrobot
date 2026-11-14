/*
 * Hexpodrobot – busz-szervó firmware, főprogram
 * CH32V003F4U6 + DRV8876
 */
#include "ch32v00x.h"
#include "debug.h"
#include "config.h"
#include "regs.h"
#include "hw.h"
#include "bus.h"
#include "control.h"

int main(void)
{
    SystemCoreClockUpdate();
    Delay_Init();

    regs_load();
    hw_init();
    bus_init();

    uint32_t save_wait = 0;
    uint32_t blink = 0;

    while (1) {
        bus_poll();

        /* Beállítások mentése: csak kikapcsolt nyomatéknál, rövid csend után,
           mert a flash törlése alatt a megszakítások le vannak tiltva. */
        if (bus_save_pending() && !regs[REG_TORQUE_ENABLE]) {
            if (++save_wait > 20000) {
                regs_save();
                bus_save_done();
                save_wait = 0;
            }
        } else {
            save_wait = 0;
        }

        /* LED: hiba esetén villog, egyébként a LED-regisztert követi */
        if (regs[REG_ERROR]) led_set((++blink >> 15) & 1);
        else                 led_set(regs[REG_LED]);
    }
}
