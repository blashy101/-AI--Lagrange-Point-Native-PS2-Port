/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "lp_core.h"

/* Minimal device state for testing the read-only presentation layer. */
static uint8_t rom[0x80000], cart[8192], RAM[2048], PPU[4], palette[32];
static uint8_t patterns[8192], frame[256 * 240], nametable[1024];
static uint8_t *PRGptr[32], *VPage[8], *vnapage[4], *XBuf = frame;
static uint8_t PALRAM[32], SPRAM[256], XOffset;
static unsigned RefreshAddr;
static uint32_t ppulut1[256], ppulut2[256];
#define ScreenON (PPU[1] & 8)
#define SpriteON (PPU[1] & 16)
#include "wide_field.inc"

int main(void)
{
    lp_set_wide(1);
    lp_wide_latch_oam();
    lp_wide_begin_picture();
    assert(lp_wide_frame() == NULL);
    PRGptr[0] = rom;
    PRGptr[0x10] = cart;
    for (unsigned i = 0; i < 8; ++i) {
        VPage[i] = patterns;
        wide_chr[i] = patterns + i * 1024;
    }
    for (unsigned i = 0; i < 4; ++i)
        vnapage[i] = nametable;
    for (unsigned value = 0; value < 256; ++value) {
        for (unsigned pixel = 0; pixel < 8; ++pixel)
            ppulut1[value] |= ((value >> (7 - pixel)) & 1) << (pixel * 4);
        ppulut2[value] = ppulut1[value] << 1;
    }
    memset(patterns + 16, 0xff, 8);
    palette[0] = 0x0f;
    palette[1] = 0x12;
    palette[5] = 0x2a;
    palette[17] = 0x36;
    memcpy(wide_colors, palette, sizeof(palette));
    wide_control = 0;
    wide_mask = 0x18;
    wide_repair_rows = 1;

    uint8_t cells[48 * 2], tiles[8], palettes[2] = {0, 1};
    memset(cells, 0, sizeof(cells));
    memset(tiles, 1, sizeof(tiles));
    struct wide_map map = {cells, tiles, palettes, 48, 2, 2, 0};
    wide_camera = (struct wide_camera){.x = 0, .y = -32, .map = 5, .border = 1};
    assert(wide_tile(&map, &wide_camera, 0, 0) == 1);
    assert(wide_tile(&map, &wide_camera, -1, 0) == 0x101);
    assert(wide_tile(&map, &wide_camera, 48 * 16, 0) == 0x101);
    assert(wide_tile(&map, &wide_camera, 0, -1) == 0x101);
    assert(wide_tile(&map, &wide_camera, 0, 2 * 16) == 0x101);

    memset(wide_pixels, 0x99, sizeof(wide_pixels));
    wide_background(&map);
    wide_finish_bands();
    for (unsigned y = 0; y < EDGE; ++y)
        for (unsigned x = 0; x < LP_FRAME_W + WIDE_EXTRA * 2; ++x)
            assert(wide_pixels[y * LP_WIDE_PITCH + x] == 0xaa);
    assert(wide_pixels[120 * LP_WIDE_PITCH + LP_WIDE_CENTER + 100] == 0x99);

    /* Reproduce a tile that the NES wraps from Y=263 to Y=7. */
    unsigned table = 0x19 * 8192 + 0x162;
    rom[table + 2] = 0x00;
    rom[table + 3] = 0x83;
    unsigned stream = 0x19 * 8192 + 0x300;
    const uint8_t sprite[] = {1, 0xff, 1, 0, 0};
    memcpy(rom + stream, sprite, sizeof(sprite));
    wide_oam_map = 5;
    wide_oam[2] = (struct wide_actor){.x = 100, .y = 263, .frame = 1, .visible = 1};
    wide_background(&map);
    wide_actors(&map);
    wide_finish_bands();
    for (unsigned y = 0; y < EDGE; ++y)
        assert(wide_pixels[y * LP_WIDE_PITCH + LP_WIDE_CENTER + 100] == 0xaa);

    /* A real sprite crossing the top is clipped at its signed Y coordinate. */
    wide_oam[2].y = -1;
    wide_background(&map);
    wide_actors(&map);
    wide_finish_bands();
    assert(wide_pixels[LP_WIDE_CENTER + 100] == 0xb6);

    /* An unrelated overlay retains its original center. */
    wide_repair_rows = 0;
    memset(wide_pixels, 0x99, sizeof(wide_pixels));
    wide_background(&map);
    wide_finish_bands();
    assert(wide_pixels[LP_WIDE_CENTER + 100] == 0x99);
    assert(wide_pixels[232 * LP_WIDE_PITCH + LP_WIDE_CENTER + 100] == 0x99);

    /* A CHR RAM write invalidates all palette variants, even with stable banks. */
    wide_prepare_tiles(0);
    wide_build_tile(1, 0);
    wide_build_tile(0x101, 0);
    assert(wide_bg_cached[1] && wide_bg_cached[0x101]);
    wide_prepare_tiles(0);
    assert(wide_bg_cached[1] && wide_bg_cached[0x101]);
    patterns[16] = 0;
    wide_prepare_tiles(0);
    assert(!wide_bg_cached[1] && !wide_bg_cached[0x101]);
    assert((uint8_t)wide_build_tile(1, 0)[0] == 0x4f);
    patterns[16] = 0xff;

    /* Restore a foreground sprite after repairing an interior background tile. */
    memset(wide_repairs, 0, sizeof(wide_repairs));
    wide_fine_x = wide_fine_y = 0;
    wide_repairs[5][8] = 1;
    wide_repair_count = 1;
    wide_dialogue = 0;
    wide_repair_rows = 1;
    memset(wide_pixels, 0x99, sizeof(wide_pixels));
    wide_background(&map);
    wide_repair_background(&map);
    wide_sprite(&map, 64, 40, 1, 0, 0);
    wide_finish_bands();
    assert(wide_pixels[40 * LP_WIDE_PITCH + LP_WIDE_CENTER + 64] == 0xb6);
    assert(wide_pixels[48 * LP_WIDE_PITCH + LP_WIDE_CENTER + 64] == 0x99);

    /* Nametable writes and fine scroll refresh the correct repair cells. */
    memset(nametable, 1, 960);
    memset(nametable + 960, 0, 64);
    palettes[1] = 0;
    wide_scroll = (struct wide_camera){.map = 5, .border = 1, .valid = 1};
    RefreshAddr = 0;
    wide_dialogue = 0;
    wide_find_repairs(&map, 0, 0);
    assert(wide_repair_count == 0);
    nametable[5 * 32 + 8] = 0;
    wide_find_repairs(&map, 0, 0);
    assert(wide_repair_count == 1 && wide_repairs[5][8]);
    wide_scroll.x = wide_scroll.scroll_x = 6;
    wide_find_repairs(&map, 6, 0);
    assert(wide_repair_count == 1 && wide_repaired(58, 40));
    nametable[5 * 32 + 8] = 1;
    wide_find_repairs(&map, 6, 0);
    assert(wide_repair_count == 0);
    wide_dialogue = 1;
    nametable[18 * 32 + 8] = 0;
    wide_find_repairs(&map, 6, 0);
    assert(wide_repair_count == 0);

    /* Full-width rails preserve text pixel size and reject incomplete frames. */
    memset(frame, 0x8f, sizeof(frame));
    for (unsigned y = 142; y <= 226; ++y) {
        if (y == 142 || y == 143 || y == 225 || y == 226)
            memset(frame + y * 256 + 8, 0xb0, 240);
    }
    frame[155 * 256 + 16] = 0xb0;
    wide_dialogue = 1;
    assert(wide_message_ready());
    wide_message();
    for (unsigned x = 16; x < 336; ++x) {
        assert(wide_pixels[142 * LP_WIDE_PITCH + x] == 0xb0);
        assert(wide_pixels[225 * LP_WIDE_PITCH + x] == 0xb0);
        assert(wide_pixels[200 * LP_WIDE_PITCH + x] == 0x8f);
    }
    assert(wide_pixels[155 * LP_WIDE_PITCH + 16] == 0xb0);
    assert(wide_pixels[155 * LP_WIDE_PITCH + 17] == 0x8f);
    assert(frame[155 * 256 + 16] == 0xb0);
    frame[142 * 256 + 224] = 0x8f;
    assert(!wide_message_ready());
}
