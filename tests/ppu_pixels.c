/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "ppu_pixels.h"

int main(void)
{
    uint8_t memory[416] __attribute__((aligned(16)));
    for (unsigned control = 0; control < 256; ++control) {
        memset(memory, 0x5a, sizeof(memory));
        for (unsigned value = 0; value < 256; ++value)
            memory[16 + value] = value;
        lp_finish_ppu_line(memory + 16, control);
        for (unsigned value = 0; value < 256; ++value) {
            unsigned expected = value;
            if ((control & 0x18) && (control & 1)) expected &= 0x30;
            if ((control >> 5) == 7) expected = (expected & 0x3f) | 0xc0;
            else if (control & 0xe0) expected |= 0x40;
            else expected = (expected & 0x3f) | 0x80;
            assert(memory[16 + value] == expected);
        }
        for (unsigned i = 0; i < 16; ++i)
            assert(memory[i] == 0x5a && memory[272 + i] == 0x5a);
    }
    const unsigned masks[]={0x30,0x3f},marks[]={0x40,0x80,0xc0};
    for(unsigned m=0;m<2;++m)for(unsigned c=0;c<3;++c)
        for(unsigned bytes=16;bytes<=384;bytes+=16) {
            memset(memory,0x5a,sizeof(memory));
            for(unsigned i=0;i<bytes;++i)memory[16+i]=(uint8_t)i;
            lp_finish_indexed_span(memory+16,bytes,masks[m]*UINT64_C(0x0101010101010101),
                                   marks[c]*UINT64_C(0x0101010101010101));
            for(unsigned i=0;i<bytes;++i)assert(memory[16+i]==(((uint8_t)i&masks[m])|marks[c]));
            for(unsigned i=0;i<16;++i)assert(memory[i]==0x5a);
            for(unsigned i=16+bytes;i<sizeof(memory);++i)assert(memory[i]==0x5a);
        }
    return 0;
}
