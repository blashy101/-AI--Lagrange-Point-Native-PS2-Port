/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <math.h>
#include "intro_audio.h"

enum { UNITY = 4096, MAX_GAIN = UNITY * 2 };
const unsigned lp_volume_gains[LP_VOLUME_COUNT] = {UNITY, 5793, UNITY * 2, 11585, UNITY * 4};

const int16_t *lp_process_audio(struct lp_intro_audio *state, bool balance, unsigned volume,
                               const int16_t *pcm, int16_t *output, size_t frames)
{
    if (!frames)
        return pcm;

    if (volume >= LP_VOLUME_COUNT)
        volume = LP_VOLUME_DEFAULT;
    unsigned base = lp_volume_gains[volume];
    unsigned first = state->gain ? state->gain : base;
    if (!balance) {
        state->energy = 0;
        if (base == UNITY && first == UNITY)
            return pcm;
    }

    unsigned peak = 1, energy = 0, measured = 0;
    for (size_t i = 0; i < frames * 2; ++i) {
        int sample = pcm[i];
        unsigned magnitude = sample < 0 ? -sample : sample;
        if (magnitude > peak)
            peak = magnitude;
        if (balance && (i & 7) == 0) {
            unsigned level = magnitude >> 4;
            energy += level * level;
            ++measured;
        }
    }

    unsigned target = UNITY;
    if (balance) {
        energy /= measured;
        state->energy = state->energy ? (state->energy * 15 + energy + 8) / 16 : energy;
        unsigned level = (unsigned)(16.0f * sqrtf((float)state->energy));
        if (level >= 256 && level < 1550) {
            unsigned boost = 1550 * UNITY / level - UNITY;
            if (boost > MAX_GAIN - UNITY)
                boost = MAX_GAIN - UNITY;
            /* Leave near-silence and the bottom of envelopes quiet. */
            if (level < 512)
                boost = boost * (level - 256) / 256;
            target += boost;
        }
    }

    target = target * base / UNITY;
    unsigned limit = 32767 * UNITY / peak;
    if (first > limit)
        first = limit;
    unsigned last = first > target ? first - (first - target + 7) / 8 :
                                    first + (target - first + 7) / 8;
    if (last > limit)
        last = limit;
    state->gain = last;
    if (first == UNITY && last == UNITY)
        return pcm;

    /* The normal +6 dB path settles at exactly two. Peak protection above
     * guarantees that doubling these samples fits in signed sixteen bits. */
    if (first == UNITY * 2 && last == first) {
        size_t samples = frames * 2, i = 0;
#ifdef PS2
        if ((((uintptr_t)pcm | (uintptr_t)output) & 15) == 0)
            for (; i + 8 <= samples; i += 8)
                __asm__ volatile("lq $8, 0(%0)\n\t" "paddh $8, $8, $8\n\t" "sq $8, 0(%1)\n\t"
                                 :: "r"(pcm + i), "r"(output + i) : "$8", "memory");
#endif
        for (; i < samples; ++i) output[i] = pcm[i] * 2;
        return output;
    }

    /* Interpolate the gain in both channels together to avoid clicks at batch boundaries. */
    int gain = first << 12;
    int step = (((int)last - (int)first) * 4096) / (int)frames;
    for (size_t i = 0; i < frames * 2; i += 2) {
        int weight = gain >> 12;
        output[i] = ((int)pcm[i] * weight) / UNITY;
        output[i + 1] = ((int)pcm[i + 1] * weight) / UNITY;
        gain += step;
    }
    return output;
}
