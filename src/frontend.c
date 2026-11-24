/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <audsrv.h>
#include <dmaKit.h>
#include <gsKit.h>
#include <kernel.h>
#include <libpad.h>

#include "display.h"
#include "frame_clock.h"
#include "intro_audio.h"
#include "audio_stream.h"
#include "iop_modules.h"
#include "memory_card.h"
#include "libretro.h"
#include "libretro_gskit_ps2.h"
#include "lp_core.h"
#ifdef LP_PERF
#include "performance.h"
#endif

extern const uint8_t lp_english[], lp_english_end[], lp_japanese[], lp_japanese_end[];

enum { PANEL_X = 52, PANEL_Y = 42, PANEL_W = 536, PANEL_H = 364, MENU_W = 640, MENU_H = 384 };
enum { BACKGROUND, HIGHLIGHT, INK, SUBDUED, ACCENT };
#define CLUT(r, g, b) (0x8000 | ((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10))
static uint16_t menu_colors[16] __attribute__((aligned(64))) = {
    CLUT(10,22,36), CLUT(22,51,68), CLUT(233,244,250), CLUT(143,166,181), CLUT(118,224,219),
};
static uint8_t menu_pixels[MENU_W * MENU_H / 2] __attribute__((aligned(64)));
static uint8_t frame_pixels[256 * 256] __attribute__((aligned(64)));
static uint16_t frame_colors[256] __attribute__((aligned(64)));
static uint8_t pad_buffer[256] __attribute__((aligned(64)));
static GSTEXTURE menu_texture = {
    .Width = MENU_W, .Height = MENU_H, .PSM = GS_PSM_T4, .ClutPSM = GS_PSM_CT16,
    .Mem = (u32 *)menu_pixels, .Clut = (u32 *)menu_colors, .Filter = GS_FILTER_NEAREST,
};
static GSTEXTURE frame_texture = {
    .Width = 256, .Height = 256, .PSM = GS_PSM_T8, .ClutPSM = GS_PSM_CT16,
    .Mem = (u32 *)frame_pixels, .Clut = (u32 *)frame_colors,
};
static GSTEXTURE wide_texture = {
    .Width = LP_WIDE_PITCH, .Height = 256, .PSM = GS_PSM_T8, .ClutPSM = GS_PSM_CT16,
    .Clut = (u32 *)frame_colors,
};
enum { NOTICE_W = 512, NOTICE_H = 32 };
static uint8_t notice_pixels[NOTICE_W * NOTICE_H / 2] __attribute__((aligned(64)));
static GSTEXTURE notice_texture = {
    .Width = NOTICE_W, .Height = NOTICE_H, .PSM = GS_PSM_T4, .ClutPSM = GS_PSM_CT16,
    .Mem = (u32 *)notice_pixels, .Clut = (u32 *)menu_colors, .Filter = GS_FILTER_NEAREST,
};
static const char *cached_notice;
static GSTEXTURE core_texture;
static RETRO_HW_RENDER_INTEFACE_GSKIT_PS2 render_interface = {
    .interface_type = RETRO_HW_RENDER_INTERFACE_GSKIT_PS2,
    .interface_version = RETRO_HW_RENDER_INTERFACE_GSKIT_PS2_VERSION,
    .coreTexture = &core_texture,
};
static GSGLOBAL *gs;
static volatile uint32_t vblank_fields;
static volatile int queued_buffer = -1;
static int vblank_semaphore;

static const char *const signal_names[] = {"480I", "480P", "720P"};
static const char *const aspect_names[] = {"4:3", "16:9"};
static enum lp_signal signal, previous_signal;
static enum lp_aspect aspect;
static unsigned selected_row, test_fields, suppress_game_buttons;
static int16_t input_mask;
static bool japanese, started, settings, audio_ready, pad_open, frame_ready, frame_dirty;
static bool frame_wide, standard_cached;
static const uint8_t *game_ram;
static uint8_t *cartridge_ram;
static struct lp_intro_audio intro_audio;
static int16_t balanced_pcm[2 * 1024] __attribute__((aligned(64)));
static const char *status;
static const unsigned settings_chord = PAD_L2 | PAD_R2;
static const u64 texture_color = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0);

