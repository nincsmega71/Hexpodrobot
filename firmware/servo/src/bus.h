#ifndef BUS_H
#define BUS_H

#include <stdint.h>

/* Utasításkódok */
#define INS_PING           0x01
#define INS_READ           0x02
#define INS_WRITE          0x03
#define INS_REG_WRITE      0x04
#define INS_ACTION         0x05
#define INS_FACTORY_RESET  0x06
#define INS_SYNC_READ      0x82
#define INS_SYNC_WRITE     0x83

void     bus_init(void);
void     bus_poll(void);          /* főciklusból hívandó */
uint8_t  bus_save_pending(void);
void     bus_save_done(void);

#endif
