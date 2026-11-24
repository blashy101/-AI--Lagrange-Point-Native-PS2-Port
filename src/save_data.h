/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LP_SAVE_DATA_H
#define LP_SAVE_DATA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { LP_CART_BYTES=8192, LP_SAVE_SLOT_BYTES=0x290, LP_SAVE_SLOT_OFFSET=0x1ad0,
       LP_SAVE_AUX_OFFSET=0xb10, LP_SAVE_PAYLOAD_BYTES=0x540,
       LP_SAVE_RECORD_BYTES=0x560, LP_SAVE_STABLE_FRAMES=3 };
struct lp_save_watch {
 uint8_t committed[LP_SAVE_PAYLOAD_BYTES],candidate[LP_SAVE_PAYLOAD_BYTES];
 unsigned stable;
};
void lp_save_extract(uint8_t *payload,const uint8_t *cart);
void lp_save_restore(uint8_t *cart,const uint8_t *payload);
bool lp_save_payload_valid(const uint8_t *payload);
unsigned lp_save_slots(const uint8_t *payload);
void lp_save_encode(uint8_t *record,const uint8_t *payload,unsigned edition,uint32_t sequence);
bool lp_save_decode(const uint8_t *record,size_t length,unsigned edition,uint8_t *payload,uint32_t *sequence);
bool lp_save_newer(uint32_t a,uint32_t b);
void lp_save_watch_init(struct lp_save_watch *watch,const uint8_t *cart);
bool lp_save_watch_poll(struct lp_save_watch *watch,const uint8_t *cart);
void lp_save_watch_commit(struct lp_save_watch *watch,const uint8_t *payload);
#endif
