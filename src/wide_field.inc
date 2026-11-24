/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included by ppu.c. The map extension observes the original game's camera,
 * CHR banks, palette and NPC updates without writing guest state. */
#include "ppu_pixels.h"

enum { WIDE_EXTRA = 48, EDGE = 16, BAND = WIDE_EXTRA + EDGE, FIELD_WIDTH = 48 };
enum { BG_CLEAR = 0x40, SPRITE_CLAIMED = 0x80 };

struct wide_camera {
    int x, y;
    uint8_t map, scroll_x, scroll_y, border, flags, valid;
};

struct wide_actor {
    int16_t x, y;
    uint8_t frame, attr, family, visible;
};

static struct wide_camera wide_scroll, wide_camera;
static struct wide_actor wide_shadow[16], wide_oam[16];
static uint8_t wide_frames[16];
static unsigned wide_previous_map = 256, wide_shadow_map = 256, wide_oam_map = 256;
static uint8_t wide_hardware_oam[256];
static const uint8_t *wide_chr[8];
static uint8_t wide_colors[32], wide_control, wide_mask;
static int wide_repair_rows, wide_dialogue;
static unsigned wide_height = LP_FRAME_H, wide_repair_dialogue;
enum { REPAIR_COLS = 33, REPAIR_ROWS = 31, MESSAGE_TOP = 140 };
static uint8_t wide_repairs[REPAIR_ROWS][REPAIR_COLS];
static unsigned wide_repair_count, wide_fine_x, wide_fine_y;
static uint8_t wide_map_snapshot[48 * 45] __attribute__((aligned(16)));
static uint8_t wide_nt_snapshot[4][1024] __attribute__((aligned(16)));
static struct wide_camera wide_repair_camera;
static unsigned wide_repair_addr;
static int wide_repairs_valid;
static uint8_t wide_pixels[LP_WIDE_PITCH * 256] __attribute__((aligned(64)));
static uint64_t wide_bg_tiles[1024][8];
static uint8_t wide_bg_cached[1024], wide_cached_colors[16];
static uint16_t wide_bg_pairs[256];
static uint8_t wide_cached_patterns[4096] __attribute__((aligned(16)));
static int wide_tiles_valid;
static int lp_wide_visible;
int lp_wide_enabled;

void lp_set_wide(int enabled)
{
    lp_wide_enabled = enabled != 0;
    lp_wide_visible = wide_scroll.valid = 0;
    wide_previous_map = wide_shadow_map = wide_oam_map = 256;
    memset(wide_frames, 0, sizeof(wide_frames));
    wide_repairs_valid = wide_tiles_valid = 0;
}

static int wide_field_row(uint8_t row)
{
    /* Sixteen encoded values represent fifteen cells, also in the border. */
    return (int8_t)row - ((int8_t)row >> 4);
}

void lp_wide_note_scroll(void)
{
    const uint8_t *cart = PRGptr[0x10];
    wide_scroll.valid = RAM[0x32] == 4 && RAM[0x36] == 4 && cart;
    if (!wide_scroll.valid)
        return;
    unsigned map = RAM[0x38];
    int col = map < 4 ? RAM[0x39] : (int8_t)RAM[0x39];
    /* The world path decrements its coarse column at the start of a left step. */
    if (map < 4 && (RAM[0x40] & 2) && (RAM[0xfd] & 15))
        ++col;
    wide_scroll = (struct wide_camera){
        .x = col * 16 - ((-RAM[0xfd]) & 15),
        .y = map < 4 ? RAM[0x3c] * 8 - ((-RAM[0xfc]) & 7) :
                        wide_field_row(RAM[0x3b]) * 16 - ((-RAM[0xfc]) & 15),
        .map = map, .scroll_x = RAM[0xfd], .scroll_y = RAM[0xfc],
        .border = RAM[0x2c], .flags = cart[0x100e], .valid = 1,
    };
}

