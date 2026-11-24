#ifndef LP_CORE_H
#define LP_CORE_H
#include <stdint.h>

struct lp_cpu_stats {
    uint64_t compiled, mapped_fallback, ram_fallback;
};

/* 0 is English, 1 is Japanese, -1 is reference execution. */
void lp_select_release(int release);
void lp_refresh_code_page(unsigned page);
void lp_note_read_handlers(unsigned start, unsigned end);
int lp_try_advance_vrc7(int cycles);

enum { LP_FRAME_W = 256, LP_FRAME_H = 240, LP_WIDE_PITCH = 384, LP_WIDE_CENTER = 48 };

/* These are presentation-only. The indexed image uses the ordinary PS2 CLUT. */
void lp_set_wide(int enabled);
const uint8_t *lp_wide_frame(void);
extern int lp_wide_enabled;
void lp_wide_note_scroll(void);
void lp_wide_note_npc(unsigned slot);
void lp_wide_note_oam_shadow(void);
#ifndef PS2
void lp_get_cpu_stats(struct lp_cpu_stats *out);
const uint8_t *lp_wide_palette(void);
#endif

#endif
