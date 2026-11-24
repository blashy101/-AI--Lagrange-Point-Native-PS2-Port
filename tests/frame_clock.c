/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>

#include "frame_clock.h"

int main(void)
{
    assert(lp_consume_field(10, 10) == 10);
    assert(lp_consume_field(10, 11) == 11);
    assert(lp_consume_field(10, 12) == 11);
    assert(lp_consume_field(10, 13) == 11);
    assert(lp_consume_field(10, 14) == 12);
    assert(lp_consume_field(10, 130) == 128);
    assert(lp_consume_field(UINT32_MAX, 1) == 0);

    uint32_t consumed = lp_consume_field(10, 12);
    assert(consumed == 11);
    assert(lp_consume_field(consumed, 12) == 12);
}
