/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "save_data.h"
static void checksum(uint8_t *p){unsigned s=0;for(unsigned i=0;i<0x28e;++i)s+=p[i];p[0x28e]=s;p[0x28f]=s>>8;}
int main(void){
 uint8_t cart[8192]={0},old[8192],p[LP_SAVE_PAYLOAD_BYTES],loaded[LP_SAVE_PAYLOAD_BYTES],r[LP_SAVE_RECORD_BYTES];
 struct lp_save_watch w;lp_save_watch_init(&w,cart);
 for(unsigned n=0;n<120;++n){cart[0]++;cart[0x11f0]++;assert(!lp_save_watch_poll(&w,cart));}
 for(unsigned i=0;i<2;++i){uint8_t *s=cart+LP_SAVE_SLOT_OFFSET+i*LP_SAVE_SLOT_BYTES;
  s[0]=0x15;s[1]=0x73;s[2]=i+8;assert(!lp_save_watch_poll(&w,cart));
  for(unsigned n=0;n<8;++n)assert(!lp_save_watch_poll(&w,cart));
  checksum(s);}
 cart[LP_SAVE_AUX_OFFSET]=0x52;assert(!lp_save_watch_poll(&w,cart));
 for(unsigned n=1;n<LP_SAVE_STABLE_FRAMES;++n)assert(!lp_save_watch_poll(&w,cart));
 assert(lp_save_watch_poll(&w,cart));lp_save_extract(p,cart);assert(lp_save_slots(p)==3);
 for(unsigned e=0;e<2;++e){uint32_t seq=0;lp_save_encode(r,p,e,14);
  assert(lp_save_decode(r,sizeof(r),e,loaded,&seq)&&seq==14&&!memcmp(p,loaded,sizeof(p)));
  assert(!lp_save_decode(r,sizeof(r),e^1,loaded,&seq));
  assert(!lp_save_decode(r,sizeof(r)-1,e,loaded,&seq)&&!lp_save_decode(r,sizeof(r)+1,e,loaded,&seq));
  for(unsigned i=0;i<sizeof(r);++i){r[i]^=1;assert(!lp_save_decode(r,sizeof(r),e,loaded,&seq));r[i]^=1;}}
 memcpy(old,cart,sizeof(cart));memset(cart,0x59,sizeof(cart));lp_save_restore(cart,p);
 for(unsigned i=0;i<sizeof(cart);++i){bool saved=(i>=LP_SAVE_SLOT_OFFSET&&i<LP_SAVE_SLOT_OFFSET+2*LP_SAVE_SLOT_BYTES)||
   (i>=LP_SAVE_AUX_OFFSET&&i<LP_SAVE_AUX_OFFSET+32);assert(cart[i]==(saved?old[i]:0x59));}
 lp_save_watch_commit(&w,p);assert(!lp_save_watch_poll(&w,cart));
 assert(lp_save_newer(1,0)&&lp_save_newer(0,UINT32_MAX)&&!lp_save_newer(UINT32_MAX,0)&&!lp_save_newer(42,42));
 return 0;
}
