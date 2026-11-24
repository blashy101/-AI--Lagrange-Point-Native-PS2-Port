/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LP_MEMORY_CARD_H
#define LP_MEMORY_CARD_H
#include <stdbool.h>
#include <stdint.h>
void lp_card_init(bool drivers_ready);
void lp_card_load(unsigned edition,uint8_t *cart);
void lp_card_poll(const uint8_t *cart);
bool lp_card_busy(void);
const char *lp_card_status(void);
const char *lp_card_notice(void);
#endif