void lp_wide_note_npc(unsigned slot)
{
    if (slot >= 16)
        return;
    if (wide_previous_map != RAM[0x38]) {
        wide_previous_map = RAM[0x38];
        memset(wide_frames, 0, sizeof(wide_frames));
    }
    const uint8_t *cart = PRGptr[0x10];
    unsigned frame = RAM[8];
    /* An FF behavior command holds the previous pose. A zero projected frame
     * can mean the actor was already outside the original viewport. */
    if (!frame && cart && cart[0x1aa0 + slot] != 0xff &&
        RAM[4] == 0xff && cart[0x1ab0 + slot] == 0)
        frame = wide_frames[slot];
    wide_frames[slot] = frame;
}

void lp_wide_note_oam_shadow(void)
{
    const uint8_t *cart = PRGptr[0x10];
    wide_shadow_map = 256;
    if (!cart || RAM[0x32] != 4 || RAM[0x36] != 4)
        return;
    wide_shadow_map = RAM[0x38];
    for (unsigned slot = 1; slot < 16; ++slot) {
        unsigned native = RAM[0x300 + slot];
        int x = (int8_t)(cart[0x1a80 + slot] - RAM[0x39]) * 16 +
                ((-RAM[0xfd]) & 15) + 8 + (cart[0x1a90 + slot] >> 4);
        int raw_row = cart[0x1a60 + slot];
        if ((raw_row & 15) == 15)
            raw_row = (raw_row + 1) & 255;
        int dy = wide_field_row(raw_row) - wide_field_row(RAM[0x3b]);
        if (wide_shadow_map < 4) {
            /* World Y wraps every 120 metatile rows. */
            dy = raw_row - RAM[0x3b] - ((raw_row >> 4) - (RAM[0x3b] >> 4));
            dy = (dy + 240) % 120;
        }
        int y = dy * 16 + ((-RAM[0xfc]) & 15) + 8 + (cart[0x1a70 + slot] >> 4);
        unsigned frame = native ? native : wide_previous_map == wide_shadow_map ? wide_frames[slot] : 0;
        if (!RAM[0x340 + slot] || (!native && (dy < 0 || dy >= 16)))
            frame = 0;
        wide_shadow[slot] = (struct wide_actor){
            native ? RAM[0x320 + slot] : x, slot == 1 && native ? RAM[0x310 + slot] : y,
            frame, RAM[0x330 + slot], RAM[0xfb], native != 0,
        };
    }
}

static void lp_wide_latch_oam(void)
{
    wide_oam_map = wide_shadow_map;
    memcpy(wide_oam, wide_shadow, sizeof(wide_oam));
}

static const uint8_t *wide_prg(unsigned bank, unsigned address)
{
    return PRGptr[0] + bank * 8192 + address - 0x8000;
}

static unsigned wide_word(const uint8_t *p)
{
    return p[0] | p[1] << 8;
}

struct wide_map {
    const uint8_t *cells, *tiles, *palettes;
    int width, rows;
    unsigned blocks, world;
};

static struct wide_map wide_map_data(const struct wide_camera *cam)
{
    if (cam->map < 4) {
        static const unsigned bases[] = {0x8001, 0x9681, 0xa581, 0xb481};
        static const int widths[] = {96, 64, 64, 32};
        unsigned tiles = wide_word(wide_prg(0x1e, 0x8ffb));
        unsigned palettes = wide_word(wide_prg(0x1e, 0x9dbd));
        return (struct wide_map){wide_prg(0x30, bases[cam->map]), wide_prg(0x1e, tiles),
                                 wide_prg(0x1e, palettes), widths[cam->map], 60, (palettes - tiles) / 16, 1};
    }
    const uint8_t *p = wide_prg(0x32, 0x81d7), *cart = PRGptr[0x10];
    unsigned tiles = wide_word(p), palettes = wide_word(p + 2);
    return (struct wide_map){cart + 0x11f0, wide_prg(0x32, tiles), wide_prg(0x32, palettes),
                             FIELD_WIDTH, cart[0x2af] <= 45 ? cart[0x2af] : 45, (palettes - tiles) / 4, 0};
}

