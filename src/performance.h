/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LP_PERFORMANCE_H
#define LP_PERFORMANCE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <timer.h>
enum lp_perf_phase { LP_PERF_WORK, LP_PERF_CORE, LP_PERF_WIDE, LP_PERF_AUDIO, LP_PERF_RENDER, LP_PERF_PHASES };
struct lp_perf_window {
    uint64_t sum[LP_PERF_PHASES], peak[LP_PERF_PHASES];
    uint32_t frames, fields, presents;
};
static struct lp_perf_window lp_perf_current, lp_perf_last;
static uint64_t lp_perf_stamps[LP_PERF_PHASES];
static uint32_t lp_perf_first_field, lp_perf_first_present;
static volatile uint32_t lp_perf_presented;
static bool lp_perf_recording;
static void lp_perf_phase_begin(enum lp_perf_phase phase)
{
    if(lp_perf_recording) lp_perf_stamps[phase]=GetTimerSystemTime();
}
static void lp_perf_phase_end(enum lp_perf_phase phase)
{
    if(!lp_perf_recording) return;
    uint64_t ticks=GetTimerSystemTime()-lp_perf_stamps[phase];
    lp_perf_current.sum[phase]+=ticks;
    if(ticks>lp_perf_current.peak[phase])lp_perf_current.peak[phase]=ticks;
}
static void lp_perf_frame_begin(uint32_t field)
{
    if(!lp_perf_current.frames) {
        lp_perf_first_field=field;
        lp_perf_first_present=lp_perf_presented;
    }
    lp_perf_recording=true;
    lp_perf_phase_begin(LP_PERF_WORK);
}
static void lp_perf_frame_end(uint32_t field)
{
    lp_perf_phase_end(LP_PERF_WORK);
    lp_perf_recording=false;
    ++lp_perf_current.frames;
    lp_perf_current.fields=field-lp_perf_first_field;
    lp_perf_current.presents=lp_perf_presented-lp_perf_first_present;
    if(lp_perf_current.frames==240) {
        lp_perf_last=lp_perf_current;
        memset(&lp_perf_current,0,sizeof(lp_perf_current));
    }
}
static void lp_perf_pause(void)
{
    if(lp_perf_current.frames) lp_perf_last=lp_perf_current;
    memset(&lp_perf_current,0,sizeof(lp_perf_current));
    lp_perf_recording=false;
}
static unsigned lp_perf_us(uint64_t ticks)
{
    return (unsigned)(ticks*1000000u/kBUSCLK);
}
#endif
