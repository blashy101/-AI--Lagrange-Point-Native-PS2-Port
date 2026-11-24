/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LP_PPU_PIXELS_H
#define LP_PPU_PIXELS_H
#include <stdint.h>

/* Aligned spans whose length is a multiple of sixteen pixels. */
static inline void lp_finish_indexed_span(uint8_t *pixels, unsigned bytes, uint64_t keep, uint64_t color)
{
#ifdef RENDER_GSKIT_PS2
#define LP_SPAN_WORD(reg, offset) \
    "lq " reg ", " offset "(%0)\n\t" \
    "pand " reg ", " reg ", $8\n\t" \
    "por " reg ", " reg ", $9\n\t" \
    "sq " reg ", " offset "(%0)\n\t"
    while (bytes >= 64) {
        __asm__ volatile(
            "pcpyld $8, %1, %1\n\t" "pcpyld $9, %2, %2\n\t"
            LP_SPAN_WORD("$10", "0") LP_SPAN_WORD("$11", "16")
            LP_SPAN_WORD("$12", "32") LP_SPAN_WORD("$13", "48")
            :: "r"(pixels), "r"(keep), "r"(color)
            : "$8", "$9", "$10", "$11", "$12", "$13", "memory");
        pixels += 64;bytes -= 64;
    }
    while (bytes) {
        __asm__ volatile(
            "pcpyld $8, %1, %1\n\t" "pcpyld $9, %2, %2\n\t" LP_SPAN_WORD("$10", "0")
            :: "r"(pixels), "r"(keep), "r"(color) : "$8", "$9", "$10", "memory");
        pixels += 16;bytes -= 16;
    }
#undef LP_SPAN_WORD
#else
    typedef uint64_t pixel_word __attribute__((may_alias));
    pixel_word *words = (pixel_word *)pixels;
    for (unsigned i = 0; i < bytes / 8; ++i) words[i] = (words[i] & keep) | color;
#endif
}

/* Preserve the device runtime's indexed palette and priority-bit rules. */
static inline void lp_finish_ppu_line(uint8_t *pixels, unsigned control)
{
    unsigned gray = (control & 0x18) && (control & 1);
    unsigned emphasis = control >> 5;
    unsigned mask = gray ? 0x30 : emphasis && emphasis != 7 ? 0xff : 0x3f;
    unsigned mark = emphasis == 7 ? 0xc0 : emphasis ? 0x40 : 0x80;
    uint64_t keep = mask * UINT64_C(0x0101010101010101);
    uint64_t color = mark * UINT64_C(0x0101010101010101);
#ifdef RENDER_GSKIT_PS2
    /* Video rows are aligned to sixteen bytes. Broadcast two 64-bit masks
     * and finish sixteen pixels at a time with the EE's integer SIMD unit. */
#define LP_PIXEL_BLOCK(a, b, c, d) \
    "lq $10, " a "(%0)\n\t" "lq $11, " b "(%0)\n\t" \
    "lq $12, " c "(%0)\n\t" "lq $13, " d "(%0)\n\t" \
    "pand $10, $10, $8\n\t" "pand $11, $11, $8\n\t" \
    "pand $12, $12, $8\n\t" "pand $13, $13, $8\n\t" \
    "por $10, $10, $9\n\t" "por $11, $11, $9\n\t" \
    "por $12, $12, $9\n\t" "por $13, $13, $9\n\t" \
    "sq $10, " a "(%0)\n\t" "sq $11, " b "(%0)\n\t" \
    "sq $12, " c "(%0)\n\t" "sq $13, " d "(%0)\n\t"
    __asm__ volatile(
        "pcpyld $8, %1, %1\n\t" "pcpyld $9, %2, %2\n\t"
        LP_PIXEL_BLOCK("0", "16", "32", "48")
        LP_PIXEL_BLOCK("64", "80", "96", "112")
        LP_PIXEL_BLOCK("128", "144", "160", "176")
        LP_PIXEL_BLOCK("192", "208", "224", "240")
        :: "r"(pixels), "r"(keep), "r"(color)
        : "$8", "$9", "$10", "$11", "$12", "$13", "memory");
#undef LP_PIXEL_BLOCK
#else
    typedef uint64_t pixel_word __attribute__((may_alias));
    pixel_word *row = (pixel_word *)pixels;
    for (unsigned i = 0; i < 32; ++i)
        row[i] = (row[i] & keep) | color;
#endif
}
#endif