/* Low byte is the CHR tile. Bits 8–9 select its palette. */
static unsigned wide_tile(const struct wide_map *map, const struct wide_camera *cam, int x, int y)
{
    if (map->world) {
        int col = x >> 5, row = (y >> 5) + 60;
        if ((unsigned)col >= (unsigned)map->width)
            return 0;
        row %= 60;
        unsigned block = map->cells[row * map->width + col];
        if ((cam->flags & 1) && (cam->map == 1 || cam->map == 2) && block >= 0x80 && block <= 0x82)
            block += 3;
        if (block >= map->blocks)
            return 0;
        unsigned pos = ((y >> 3) & 3) * 4 + ((x >> 3) & 3);
        unsigned shift = ((x >> 3) & 2) + ((y >> 2) & 4);
        return map->tiles[block * 16 + pos] | ((map->palettes[block] >> shift) & 3) << 8;
    }
    int col = x >> 4, row = y >> 4;
    unsigned block = cam->border;
    if ((unsigned)col < FIELD_WIDTH && (unsigned)row < (unsigned)map->rows) {
        block = map->cells[row * FIELD_WIDTH + col];
        if (cam->map == 8 && (cam->flags & 4) && block == 0x35)
            block = 0x23;
    }
    if (block >= map->blocks)
        return 0;
    unsigned quadrant = ((y >> 2) & 2) | ((x >> 3) & 1);
    return map->tiles[block * 4 + quadrant] | (map->palettes[block] & 3) << 8;
}

/* Pattern and nametable pages are naturally eight-byte aligned. */
static int wide_same_page(const uint8_t *a, const uint8_t *b, unsigned bytes)
{
    if (((uintptr_t)a | (uintptr_t)b) & 7)
        return memcmp(a, b, bytes) == 0;
    typedef uint64_t memory_word __attribute__((may_alias));
    const memory_word *left = (const memory_word *)a, *right = (const memory_word *)b;
    for (unsigned i = 0; i < bytes / 8; ++i)
        if (left[i] != right[i])
            return 0;
    return 1;
}

/* Compare background tiles before the dialogue IRQ changes the PPU scroll.
 * Cache the result while the camera, live map and nametables are unchanged. */
static void wide_find_repairs(const struct wide_map *map, unsigned sx, unsigned sy)
{
    unsigned cells = map->world ? 0 : map->width * map->rows;
    wide_fine_x = sx & 7;
    wide_fine_y = sy & 7;
    struct wide_camera key = wide_scroll;
    key.x -= wide_fine_x;
    key.y -= wide_fine_y;
    key.scroll_x >>= 3;
    key.scroll_y >>= 3;
    unsigned address = RefreshAddr & 0xfff;
    int changed = !wide_repairs_valid || wide_repair_dialogue != (unsigned)wide_dialogue ||
                  wide_repair_addr != address ||
                  wide_repair_camera.x != key.x || wide_repair_camera.y != key.y ||
                  wide_repair_camera.map != key.map || wide_repair_camera.border != key.border ||
                  wide_repair_camera.flags != key.flags || wide_repair_camera.scroll_x != key.scroll_x ||
                  wide_repair_camera.scroll_y != key.scroll_y ||
                  (cells && !wide_same_page(wide_map_snapshot, map->cells, cells));
    for (unsigned page = 0; page < 4; ++page)
        changed |= !wide_same_page(wide_nt_snapshot[page], vnapage[page], 1024);
    if (!changed)
        return;
    wide_repair_dialogue = wide_dialogue;
    wide_repair_camera = key;
    wide_repair_addr = address;
    if (cells)
        memcpy(wide_map_snapshot, map->cells, cells);
    for (unsigned page = 0; page < 4; ++page)
        memcpy(wide_nt_snapshot[page], vnapage[page], 1024);
    wide_repair_count = 0;
    for (unsigned row = 0; row < REPAIR_ROWS; ++row) {
        for (unsigned col = 0; col < REPAIR_COLS; ++col) {
            unsigned nx = (sx >> 3) + col, ny = (sy >> 3) + row;
            unsigned page = ((RefreshAddr >> 10) & 3) ^ (nx >> 5) ^ (ny >= 30 ? 2 : 0);
            ny %= 30;
            nx &= 31;
            const uint8_t *nt = vnapage[page];
            unsigned shift = (nx & 2) + ((ny & 2) << 1);
            unsigned palette = (nt[960 + (ny >> 2) * 8 + (nx >> 2)] >> shift) & 3;
            unsigned actual = nt[ny * 32 + nx] | palette << 8;
            unsigned expected = wide_tile(map, &wide_scroll,
                wide_scroll.x - (int)wide_fine_x + col * 8,
                wide_scroll.y - (int)wide_fine_y + row * 8);
            int x = (int)col * 8 - wide_fine_x, y = (int)row * 8 - wide_fine_y;
            int drawable = x < LP_FRAME_W - EDGE && x + 8 > EDGE &&
                           y + 8 > EDGE && y < LP_FRAME_H - EDGE &&
                           (!wide_dialogue || y < MESSAGE_TOP);
            wide_repairs[row][col] = drawable && actual != expected;
            wide_repair_count += wide_repairs[row][col];
        }
    }
    wide_repairs_valid = 1;
}

