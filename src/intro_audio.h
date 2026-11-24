/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LP_INTRO_AUDIO_H
#define LP_INTRO_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct lp_intro_audio {
    unsigned energy;
    unsigned gain;
};

enum { LP_AUDIO_UNITY = 4096, LP_VOLUME_COUNT = 5, LP_VOLUME_DEFAULT = 2 };
extern const unsigned lp_volume_gains[LP_VOLUME_COUNT];

/* Interleaved stereo at 48 kHz, one game step per batch. */
const int16_t *lp_process_audio(struct lp_intro_audio *state, bool balance, unsigned volume,
                               const int16_t *pcm, int16_t *output, size_t frames);

#endif
