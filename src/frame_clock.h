/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LP_FRAME_CLOCK_H
#define LP_FRAME_CLOCK_H

#include <stdint.h>

static inline uint32_t lp_consume_field(uint32_t consumed, uint32_t current)
{
    uint32_t pending = current - consumed;
    /* Carry short overruns into later presentations. */
    if (pending > 3)
        return current - 2;
    return consumed + (pending != 0);
}

#endif