static void lp_wide_begin_picture(void)
{
    if (!wide_scroll.valid || !PRGptr[0] || !PRGptr[0x10] || !ScreenON || !SpriteON ||
        RAM[0x32] != 4 || RAM[0x36] != 4 || RAM[0x38] != wide_scroll.map)
        return;
    unsigned sy = ((RefreshAddr & 0x3e0) >> 2) | ((RefreshAddr >> 12) & 7);
    unsigned sx = (RefreshAddr & 31) * 8 + XOffset;
    if (sx != wide_scroll.scroll_x || sy != wide_scroll.scroll_y)
        return;

    /* Confirm the active nametable matches the map before extending it.
     * Menus and scripted screens can share the field state. */
    struct wide_map map = wide_map_data(&wide_scroll);
    unsigned matches = 0, samples = 0;
    wide_dialogue = (RAM[0xd6] & 8) != 0;
    for (unsigned row = 3; row <= 24; row += 7) {
        if (wide_dialogue && row * 8 >= MESSAGE_TOP)
            continue;
        for (unsigned col = 3; col <= 28; col += 5) {
            unsigned nx = (sx >> 3) + col, ny = (sy >> 3) + row;
            unsigned page = ((RefreshAddr >> 10) & 3) ^ (nx >> 5) ^ (ny >= 30 ? 2 : 0);
            unsigned address = (ny % 30) * 32 + (nx & 31);
            int x = wide_scroll.x - (sx & 7) + col * 8, y = wide_scroll.y - (sy & 7) + row * 8;
            ++samples;
            matches += vnapage[page][address] == (uint8_t)wide_tile(&map, &wide_scroll, x, y);
        }
    }
    if (matches + 4 < samples)
        return;

    wide_camera = wide_scroll;
    wide_control = PPU[0];
    wide_mask = PPU[1];
    /* The message overlay is applied after map and actor repairs. */
    wide_repair_rows = wide_dialogue || (!RAM[0xd6] && !RAM[0x4b] && !RAM[0x36c]);
    wide_find_repairs(&map, sx, sy);
    for (unsigned i = 0; i < 8; ++i)
        wide_chr[i] = VPage[i] + i * 1024;
    memcpy(wide_colors, PALRAM, sizeof(wide_colors));
    memcpy(wide_hardware_oam, SPRAM, sizeof(wide_hardware_oam));
    lp_wide_visible = 1;
}

static const uint8_t *wide_pattern(unsigned addr)
{
    return wide_chr[addr >> 10] + (addr & 1023);
}

static unsigned wide_bg_pen(unsigned code, unsigned x, unsigned y)
{
    unsigned address = (wide_control & 0x10) * 256 + (code & 255) * 16;
    const uint8_t *p = wide_pattern(address);
    unsigned bit = 7 - (x & 7), line = y & 7;
    return ((p[line] >> bit) & 1) | ((p[line + 8] >> bit) & 1) * 2;
}

static uint8_t wide_bg_color(unsigned pen, unsigned palette)
{
    return pen ? wide_colors[palette * 4 + pen] : wide_colors[0] | BG_CLEAR;
}

