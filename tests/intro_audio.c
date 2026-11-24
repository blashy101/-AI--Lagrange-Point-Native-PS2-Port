/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "intro_audio.h"

int main(void)
{
    enum { FRAMES = 800 };
    int16_t pcm[FRAMES * 2] = {0}, output[FRAMES * 2];
    struct lp_intro_audio gain = {0};
    assert(lp_process_audio(&gain, true, 0, pcm, output, FRAMES) == pcm);

    for (unsigned i = 0; i < FRAMES; ++i) {
        pcm[i * 2] = 700;
        pcm[i * 2 + 1] = -700;
    }
    assert(lp_process_audio(&gain, false, 0, pcm, output, FRAMES) == pcm);
    for (unsigned i = 0; i < 96; ++i)
        lp_process_audio(&gain, true, 0, pcm, output, FRAMES);
    assert(output[0] >= 1380 && output[0] <= 1400 && output[1] == -output[0]);

    pcm[18] = 27000;
    pcm[19] = -27500;
    const int16_t *limit = lp_process_audio(&gain, true, 0, pcm, output, FRAMES);
    assert(limit[18] >= pcm[18] && limit[18] < 32767);
    assert(limit[19] <= pcm[19] && limit[19] >= -32767);

    for (unsigned i = 0; i < 96; ++i)
        lp_process_audio(&gain, false, 0, pcm, output, FRAMES);
    assert(lp_process_audio(&gain, false, 0, pcm, output, FRAMES) == pcm);

    for (unsigned volume = 1; volume < LP_VOLUME_COUNT; ++volume) {
        struct lp_intro_audio boosted = {0};
        pcm[18] = 700;
        pcm[19] = -700;
        const int16_t *sound = lp_process_audio(&boosted, false, volume, pcm, output, FRAMES);
        int expected = 700 * lp_volume_gains[volume] / LP_AUDIO_UNITY;
        assert(sound[0] == expected && sound[1] == -expected);
        pcm[18] = 32767;
        pcm[19] = -32768;
        sound = lp_process_audio(&boosted, false, volume, pcm, output, FRAMES);
        assert(sound[18] >= 32700 && sound[19] <= -32700);
        for (unsigned i = 0; i < FRAMES * 2; ++i)
            assert(sound[i] >= -32767 && sound[i] <= 32767);
    }
}
