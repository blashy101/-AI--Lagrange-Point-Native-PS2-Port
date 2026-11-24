/* Included by the generated FCEUmm build. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cart.h"
#include "lp_core.h"

#ifndef PS2
static struct lp_cpu_stats lp_stats;
#define LP_COUNT_N(field, n) (lp_stats.field += (n))

void lp_get_cpu_stats(struct lp_cpu_stats *out)
{
    *out = lp_stats;
}
#else
#define LP_COUNT_N(field, n) ((void)0)
#endif
#define LP_COUNT(field) LP_COUNT_N(field, 1)

static inline __attribute__((always_inline)) uint8_t lp_read_operand(
    uint32_t address, uint16_t operand_pc, unsigned operand, unsigned count)
{
    uint32_t index = address - operand_pc;
    if (index < count)
        return _DB = (uint8_t)(operand >> (index * 8));
    return RdMemNorm(address);
}

struct lp_compiled_instruction {
    uint16_t (*run)(uint16_t pc);
    uint8_t opcode;
    uint8_t cycles;
};

static inline int lp_wide_site(unsigned pc, unsigned bank)
{
    return (uintptr_t)Page[pc >> 11] + pc - (uintptr_t)PRGptr[0] == bank * 8192 + (pc & 8191);
}

#undef _PC
#define _PC pc
#define RdRAM RdRAMFast
#define WrRAM WrRAMFast
#define RdMem(a) lp_read_operand((a), operand_pc, operand, count)
#define WrMem WrMemNorm
#include "native_handlers.inc"
#undef RdMem
#undef WrMem
#undef RdRAM
#undef WrRAM
#undef _PC
#define _PC X.PC

#include "native_map.inc"

#include "native_map_cache.inc"

#if LP_HAS_SRAM_MIX
/* The fixed-bank $E9FC–$EA17 loop, with a shorter entry at $EA09 for
 * scanline or device boundaries. Its accesses are plain cartridge RAM.
 * The interpreter clocks any final branch penalties at the next opcode. */
static inline uint16_t lp_run_sram_mix(uint16_t pc)
{
    int whole_iteration = pc == 0xe9fc;
    unsigned index = _X;
    if (index >= 16 || !lp_active_map || overclocked || encryptOpcodes || ARead[pc] != CartBR)
        return 0;
    if ((uintptr_t)Page[0xe9fc >> 11] + 0xe9fc - (uintptr_t)PRGptr[0] != 0x7e9fc)
        return 0;

    unsigned target = 0x6000 + index;
    unsigned previous = index ? target - 1 : 0x600f;
    if (ARead[target] != CartBR || BWrite[target] != CartBW ||
        (whole_iteration && (ARead[previous] != CartBR || (index && ARead[previous ^ 0x100] != CartBR))))
        return 0;

    /* The taken branch returns from $EA18 to $E9FC across a page. */
    int branch_cycles = whole_iteration && index < 15 ? 2 : 0;
    int cycles = whole_iteration ? 31 + branch_cycles + (index == 0) : 15;
    int sound_cycles = _tcount + cycles - branch_cycles;
    if (_count < cycles * 48 || !FCEU_CanBatchSoundCPU(sound_cycles) ||
        !lp_try_advance_vrc7(sound_cycles))
        return 0;

    /* CPX #0 sets carry. The three ADCs and final CPX share the regular
     * CPU macros so their flags match single-instruction execution. */
    if (whole_iteration) {
        _P |= C_FLAG;
        _A = CartBR(previous);
    }
    uint8_t x = RAM[0xf3];
    ADC;
    x = RAM[0x22];
    ADC;
    x = CartBR(target);
    ADC;
    CartBW(target, _A);

    ADDCYC(cycles);
    _tcount = branch_cycles;
    FCEU_AdvanceSoundCPU(sound_cycles);
    if (!whole_iteration) {
        _PI = _P;
        _DB = x;
        X.PC = 0xea10;
        LP_COUNT_N(compiled, 4);
        return 0xea13;
    }
    ++_X;
    x = 16;
    CPX;
    _PI = _P;
    _DB = branch_cycles ? 0xe4 : 0xd0;
    X.PC = 0xea16;
    LP_COUNT_N(compiled, 10 + (index == 0));
    return branch_cycles ? 0xe9fc : 0xea18;
}
#endif