static void wide_prepare_tiles(unsigned bank)
{
    int colors_changed = !wide_tiles_valid || memcmp(wide_cached_colors, wide_colors, 16) != 0;
    if (colors_changed) {
        memcpy(wide_cached_colors, wide_colors, 16);
        memset(wide_bg_cached, 0, sizeof(wide_bg_cached));
        for (unsigned pair = 0; pair < 256; ++pair) {
            unsigned a = pair & 15, b = pair >> 4;
            wide_bg_pairs[pair] = wide_bg_color(a & 3, a >> 2) | wide_bg_color(b & 3, b >> 2) << 8;
        }
    }
    /* This cartridge uses writable CHR. Bank pointers alone do not describe
     * its contents; invalidate only tiles whose sixteen pattern bytes changed. */
    for (unsigned page = 0; page < 4; ++page) {
        const uint8_t *patterns = wide_chr[bank / 1024 + page];
        uint8_t *cached = wide_cached_patterns + page * 1024;
        if (wide_tiles_valid && wide_same_page(cached, patterns, 1024))
            continue;
        for (unsigned tile = 0; tile < 64; ++tile) {
            if (wide_tiles_valid && wide_same_page(cached + tile * 16, patterns + tile * 16, 16))
                continue;
            unsigned code = page * 64 + tile;
            for (unsigned palette = 0; palette < 4; ++palette)
                wide_bg_cached[code + palette * 256] = 0;
        }
        memcpy(cached, patterns, 1024);
    }
    wide_tiles_valid = 1;
}

static const uint64_t *wide_build_tile(unsigned code, unsigned bank)
{
    uint64_t *tile = wide_bg_tiles[code];
    if (!wide_bg_cached[code]) {
        const uint8_t *p = wide_pattern(bank + (code & 255) * 16);
        uint32_t palette = (code >> 8) * 0x44444444u;
        for (unsigned y = 0; y < 8; ++y) {
            uint32_t pens = ppulut1[p[y]] | ppulut2[p[y + 8]] | palette;
            tile[y] = (uint64_t)wide_bg_pairs[pens & 255] | (uint64_t)wide_bg_pairs[(pens >> 8) & 255] << 16 |
                      (uint64_t)wide_bg_pairs[(pens >> 16) & 255] << 32 | (uint64_t)wide_bg_pairs[pens >> 24] << 48;
        }
        wide_bg_cached[code] = 1;
    }
    return tile;
}

static void wide_background(const struct wide_map *map)
{
    unsigned pattern_bank = (wide_control & 0x10) * 256;
    wide_prepare_tiles(pattern_bank);
    for (unsigned region = 0; region < (wide_repair_rows ? 4u : 2u); ++region) {
        int position = region < 2 ? (region ? LP_FRAME_W - EDGE : -WIDE_EXTRA) : 0;
        unsigned width = region < 2 ? BAND : LP_FRAME_W;
        unsigned start_y = region == 3 ? LP_FRAME_H - EDGE : 0;
        unsigned end_y = region < 2 ? LP_FRAME_H : start_y + EDGE;
        if (end_y > wide_height) end_y = wide_height;
        int x = wide_camera.x + position;
        unsigned fine = x & 7;
        int offset = LP_WIDE_CENTER + position;
        const uint64_t *tiles[LP_FRAME_W / 8 + 1];
        unsigned count = width / 8 + (fine != 0);
        unsigned shift = fine * 8;
        typedef uint64_t pixel_lane __attribute__((may_alias));
        for (unsigned line = start_y; line < end_y; ++line) {
            int y = wide_camera.y + line;
            if ((y & 7) == 0 || line == start_y) {
                for (unsigned i = 0; i < count; ++i) {
                    unsigned code = wide_tile(map, &wide_camera, x - (int)fine + i * 8, y);
                    tiles[i] = wide_build_tile(code, pattern_bank);
                }
            }
            pixel_lane *dest = (pixel_lane *)(wide_pixels + line * LP_WIDE_PITCH + offset);
            unsigned row = y & 7;
            if (shift) {
                uint64_t last = tiles[0][row];
                for (unsigned i = 0; i < width / 8; ++i) {
                    uint64_t next = tiles[i + 1][row];
                    dest[i] = (last >> shift) | (next << (64 - shift));
                    last = next;
                }
            } else {
                for (unsigned i = 0; i < width / 8; ++i)
                    dest[i] = tiles[i][row];
            }
        }
    }
}