static int on_vblank(int cause)
{
    (void)cause;
    int buffer = queued_buffer;
    if (buffer >= 0 && (GS_CSR_FINISH)) {
        GS_SET_DISPFB2(gs->ScreenBuffer[buffer] / 8192, gs->Width / 64, gs->PSM, 0, 0);
        queued_buffer = -1;
#ifdef LP_PERF
        ++lp_perf_presented;
#endif
    }
    ++vblank_fields;
    iSignalSema(vblank_semaphore);
    ExitHandler();
    return 0;
}

static bool init_vblank(void)
{
    ee_sema_t semaphore = {.init_count = 0, .max_count = 1};
    vblank_semaphore = CreateSema(&semaphore);
    if (vblank_semaphore < 0)
        return false;
    if (AddIntcHandler(INTC_VBLANK_S, on_vblank, 0) < 0) {
        DeleteSema(vblank_semaphore);
        return false;
    }
    EnableIntc(INTC_VBLANK_S);
    return true;
}

static uint32_t wait_for_field(uint32_t previous)
{
    uint32_t current;
    while ((current = vblank_fields) == previous)
        WaitSema(vblank_semaphore);
    return current;
}

static bool allocate_texture(GSTEXTURE *texture)
{
    texture->Vram = gsKit_vram_alloc(gs, gsKit_texture_size(texture->Width, texture->Height, texture->PSM),
                                     GSKIT_ALLOC_USERBUFFER);
    texture->VramClut = gsKit_vram_alloc(gs, gsKit_texture_size(16, 16, GS_PSM_CT16),
                                         GSKIT_ALLOC_USERBUFFER);
    return texture->Vram != GSKIT_ALLOC_ERROR && texture->VramClut != GSKIT_ALLOC_ERROR;
}

static bool configure_display(enum lp_signal mode)
{
    const short modes[] = {GS_MODE_NTSC, GS_MODE_DTV_480P, GS_MODE_DTV_720P};
    queued_buffer = -1;
    if (gs->FirstFrame == GS_SETTING_OFF)
        gsKit_finish();
    gs->Mode = modes[mode];
    gs->Interlace = mode == SIGNAL_480I ? GS_INTERLACED : GS_NONINTERLACED;
    gs->Field = mode == SIGNAL_480I ? GS_FIELD : GS_FRAME;
    gs->Width = mode == SIGNAL_720P ? 1280 : 640;
    gs->Height = mode == SIGNAL_480I ? 448 : mode == SIGNAL_480P ? 480 : 720;
    gs->PSM = GS_PSM_CT16;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->ActiveBuffer = 1;
    gsKit_vram_clear(gs);
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    signal = mode;
    frame_dirty = frame_ready;
    if (!allocate_texture(&wide_texture) || !allocate_texture(&menu_texture) ||
        !allocate_texture(&notice_texture))
        return false;
    /* Only one game texture is on screen. Both formats share the larger allocation. */
    cached_notice = NULL;
    frame_texture.Vram = wide_texture.Vram;
    frame_texture.VramClut = wide_texture.VramClut;
    return true;
}

static void revert_display(void)
{
    configure_display(previous_signal);
    test_fields = 0;
    status = "VIDEO MODE RESTORED";
}

static void box(unsigned x, unsigned y, unsigned w, unsigned h, unsigned color)
{
    /* The menu's five colors fit in a four-bit texture beside the 720p buffers. */
    uint8_t *row = menu_pixels + (y - PANEL_Y) * (MENU_W / 2);
    unsigned start = x - PANEL_X, end = start + w;
    unsigned first_full = (start + 1) / 2, last_full = end / 2;
    unsigned pair = color | (color << 4);
    for (unsigned i = 0; i < h; ++i, row += MENU_W / 2) {
        if (start & 1)
            row[start / 2] = (row[start / 2] & 0x0f) | (color << 4);
        if (end & 1)
            row[end / 2] = (row[end / 2] & 0xf0) | color;
        memset(row + first_full, pair, last_full - first_full);
    }
}

