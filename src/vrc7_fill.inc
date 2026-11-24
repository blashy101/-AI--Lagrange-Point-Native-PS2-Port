/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Registers remain fixed throughout a fill call. Channels have independent
 * oscillator/envelope/feedback state and share only the sample's AM/PM LFO.
 * Process small blocks by channel so the EE keeps one slot pair in cache. */
void OPLL_fillbuf(OPLL * __restrict__ opll, int32_t * __restrict__ buf, int32_t len, int shift)
{
    enum { BLOCK = 64 };
    /* These immutable LFO tables and six 11-bit channel outputs fit exactly
     * in signed halves. Keep the block's working set below one KiB. */
    _Static_assert(6 * ((1 << DB2LIN_AMP_BITS) - 1) <= 32767, "VRC7 mix range");
    int16_t pm[BLOCK], am[BLOCK], mixed[BLOCK];
    while (len > 0) {
        int count = len < BLOCK ? len : BLOCK;
        for (int i = 0; i < count; ++i) {
            update_ampm(opll);
            pm[i] = opll->lfo_pm;
            am[i] = opll->lfo_am;
            mixed[i] = 0;
        }
        for (unsigned channel = 0; channel < 6; ++channel) {
            OPLL_SLOT *mod = MOD(opll, channel), *car = CAR(opll, channel);
            unsigned masked = opll->mask & OPLL_MASK_CH(channel);
            for (int i = 0; i < count; ++i) {
                calc_phase(mod, pm[i]);
                calc_envelope(mod, am[i]);
                calc_phase(car, pm[i]);
                calc_envelope(car, am[i]);
                if (!masked && car->eg_mode != FINISH)
                    mixed[i] += calc_slot_car(car, calc_slot_mod(mod));
            }
        }
        for (int i = 0; i < count; ++i)
            buf[i] += ((int16_t)mixed[i] + 32768) << shift;
        buf += count;
        len -= count;
    }
}