static void wide_repair_background(const struct wide_map *map)
{
    if (!wide_repair_count)
        return;
    unsigned bank = (wide_control & 0x10) * 256;
    for (unsigned row = 0; row < REPAIR_ROWS; ++row) {
        int y = row * 8 - wide_fine_y;
        if (y >= LP_FRAME_H || (wide_dialogue && y >= MESSAGE_TOP))
            break;
        for (unsigned col = 0; col < REPAIR_COLS; ++col) {
            if (!wide_repairs[row][col])
                continue;
            int x = col * 8 - wide_fine_x;
            if (x >= LP_FRAME_W || x + 8 <= EDGE || x >= LP_FRAME_W - EDGE)
                continue;
            unsigned code = wide_tile(map, &wide_camera, wide_camera.x + x, wide_camera.y + y);
            const uint64_t *tile = wide_build_tile(code, bank);
            for (int line = 0; line < 8; ++line) {
                int sy = y + line;
                if ((unsigned)sy >= LP_FRAME_H || sy < EDGE || sy >= LP_FRAME_H - EDGE ||
                    (wide_dialogue && sy >= MESSAGE_TOP))
                    continue;
                const uint8_t *source = (const uint8_t *)(tile + line);
                uint8_t *dest = wide_pixels + sy * LP_WIDE_PITCH + LP_WIDE_CENTER;
                for (int pixel = 0; pixel < 8; ++pixel)
                    if (x + pixel >= EDGE && x + pixel < LP_FRAME_W - EDGE)
                        dest[x + pixel] = source[pixel];
            }
        }
    }
}

static int wide_repaired(int x, int y)
{
    return wide_repair_count && x >= EDGE && x < LP_FRAME_W - EDGE &&
           (!wide_dialogue || y < MESSAGE_TOP) &&
           wide_repairs[(y + wide_fine_y) >> 3][(x + wide_fine_x) >> 3];
}

static uint8_t wide_color(unsigned color)
{
    unsigned emphasis = wide_mask >> 5;
    color &= wide_mask & 1 ? 0x30 : 0x3f;
    return color | (emphasis == 7 ? 0xc0 : emphasis ? 0x40 : 0x80);
}

static void wide_finish_bands(void)
{
    enum { LEFT = LP_WIDE_CENTER - WIDE_EXTRA, RIGHT = LP_WIDE_CENTER + LP_FRAME_W - EDGE };
    unsigned mask = wide_mask & 1 ? 0x30 : 0x3f;
    uint64_t keep = mask * UINT64_C(0x0101010101010101);
    uint64_t emphasis = wide_color(0) * UINT64_C(0x0101010101010101);
    for (unsigned y = 0; y < wide_height; ++y) {
        uint8_t *row = wide_pixels + y * LP_WIDE_PITCH;
        if (wide_repair_rows && (y < EDGE || y >= LP_FRAME_H - EDGE)) {
            lp_finish_indexed_span(row + LEFT, LP_FRAME_W + WIDE_EXTRA * 2, keep, emphasis);
            continue;
        }
        lp_finish_indexed_span(row + LEFT, BAND, keep, emphasis);
        lp_finish_indexed_span(row + RIGHT, BAND, keep, emphasis);
        if (wide_repair_count && (!wide_dialogue || y < MESSAGE_TOP)) {
            for (unsigned col = 0; col < REPAIR_COLS; ++col) {
                if (!wide_repairs[(y + wide_fine_y) >> 3][col])
                    continue;
                int start = (int)col * 8 - wide_fine_x;
                int end = start + 8;
                if (start < EDGE) start = EDGE;
                if (end > LP_FRAME_W - EDGE) end = LP_FRAME_W - EDGE;
                for (int x = start; x < end; ++x)
                    row[LP_WIDE_CENTER + x] = wide_color(row[LP_WIDE_CENTER + x]);
            }
        }
    }
}

static inline void wide_copy_center(uint8_t *dest, const uint8_t *source)
{
#ifdef RENDER_GSKIT_PS2
    /* The PPU and texture rows are aligned for the EE's quadword transfers. */
    for (unsigned x = 0; x < LP_FRAME_W; x += 64) {
        __asm__ volatile(
            "lq $8,  0(%1)\n\t" "lq $9, 16(%1)\n\t" "lq $10, 32(%1)\n\t" "lq $11, 48(%1)\n\t"
            "sq $8,  0(%0)\n\t" "sq $9, 16(%0)\n\t" "sq $10, 32(%0)\n\t" "sq $11, 48(%0)\n\t"
            :: "r"(dest + x), "r"(source + x) : "$8", "$9", "$10", "$11", "memory");
    }
#else
    memcpy(dest, source, LP_FRAME_W);
#endif
}

