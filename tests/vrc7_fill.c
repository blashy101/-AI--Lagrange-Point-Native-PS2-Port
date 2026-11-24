/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#define OPLL_fillbuf reference_fillbuf
#include "../third_party/fceumm/src/boards/emu2413.c"
#undef OPLL_fillbuf
#include "../src/vrc7_fill.inc"
static unsigned seed=0x87319;
static unsigned random_value(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void)
{
    OPLL *reference=OPLL_new(3579545,48000), *candidate=OPLL_new(3579545,48000);
    assert(reference&&candidate);
    for(unsigned i=0;i<sizeof(pmtable)/sizeof(*pmtable);++i)assert(pmtable[i]>=-32768&&pmtable[i]<=32767);
    for(unsigned i=0;i<sizeof(amtable)/sizeof(*amtable);++i)assert(amtable[i]>=-32768&&amtable[i]<=32767);
    unsigned states=0, samples=0;
    for(unsigned batch=0;batch<10000;++batch) {
        unsigned reg=random_value()%26,value=random_value()&255;
        if(reg>=8)reg=0x10+(reg-8)/6*16+(reg-8)%6;
        OPLL_writeReg(reference,reg,value);OPLL_writeReg(candidate,reg,value);
        if(batch%17==0){unsigned mask=random_value()&63;OPLL_setMask(reference,mask);OPLL_setMask(candidate,mask);}
        if(batch%97==0){unsigned rates[]={44100,48000,96000};unsigned rate=rates[batch/97%3];OPLL_set_rate(reference,rate);OPLL_set_rate(candidate,rate);}
        if(batch%127==0){OPLL_reset(reference);OPLL_reset(candidate);}
        int32_t a[2048],b[2048];
        unsigned count=random_value()%2048+1,shift=(batch&1)?1:4;
        for(unsigned i=0;i<count;++i)a[i]=b[i]=(int)(random_value()&65535)-32768;
        reference_fillbuf(reference,a,count,shift);OPLL_fillbuf(candidate,b,count,shift);
        assert(!memcmp(a,b,count*sizeof(*a)));
        assert(!memcmp(reference,candidate,sizeof(*reference)));
        for(unsigned i=0;i<12;++i)states|=1u<<reference->slot[i].eg_mode;
        samples+=count;
    }
    printf("VRC7 blocks: %u bit-identical mixed samples and full chip states; envelope states=0x%x\n",samples,states);
    OPLL_delete(reference);OPLL_delete(candidate);return 0;
}
