/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "display.h"
int main(void) {
 assert(ASPECT_COUNT==2);
 for(enum lp_signal m=SIGNAL_480I;m<SIGNAL_COUNT;++m) {
  unsigned w=m==SIGNAL_720P?1280:640;
  struct lp_rect wide=lp_game_rect(w,m,ASPECT_16_9),orig=lp_game_rect(w,m,ASPECT_4_3);
  assert(wide.width*4==w*3&&wide.width+2*wide.x==w);
  assert(orig.width==(m==SIGNAL_720P?wide.width:w)&&orig.width+2*orig.x==w);
 }
}