static void wide_sprite(const struct wide_map *map, int x, int y, unsigned tile, unsigned attr, int extra)
{
    int height = wide_control & 0x20 ? 16 : 8;
    if (x >= LP_FRAME_W + WIDE_EXTRA || x <= -WIDE_EXTRA - 8 || y >= (int)wide_height || y <= -height ||
        (!extra && !wide_repair_count && x >= EDGE && x + 8 <= LP_FRAME_W - EDGE &&
         (!wide_repair_rows || (y >= EDGE && y + height <= LP_FRAME_H - EDGE))))
        return;
    unsigned address = height == 16 ? (tile & 1) * 4096 + (tile & 254) * 16 :
                                     ((wide_control & 8) * 512 + tile * 16);
    const uint8_t *pattern = wide_pattern(address);
    for (int row = 0; row < height; ++row) {
        int sy = y + row;
        if ((unsigned)sy >= wide_height)
            continue;
        unsigned line = attr & 0x80 ? height - 1 - row : row;
        const uint8_t *p = pattern + line + (line & 8);
        uint8_t *dest = wide_pixels + sy * LP_WIDE_PITCH + LP_WIDE_CENTER;
        for (int col = 0; col < 8; ++col) {
            int sx = x + col;
            if (sx < -WIDE_EXTRA || sx >= LP_FRAME_W + WIDE_EXTRA)
                continue;
            int band = sx < EDGE || sx >= LP_FRAME_W - EDGE ||
                       (wide_repair_rows && (sy < EDGE || sy >= LP_FRAME_H - EDGE)) || wide_repaired(sx, sy);
            if (!band && !extra)
                continue;
            unsigned bit = attr & 0x40 ? col : 7 - col;
            unsigned pen = ((p[0] >> bit) & 1) | (((p[8] >> bit) & 1) << 1);
            if (!pen)
                continue;
            unsigned color = wide_colors[16 + (attr & 3) * 4 + pen];
            if (band) {
                uint8_t old = dest[sx];
                if (!(old & SPRITE_CLAIMED)) {
                    if (!(attr & 0x20) || (old & BG_CLEAR))
                        old = color;
                    dest[sx] = old | SPRITE_CLAIMED;
                }
            } else {
                int bx = wide_camera.x + sx, by = wide_camera.y + sy;
                unsigned code = wide_tile(map, &wide_camera, bx, by);
                unsigned bg = wide_bg_pen(code, bx, by);
                if ((!(attr & 0x20) || !bg) && dest[sx] == wide_color(wide_bg_color(bg, code >> 8)))
                    dest[sx] = wide_color(color);
            }
        }
    }
}

static void wide_actors(const struct wide_map *map)
{
    if (wide_oam_map != wide_camera.map)
        return;
    for (unsigned i = 1; i < 16; ++i) {
        struct wide_actor a = wide_oam[i];
        if (!a.frame || a.x < -WIDE_EXTRA - 24 || a.x >= LP_FRAME_W + WIDE_EXTRA + 24 ||
            (a.visible && a.x >= 16 && a.x < LP_FRAME_W - 16 &&
             (!wide_repair_rows || (a.y >= EDGE + 24 && a.y < LP_FRAME_H - EDGE))))
            continue;
        int alternate = a.family == 2;
        if (a.frame >= (alternate ? 245 : 228))
            continue;
        const uint8_t *table = wide_prg(0x19, alternate ? 0x9dc1 : 0x8162);
        const uint8_t *p = wide_prg(0x19, wide_word(table + a.frame * 2));
        unsigned count = *p++, run = 0, attr = 0;
        for (unsigned tile_index = 0; tile_index < count; ++tile_index) {
            int y = a.y + (int8_t)*p++ + 1;
            unsigned tile = (uint8_t)(*p++ + (alternate ? 0x4f : 0));
            if (run) {
                --run;
            } else {
                attr = *p++;
                run = (attr >> 2) & 7;
            }
            int x = a.x + (int8_t)*p++;
            if (!a.visible || x < 0 || x >= 249 ||
                (wide_repair_rows && (y < EDGE || y + (wide_control & 0x20 ? 16 : 8) > LP_FRAME_H - EDGE)))
                wide_sprite(map, x, y, tile, (attr | a.attr) & 0xe3, 1);
        }
    }
}

