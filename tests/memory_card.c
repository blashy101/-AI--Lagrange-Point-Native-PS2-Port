/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "save_data.h"
#include "memory_card.h"
static void change(uint8_t *cart,unsigned revision){
 uint8_t *p=cart+LP_SAVE_SLOT_OFFSET;memset(p,0,LP_SAVE_SLOT_BYTES);
 p[0]=0x15;p[1]=0x73;p[2]=revision;
 unsigned sum=0;for(unsigned i=0;i<0x28e;++i)sum+=p[i];p[0x28e]=sum;p[0x28f]=sum>>8;
 memset(cart+LP_SAVE_AUX_OFFSET,revision,32);
}
int main(int argc,char **argv){
 assert(argc==4);unsigned e=strtoul(argv[2],NULL,10),v=strtoul(argv[3],NULL,10);
 uint8_t cart[8192]={0};cart[0]=0xa5;lp_card_init(true);lp_card_load(e,cart);assert(cart[0]==0xa5);
 if(!strcmp(argv[1],"load")){
  assert(cart[LP_SAVE_SLOT_OFFSET]==0x15&&cart[LP_SAVE_SLOT_OFFSET+1]==0x73&&cart[LP_SAVE_SLOT_OFFSET+2]==v);
  for(unsigned i=0;i<32;++i)assert(cart[LP_SAVE_AUX_OFFSET+i]==v);
 }else if(!strcmp(argv[1],"empty")){
  assert(cart[LP_SAVE_SLOT_OFFSET]==0);for(unsigned n=0;n<120;++n)lp_card_poll(cart);assert(!lp_card_busy());
 }else{
  bool icon=!strcmp(argv[1],"icon"),icon_save=!strcmp(argv[1],"icon-save");
  unsigned complete=0;bool was_busy=false,changed=false,twice=!strcmp(argv[1],"two")||icon_save;
  if(icon_save){lp_card_poll(cart);assert(lp_card_busy());was_busy=true;change(cart,v+1);changed=true;}
  else if(!icon)change(cart,v);
  for(unsigned n=0;n<5000&&complete<(twice?2u:1u);++n){
   lp_card_poll(cart);bool now=lp_card_busy();if(twice&&now&&!changed){change(cart,v+1);changed=true;}
   if(was_busy&&!now)++complete;
   was_busy=now;
   struct timespec t={0,1000000};nanosleep(&t,NULL);
  }
  assert(complete==(twice?2u:1u));bool failed=getenv("CARD_EXPECT_FAIL")!=NULL;
  bool success=!strcmp(lp_card_status(),"SAVED TO MEMORY CARD")||
    (icon&&(!strcmp(lp_card_status(),"SAVE ICON UPDATED")||!strcmp(lp_card_status(),"MEMORY CARD SAVE LOADED")));
  assert(success!=failed);
 }
 printf("%s %u %u: %s\n",argv[1],e,v,lp_card_status());return 0;
}