/* Five column masks per glyph with the lowest bit at the top. */
static const uint8_t letters[][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36}, {0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c}, {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f}, {0x41,0x41,0x7f,0x41,0x41},
    {0x20,0x40,0x41,0x3f,0x01}, {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f}, {0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,0x09,0x09,0x09,0x06}, {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01}, {0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f}, {0x7f,0x20,0x18,0x20,0x7f}, {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43},
};
static const uint8_t digits[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0,0x42,0x7f,0x40,0}, {0x62,0x51,0x49,0x49,0x46},
    {0x22,0x41,0x49,0x49,0x36}, {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, {0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1e},
};
static const uint8_t colon[] = {0, 0x24, 0x24, 0, 0};

static void label(unsigned x, unsigned y, unsigned size, const char *s, unsigned color)
{
    for (; *s; ++s, x += 6 * size) {
        const uint8_t *glyph;
        if (*s >= 'A' && *s <= 'Z') glyph = letters[*s - 'A'];
        else if (*s >= '0' && *s <= '9') glyph = digits[*s - '0'];
        else if (*s == ':') glyph = colon;
        else continue;
        for (unsigned col = 0; col < 5; ++col)
            for (unsigned row = 0; row < 7; ++row)
                if (glyph[col] & (1u << row))
                    box(x + col * size, y + row * size, size, size, color);
    }
}

static void draw_row(unsigned row, const char *name, const char *value)
{
    unsigned y = 132 + row * 40;
    bool focused = row == selected_row;
    if (focused) {
        box(69, y - 8, 502, 28, HIGHLIGHT);
        box(69, y - 8, 3, 28, ACCENT);
    }
    label(83, y, 2, name, focused ? ACCENT : SUBDUED);
    label(356, y, 2, value, INK);
}

#ifdef LP_PERF
static void draw_performance(void)
{
    static const char *const phases[]={"WORK","CORE","WIDE","AUDIO","RENDER"};
    char line[80];
    if(!lp_perf_last.frames){label(83,248,1,"NO PERFORMANCE SAMPLE YET",SUBDUED);return;}
    for(unsigned i=0;i<LP_PERF_PHASES;++i) {
        snprintf(line,sizeof(line),"%s AVG %u MAX %u US",phases[i],
                 lp_perf_us(lp_perf_last.sum[i]/lp_perf_last.frames),lp_perf_us(lp_perf_last.peak[i]));
        label(83,242+i*12,1,line,SUBDUED);
    }
    snprintf(line,sizeof(line),"FRAMES %u FIELDS %u PRESENTS %u",(unsigned)lp_perf_last.frames,
             (unsigned)lp_perf_last.fields,(unsigned)lp_perf_last.presents);
    label(83,302,1,line,SUBDUED);
    struct lp_audio_stats audio=lp_audio_stream_stats();
    snprintf(line,sizeof(line),"AUDIO DROPS %u ERRORS %u",audio.dropped,audio.errors);
    label(83,314,1,line,SUBDUED);
}
#endif

static void draw_menu(void)
{
    box(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, BACKGROUND);
    box(PANEL_X, PANEL_Y, PANEL_W, 3, ACCENT);
    label(83, 72, 3, started ? "OPTIONS" : "LAGRANGE POINT", INK);
    if (test_fields) {
        char countdown[24];
        snprintf(countdown, sizeof(countdown), "REVERTS IN %u S", (test_fields + 59) / 60);
        label(83, 157, 2, "KEEP THIS VIDEO MODE", INK);
        label(83, 196, 2, countdown, ACCENT);
        label(83, 354, 2, "CROSS KEEP   CIRCLE REVERT", SUBDUED);
    } else {
        draw_row(0, "SCREEN", aspect_names[aspect]);
        draw_row(1, "OUTPUT", signal_names[signal]);
        if (!started) {
            draw_row(2, "LANGUAGE", japanese ? "JAPANESE" : "ENGLISH");
            draw_row(3, "PLAY", "");
        } else draw_row(2, "RESUME", "");
#ifdef LP_PERF
        if(started) draw_performance();
        else {
#endif
        if (status) label(83, 295, 2, status, ACCENT);
        const char *card = lp_card_status();
        if (card) label(83, 319, 1, card, SUBDUED);
#ifdef LP_PERF
        }
#endif
        label(83, 354, 2, "DPAD CHANGE   CROSS SELECT", SUBDUED);
        label(83, 378, 2, started ? "CIRCLE RESUME" : "START PLAY   L2 + R2 OPTIONS", SUBDUED);
    }
    float sx = gs->Width / 640.0f, sy = gs->Height / 448.0f;
    gsKit_texture_upload(gs, &menu_texture);
    gsKit_prim_sprite_texture(gs, &menu_texture, PANEL_X * sx, PANEL_Y * sy, 0, 0,
                              (PANEL_X + PANEL_W) * sx, (PANEL_Y + PANEL_H) * sy,
                              PANEL_W, PANEL_H, 0, texture_color);
}

