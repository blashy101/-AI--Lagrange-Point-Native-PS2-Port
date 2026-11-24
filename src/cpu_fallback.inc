/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Keep the complete opcode interpreter out of the translated dispatch loop.
 * Guest RAM and unsupported instruction sites retain the original handlers. */
#undef _PC
#define _PC pc
#define RdRAM RdRAMFast
#define WrRAM WrRAMFast
#define RdMem RdMemNorm
#define WrMem WrMemNorm
static __attribute__((noinline, cold)) uint16_t lp_interpret_instruction(uint16_t pc, uint8_t b1)
{
    if (encryptOpcodes == 10) b1 = b1 &~0x36 | b1 >>1 &0x12 | b1 <<1 &0x24;
    if (encryptOpcodes == 11) {
        if (encryptOpcodesConfig &0x02) b1 = b1 &~0x30 | b1 >>1 &0x10 | b1 <<1 &0x20;
        if (encryptOpcodesConfig &0x40) b1 = b1 &~0xC6 | b1 >>1 &0x42 | b1 <<1 &0x84;
    }
    if (encryptOpcodes == 12) b1 = b1 &~0xC6 | b1 >>1 &0x42 | b1 <<1 &0x84;
    if (encryptOpcodes == 14) b1 = b1 &~0xC0 | b1 >>1 &0x40 | b1 <<1 &0x80;
    switch (b1) {
        #include "ops.h"
    }
    return pc;
}
#undef RdMem
#undef WrMem
#undef RdRAM
#undef WrRAM
#undef _PC
#define _PC X.PC
