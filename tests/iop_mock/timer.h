#ifndef MOCK_TIMER_H
#define MOCK_TIMER_H
#include <stdint.h>
uint64_t GetTimerSystemTime(void);
static inline uint64_t MSec2TimerBusClock(uint64_t ms){return ms;}
#endif