static int wide_message_ready(void)
{
    if (!wide_dialogue)
        return 0;
    /* Both editions use the same field message frame. Verify its horizontal
     * rails so opening/closing windows retain their original animation. */
    unsigned white = XBuf[142 * LP_FRAME_W + 32];
    if ((white & 0x2f) != 0x20 || XBuf[142 * LP_FRAME_W + 224] != white ||
        XBuf[225 * LP_FRAME_W + 32] != white || XBuf[225 * LP_FRAME_W + 224] != white)
        return 0;
    return 1;
}

static void wide_message(void)
{
    unsigned white = XBuf[142 * LP_FRAME_W + 32];
    uint8_t black = XBuf[224 * LP_FRAME_W + 16];
    for (unsigned y = MESSAGE_TOP; y < LP_FRAME_H; ++y) {
        const uint8_t *source = XBuf + y * LP_FRAME_W;
        uint8_t *dest = wide_pixels + y * LP_WIDE_PITCH;
        uint8_t fill = y == 142 || y == 143 || y == 225 || y == 226 ? white : black;
        memset(dest, black, LP_FRAME_W + WIDE_EXTRA * 2);
        memset(dest + 16, fill, LP_FRAME_W + WIDE_EXTRA * 2 - 32);
        memcpy(dest + 8, source + 8, 8);
        memcpy(dest + LP_FRAME_W + WIDE_EXTRA * 2 - 16, source + 240, 8);
        if (y >= 144 && y < 225)
            memcpy(dest + 16, source + 16, 224);
    }
}

const uint8_t *lp_wide_frame(void)
{
    if (!lp_wide_enabled || !lp_wide_visible || PPU[1] != wide_mask)
        return NULL;
    struct wide_map map = wide_map_data(&wide_camera);
    int message = wide_message_ready();
    wide_height = message ? MESSAGE_TOP : LP_FRAME_H;
    for (unsigned y = 0; y < wide_height; ++y)
        wide_copy_center(wide_pixels + y * LP_WIDE_PITCH + LP_WIDE_CENTER, XBuf + y * LP_FRAME_W);
    wide_background(&map);
    wide_repair_background(&map);
    /* Field slot 0 ($5B at X=$F8) covers the wrapped NES nametable edge. */
    unsigned first = 0;
    const unsigned curtain_tiles = 27;
    while (first < curtain_tiles) {
        const uint8_t *oam = wide_hardware_oam + first * 4;
        if (oam[0] != 8 + first * 8 || oam[1] != 0x40 || oam[2] != 0 || oam[3] != 0xf8)
            break;
        ++first;
    }
    for (unsigned i = first == curtain_tiles ? first * 4 : 0; i < sizeof(wide_hardware_oam); i += 4) {
        const uint8_t *oam = wide_hardware_oam + i;
        /* The NES adds descriptor offsets modulo 256. At the vertical edge
         * an actor below the frame can leave its feet at the top. Reproject
         * those tiles with the object's full Y coordinate instead. */
        if (wide_repair_rows && (oam[0] + 1 < EDGE ||
            oam[0] + 1 + (wide_control & 0x20 ? 16 : 8) > LP_FRAME_H - EDGE))
            continue;
        wide_sprite(&map, oam[3], oam[0] + 1, oam[1], oam[2], 0);
    }
    wide_actors(&map);
    wide_finish_bands();
    if (message)
        wide_message();
    memcpy(wide_pixels + LP_FRAME_H * LP_WIDE_PITCH,
           wide_pixels + (LP_FRAME_H - 1) * LP_WIDE_PITCH, LP_WIDE_PITCH);
    return wide_pixels;
}

#ifndef PS2
const uint8_t *lp_wide_palette(void)
{
    return wide_colors;
}
#endif