static void draw_card_notice(void)
{
    const char *text = lp_card_notice();
    if (!text) return;
    if (text != cached_notice) {
        memset(notice_pixels, 0, sizeof(notice_pixels));
        unsigned x = 10;
        for (const char *p = text; *p && x + 10 < NOTICE_W; ++p, x += 12) {
            const uint8_t *glyph = *p >= 'A' && *p <= 'Z' ? letters[*p - 'A'] :
                                   *p >= '0' && *p <= '9' ? digits[*p - '0'] : NULL;
            if (!glyph) continue;
            for (unsigned col = 0; col < 5; ++col)
                for (unsigned row = 0; row < 7; ++row)
                    if (glyph[col] & (1u << row))
                        for (unsigned yy = 0; yy < 2; ++yy)
                            for (unsigned xx = 0; xx < 2; ++xx) {
                                unsigned px = x + col * 2 + xx, py = 9 + row * 2 + yy;
                                unsigned offset = py * NOTICE_W + px;
                                notice_pixels[offset / 2] |= INK << ((offset & 1) * 4);
                            }
        }
        gsKit_texture_upload(gs, &notice_texture);
        cached_notice = text;
    }
    float sx = gs->Width / 640.0f, sy = gs->Height / 448.0f;
    float width = NOTICE_W * sx, height = NOTICE_H * sy, x = (gs->Width - width) / 2;
    gsKit_prim_sprite_texture(gs, &notice_texture, x, gs->Height - height, 0, 0,
                              x + width, gs->Height, NOTICE_W, NOTICE_H, 0, texture_color);
}

static void cache_standard_frame(void)
{
    memcpy(frame_pixels, core_texture.Mem, LP_FRAME_W * LP_FRAME_H);
    for (unsigned y = LP_FRAME_H; y < 256; ++y)
        memcpy(frame_pixels + y * LP_FRAME_W, frame_pixels + (LP_FRAME_H - 1) * LP_FRAME_W, LP_FRAME_W);
    standard_cached = true;
}

static void render(void)
{
    /* Keep the completed game frame while the previous buffer is queued. */
    while (queued_buffer >= 0) {
        uint32_t field = vblank_fields;
        if (queued_buffer >= 0)
            wait_for_field(field);
    }
    gsKit_setactive(gs);
    gsKit_clear(gs, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
    if (started && frame_ready) {
        bool wide = frame_wide && aspect == ASPECT_16_9;
        const float side = LP_FRAME_W / 6.0f;
        float u0 = wide ? LP_WIDE_CENTER - side : 0;
        float u1 = wide ? LP_WIDE_CENTER + LP_FRAME_W + side : LP_FRAME_W;
        struct lp_rect rect = wide ? (struct lp_rect){0, gs->Width} : lp_game_rect(gs->Width, signal, aspect);
        GSTEXTURE *texture = wide ? &wide_texture : &frame_texture;
        if (!wide && !standard_cached)
            cache_standard_frame();
        texture->Filter = GS_FILTER_NEAREST;
        if (frame_dirty) {
            gsKit_texture_upload(gs, texture);
            frame_dirty = false;
        }
        gsKit_prim_sprite_texture(gs, texture, rect.x, 0, u0, 0,
                                  rect.x + rect.width, gs->Height, u1, LP_FRAME_H, 0, texture_color);
    }
    if (!started || settings)
        draw_menu();
    else
        draw_card_notice();
    gsKit_queue_exec(gs);
    queued_buffer = gs->ActiveBuffer;
    gs->ActiveBuffer ^= 1;
}

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
    if (level >= RETRO_LOG_WARN) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
}

static bool environment(unsigned command, void *data)
{
    switch (command) {
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
        *(const void **)data = &render_interface;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;
    default:
        return false;
    }
}

