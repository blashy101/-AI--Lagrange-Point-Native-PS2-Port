/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "save_data.h"
#include <string.h>
static uint32_t word(const uint8_t *p) {
 return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put_word(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=v>>(8*i); }
static uint32_t crc(const uint8_t *p) {
 uint32_t c=UINT32_MAX;
 for(unsigned i=0;i<LP_SAVE_RECORD_BYTES;++i) {
  c^=i>=20&&i<24?0:p[i];
  for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320u&(0u-(c&1)));
 }
 return ~c;
}
static bool signed_slot(const uint8_t *p) { return p[0]==0x15&&p[1]==0x73; }
static bool valid_slot(const uint8_t *p) {
 unsigned sum=0;for(unsigned i=0;i<0x28e;++i)sum+=p[i];
 return (sum&65535)==(unsigned)(p[0x28e]|p[0x28f]<<8);
}
void lp_save_extract(uint8_t *p,const uint8_t *cart) {
 memcpy(p,cart+LP_SAVE_SLOT_OFFSET,2*LP_SAVE_SLOT_BYTES);
 memcpy(p+2*LP_SAVE_SLOT_BYTES,cart+LP_SAVE_AUX_OFFSET,32);
}
void lp_save_restore(uint8_t *cart,const uint8_t *p) {
 memcpy(cart+LP_SAVE_SLOT_OFFSET,p,2*LP_SAVE_SLOT_BYTES);
 memcpy(cart+LP_SAVE_AUX_OFFSET,p+2*LP_SAVE_SLOT_BYTES,32);
}
bool lp_save_payload_valid(const uint8_t *p) {
 for(unsigned i=0;i<2;++i) {
  const uint8_t *s=p+i*LP_SAVE_SLOT_BYTES;
  if(signed_slot(s)&&!valid_slot(s))return false;
 }
 return true;
}
unsigned lp_save_slots(const uint8_t *p) {
 unsigned mask=0;
 for(unsigned i=0;i<2;++i)if(signed_slot(p+i*LP_SAVE_SLOT_BYTES)&&valid_slot(p+i*LP_SAVE_SLOT_BYTES))mask|=1u<<i;
 return mask;
}
void lp_save_encode(uint8_t *r,const uint8_t *p,unsigned e,uint32_t seq) {
 memset(r,0,32);memcpy(r,"LP2CARD1",8);put_word(r+8,e);put_word(r+12,seq);
 put_word(r+16,LP_SAVE_PAYLOAD_BYTES);memcpy(r+32,p,LP_SAVE_PAYLOAD_BYTES);put_word(r+20,crc(r));
}
bool lp_save_decode(const uint8_t *r,size_t n,unsigned e,uint8_t *p,uint32_t *seq) {
 if(n!=LP_SAVE_RECORD_BYTES||memcmp(r,"LP2CARD1",8)||word(r+8)!=e||
    word(r+16)!=LP_SAVE_PAYLOAD_BYTES||word(r+24)||word(r+28)||word(r+20)!=crc(r)||
    !lp_save_payload_valid(r+32))return false;
 if(p)memcpy(p,r+32,LP_SAVE_PAYLOAD_BYTES);
 if(seq)*seq=word(r+12);
 return true;
}
bool lp_save_newer(uint32_t a,uint32_t b) { return (int32_t)(a-b)>0; }
void lp_save_watch_init(struct lp_save_watch *w,const uint8_t *cart) {
 lp_save_extract(w->committed,cart);memcpy(w->candidate,w->committed,LP_SAVE_PAYLOAD_BYTES);w->stable=0;
}
bool lp_save_watch_poll(struct lp_save_watch *w,const uint8_t *cart) {
 uint8_t p[LP_SAVE_PAYLOAD_BYTES];lp_save_extract(p,cart);
 if(memcmp(p,w->candidate,sizeof(p))) { memcpy(w->candidate,p,sizeof(p));w->stable=0;return false; }
 if(w->stable<LP_SAVE_STABLE_FRAMES)++w->stable;
 if(w->stable!=LP_SAVE_STABLE_FRAMES||!memcmp(p,w->committed,sizeof(p)))return false;
 return lp_save_payload_valid(p)&&(lp_save_slots(p)||lp_save_slots(w->committed));
}
void lp_save_watch_commit(struct lp_save_watch *w,const uint8_t *p) { memcpy(w->committed,p,LP_SAVE_PAYLOAD_BYTES); }
