#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>

/* A busz jelzi a szabályzónak, hogy új célpozíció vagy nyomaték-parancs jött */
extern volatile uint8_t  control_goal_changed;
extern volatile uint16_t control_bus_idle_ms;
extern volatile uint8_t  control_err_latched;   /* nyomatékot leállító hibák */

void control_clear_errors(void);

#endif