static void video(const void *frame, unsigned width, unsigned height, size_t pitch)
{
    (void)pitch;
    if (frame != RETRO_HW_FRAME_BUFFER_VALID || width != 256 || height != 240)
        return;
#ifdef LP_PERF
    lp_perf_phase_begin(LP_PERF_WIDE);
#endif
    const uint8_t *expanded = lp_wide_frame();
#ifdef LP_PERF
    lp_perf_phase_end(LP_PERF_WIDE);
#endif
    frame_wide = expanded != NULL;
    if (frame_wide)
        wide_texture.Mem = (u32 *)expanded;
    standard_cached = false;
    memcpy(frame_colors, core_texture.Clut, sizeof(frame_colors));
    frame_ready = frame_dirty = true;
}

static size_t audio(const int16_t *pcm, size_t frames)
{
#ifdef LP_PERF
    lp_perf_phase_begin(LP_PERF_AUDIO);
#endif
    if (audio_ready) {
        if (frames <= sizeof(balanced_pcm) / (2 * sizeof(*balanced_pcm))) {
            /* State $0D is the opening story. $716C holds the last audio
             * command and $7175 is the driver's shared fade attenuation. */
            bool balance = game_ram && cartridge_ram && game_ram[0x32] == 0x0d &&
                           cartridge_ram[0x116c] >= 0x09 && cartridge_ram[0x116c] <= 0x80 &&
                           cartridge_ram[0x1175] == 0;
            pcm = lp_process_audio(&intro_audio, balance, LP_VOLUME_DEFAULT, pcm, balanced_pcm, frames);
        }
        lp_audio_stream_push(pcm, frames);
    }
#ifdef LP_PERF
    lp_perf_phase_end(LP_PERF_AUDIO);
#endif
    return frames;
}

static void audio_sample(int16_t left, int16_t right)
{
    int16_t pcm[] = {left, right};
    audio(pcm, 1);
}

static void input_poll(void) {}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port || index || device != RETRO_DEVICE_JOYPAD)
        return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        return input_mask;
    return id < 16 && ((unsigned)input_mask & (1u << id));
}

static unsigned pad_buttons(void)
{
    static const uint16_t mapping[] = {
        [RETRO_DEVICE_ID_JOYPAD_A] = PAD_CROSS, [RETRO_DEVICE_ID_JOYPAD_B] = PAD_SQUARE,
        [RETRO_DEVICE_ID_JOYPAD_START] = PAD_START, [RETRO_DEVICE_ID_JOYPAD_SELECT] = PAD_SELECT,
        [RETRO_DEVICE_ID_JOYPAD_UP] = PAD_UP, [RETRO_DEVICE_ID_JOYPAD_DOWN] = PAD_DOWN,
        [RETRO_DEVICE_ID_JOYPAD_LEFT] = PAD_LEFT, [RETRO_DEVICE_ID_JOYPAD_RIGHT] = PAD_RIGHT,
    };
    struct padButtonStatus pad;
    unsigned held = pad_open && padRead(0, 0, &pad) > 0 ? 0xffffu ^ pad.btns : 0;
    suppress_game_buttons &= held;
    input_mask = 0;
    if ((held & settings_chord) != settings_chord) {
        for (unsigned i = 0; i < sizeof(mapping) / sizeof(mapping[0]); ++i)
            if (held & ~suppress_game_buttons & mapping[i])
                input_mask |= 1u << i;
    }
    return held;
}

static void start_game(void)
{
    const uint8_t *base = japanese ? lp_japanese : lp_english;
    const uint8_t *end = japanese ? lp_japanese_end : lp_english_end;
    struct retro_game_info game = {
        .path = japanese ? "lagrange-jn.nes" : "lagrange-en.nes",
        .data = base,
        .size = (uintptr_t)end - (uintptr_t)base,
    };
    lp_select_release(japanese);
    started = retro_load_game(&game);
    if (started) {
        game_ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        cartridge_ram = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
        lp_set_wide(aspect == ASPECT_16_9);
        lp_card_load(japanese, cartridge_ram);
    }
    selected_row = 0;
    status = started ? NULL : "GAME DATA COULD NOT LOAD";
}

