/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "intro_audio.h"
static int16_t input[2080] __attribute__((aligned(64))),output[2080] __attribute__((aligned(64)));
int main(void)
{
    const unsigned sizes[]={1,2,3,4,7,8,31,798,799,801,1024},shifts[]={0,1,7};
    for(unsigned n=0;n<sizeof(sizes)/sizeof(*sizes);++n)
        for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b) {
            unsigned frames=sizes[n],source=16+shifts[a],dest=16+shifts[b];
            memset(input,0x5a,sizeof(input));memset(output,0x5a,sizeof(output));
            for(unsigned i=0;i<frames*2;++i)input[source+i]=(int)((i*521+frames*103)%32767)-16383;
            struct lp_intro_audio state={.gain=8192};
            assert(lp_process_audio(&state,false,LP_VOLUME_DEFAULT,input+source,output+dest,frames)==output+dest);
            for(unsigned i=0;i<frames*2;++i)assert(output[dest+i]==input[source+i]*2);
            for(unsigned i=0;i<dest;++i)assert(output[i]==0x5a5a);
            for(unsigned i=dest+frames*2;i<2080;++i)assert(output[i]==0x5a5a);
        }
    return 0;
}
