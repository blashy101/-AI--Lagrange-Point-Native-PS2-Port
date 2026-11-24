/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LP_AUDIO_STREAM_H
#define LP_AUDIO_STREAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { LP_AUDIO_BLOCK_FRAMES = 1024, LP_AUDIO_BLOCKS = 4,
       LP_AUDIO_RESUME_PRIME_FRAMES = 5120 }; /* SDK's 10 x 512-frame ring. */
struct lp_audio_stats { unsigned submitted, sent, dropped, errors; };
bool lp_audio_stream_init(void);
/* Copies PCM immediately. Only the worker calls audsrv after initialization. */
void lp_audio_stream_push(const int16_t *pcm, size_t frames);
void lp_audio_stream_pause(bool paused);
struct lp_audio_stats lp_audio_stream_stats(void);
#endif