static void menu_input(unsigned pressed, uint32_t elapsed_fields)
{
    if (test_fields) {
        if (pressed & PAD_CROSS) {
            test_fields = 0;
            status = NULL;
        } else if ((pressed & PAD_CIRCLE) || elapsed_fields >= test_fields) {
            revert_display();
        } else {
            test_fields -= elapsed_fields;
        }
        return;
    }
    if (started && (pressed & PAD_CIRCLE)) {
        settings = false;
        return;
    }
    if (!started && (pressed & PAD_START)) {
        start_game();
        return;
    }
    unsigned rows = started ? 3 : 4;
    if (pressed & PAD_UP)
        selected_row = (selected_row + rows - 1) % rows;
    if (pressed & PAD_DOWN)
        selected_row = (selected_row + 1) % rows;
    if (!(pressed & (PAD_LEFT | PAD_RIGHT | PAD_CROSS)))
        return;

    bool back = (pressed & PAD_LEFT) != 0;
    status = NULL;
    switch (selected_row) {
    case 0:
        aspect = (aspect + (back ? ASPECT_COUNT - 1 : 1)) % ASPECT_COUNT;
        lp_set_wide(aspect == ASPECT_16_9);
        frame_wide = false;
        frame_dirty = frame_ready;
        break;
    case 1: {
        enum lp_signal mode = (signal + (back ? SIGNAL_COUNT - 1 : 1)) % SIGNAL_COUNT;
        previous_signal = signal;
        if (configure_display(mode)) {
            test_fields = 480;
        } else {
            revert_display();
            status = "VIDEO MODE UNAVAILABLE";
        }
        break;
    }
    case 2:
        if (!started) japanese = !japanese;
        else if (pressed & PAD_CROSS) settings = false;
        break;
    case 3:
        if (pressed & PAD_CROSS) start_game();
        break;
    }

}

static void init_io(void)
{
    struct lp_iop_services services = lp_iop_modules_init();
    status = services.error;
    pad_open = services.pad && padInit(0) > 0 && padPortOpen(0, 0, pad_buffer) > 0;
    lp_card_init(services.card);
    struct audsrv_fmt_t format = {.bits = 16, .freq = 48000, .channels = 2};
    if (services.audio && audsrv_init() == 0 && audsrv_set_format(&format) == 0) {
        audsrv_set_volume(MAX_VOLUME);
        audio_ready = lp_audio_stream_init();
    }
}

int main(void)
{
    /* Loader entry priorities differ. Keep the audio worker above the game
     * and the card worker below it, including during late-frame catch-up. */
    ChangeThreadPriority(GetThreadId(), 64);
    init_io();
    gs = gsKit_init_global();
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    if (!configure_display(SIGNAL_480I) || !init_vblank())
        return 1;

    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();
    if (!status && !pad_open)
        status = "CONTROLLER INIT FAILED";
    else if (!status && !audio_ready)
        status = "AUDIO UNAVAILABLE";

    unsigned last = 0;
    uint32_t previous_field = vblank_fields;
    for (;;) {
        uint32_t field = wait_for_field(previous_field);
        uint32_t elapsed_fields = field - previous_field;
        unsigned held = pad_buttons();
#ifdef LP_PERF
        bool measured=false;
#endif
        if (started && !settings) {
            if ((held & settings_chord) == settings_chord && (last & settings_chord) != settings_chord) {
                previous_field = field;
                settings = true;
#ifdef LP_PERF
                lp_perf_pause();
#endif
                selected_row = 0;
                if (audio_ready)
                    lp_audio_stream_pause(true);
            } else {
#ifdef LP_PERF
                lp_perf_frame_begin(vblank_fields);
                measured=true;
#endif
                lp_audio_stream_pause(false);
                previous_field = lp_consume_field(previous_field, field);
#ifdef LP_PERF
                lp_perf_phase_begin(LP_PERF_CORE);
#endif
                retro_run();
#ifdef LP_PERF
                lp_perf_phase_end(LP_PERF_CORE);
#endif
            }
        } else {
            previous_field = field;
            menu_input(held & ~last, elapsed_fields);
            if (started && !settings)
                suppress_game_buttons = held;
        }
        if (started) lp_card_poll(cartridge_ram);
        last = held;
#ifdef LP_PERF
        if(measured) lp_perf_phase_begin(LP_PERF_RENDER);
#endif
        render();
#ifdef LP_PERF
        if(measured){lp_perf_phase_end(LP_PERF_RENDER);lp_perf_frame_end(vblank_fields);}
#endif
    }
}
