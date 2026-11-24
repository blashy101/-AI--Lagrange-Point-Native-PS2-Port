/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "audio_stream.h"
#include <audsrv.h>
#include <kernel.h>
#include <stdatomic.h>
#include <string.h>

struct audio_block {
    unsigned frames, generation;
    int16_t pcm[LP_AUDIO_BLOCK_FRAMES * 2];
};
static struct audio_block blocks[LP_AUDIO_BLOCKS] __attribute__((aligned(64)));
static uint8_t stack[16384] __attribute__((aligned(16)));
static atomic_uint produced, consumed, generation, submitted, sent, dropped, errors;
static bool ready;
static int wake;

/* Each counter has one writer. Avoid read/modify/write atomics, which require
 * unsupported LL/SC helpers on the R5900; readers only need atomic snapshots. */
static void add(atomic_uint *counter, unsigned amount)
{
    atomic_store_explicit(counter, atomic_load_explicit(counter, memory_order_relaxed) + amount,
                          memory_order_relaxed);
}

static void worker(void *arg)
{
    (void)arg;
    unsigned previous = 0, unmute_after = 0;
    for (;;) {
        WaitSema(wake);
        for (;;) {
            unsigned current = atomic_load_explicit(&generation, memory_order_acquire);
            if (current != previous) {
                if (current & 1) {
                    /* Keep the IOP read head moving. Stopping it can leave a
                     * full ring that wait_audio can never drain on Resume. */
                    if (audsrv_set_volume(MIN_VOLUME) != 0) add(&errors, 1);
                    unmute_after = 0;
                } else {
                    /* Replace one complete ring of stale paused PCM while
                     * muted, then restore the existing +6 dB output path. */
                    unmute_after = LP_AUDIO_RESUME_PRIME_FRAMES;
                }
                previous = current;
            }
            unsigned read = atomic_load_explicit(&consumed, memory_order_relaxed);
            if (read == atomic_load_explicit(&produced, memory_order_acquire))
                break;
            const struct audio_block *block = &blocks[read % LP_AUDIO_BLOCKS];
            if (!(current & 1) && block->generation == current) {
                unsigned offset = 0, bytes = block->frames * 2 * sizeof(int16_t);
                while (offset < bytes) {
                    if (atomic_load_explicit(&generation, memory_order_acquire) != current)
                        break;
                    /* The IOP clamps each copy to its available ring space.
                     * Usually this single RPC accepts the entire block. Ask
                     * it to wait only after a short copy, preserving the tail. */
                    int copied = audsrv_play_audio((const char *)block->pcm + offset, bytes - offset);
                    if (copied < 0 || audsrv_get_error() != 0 ||
                        (unsigned)copied > bytes - offset || (copied & 3)) {
                        add(&errors, 1);
                        break;
                    }
                    offset += copied;
                    if (unmute_after) {
                        unsigned frames = copied / (2 * sizeof(int16_t));
                        unmute_after = frames >= unmute_after ? 0 : unmute_after - frames;
                        if (!unmute_after && atomic_load_explicit(&generation, memory_order_acquire) == current &&
                            audsrv_set_volume(MAX_VOLUME) != 0)
                            add(&errors, 1);
                    }
                    if (offset < bytes && audsrv_wait_audio(bytes - offset) != 0) {
                        add(&errors, 1);
                        break;
                    }
                }
                add(&sent, offset / (2 * sizeof(int16_t)));
            }
            atomic_store_explicit(&consumed, read + 1, memory_order_release);
        }
    }
}

bool lp_audio_stream_init(void)
{
    if (ready)
        return true;
    ee_sema_t semaphore = {.init_count = 0, .max_count = 1};
    wake = CreateSema(&semaphore);
    if (wake < 0)
        return false;
    /* Run briefly above the game thread, then sleep on RPC/semaphore waits.
     * A lower-priority worker can starve while the game catches up late fields. */
    ee_thread_t thread = {.func = worker, .stack = stack, .stack_size = sizeof(stack),
                          .gp_reg = &_gp, .initial_priority = 48};
    int id = CreateThread(&thread);
    if (id < 0 || StartThread(id, NULL) < 0) {
        if (id >= 0) DeleteThread(id);
        DeleteSema(wake);
        return false;
    }
    ready = true;
    return true;
}

void lp_audio_stream_push(const int16_t *pcm, size_t frames)
{
    if (!ready)
        return;
    unsigned current = atomic_load_explicit(&generation, memory_order_acquire);
    while (frames) {
        unsigned count = frames > LP_AUDIO_BLOCK_FRAMES ? LP_AUDIO_BLOCK_FRAMES : frames;
        unsigned write = atomic_load_explicit(&produced, memory_order_relaxed);
        if ((current & 1) || write - atomic_load_explicit(&consumed, memory_order_acquire) >= LP_AUDIO_BLOCKS) {
            add(&dropped, frames);
            return;
        }
        struct audio_block *block = &blocks[write % LP_AUDIO_BLOCKS];
        block->frames = count;
        block->generation = current;
        memcpy(block->pcm, pcm, count * 2 * sizeof(*pcm));
        atomic_store_explicit(&produced, write + 1, memory_order_release);
        add(&submitted, count);
        SignalSema(wake);
        pcm += count * 2;
        frames -= count;
    }
}

void lp_audio_stream_pause(bool paused)
{
    if (!ready)
        return;
    unsigned current = atomic_load_explicit(&generation, memory_order_relaxed);
    if ((current & 1) != (unsigned)paused) {
        atomic_store_explicit(&generation, current + 1, memory_order_release);
        SignalSema(wake);
    }
}

struct lp_audio_stats lp_audio_stream_stats(void)
{
    return (struct lp_audio_stats){atomic_load(&submitted), atomic_load(&sent),
                                   atomic_load(&dropped), atomic_load(&errors)};
}
