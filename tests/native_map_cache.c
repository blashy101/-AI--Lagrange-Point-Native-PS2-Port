/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PS2 1
#include "lp_core.h"
typedef uint8_t (*read_handler)(uint32_t);
static uint8_t CartBR(uint32_t address){(void)address;return 0;}
static uint8_t intercepted(uint32_t address){(void)address;return 0;}
static read_handler ARead[65536];
static uint8_t rom[0x80000],*PRGptr[32],*Page[32],encryptOpcodes;
static uint32_t PRGsize[32];
static uint16_t english[0x80000],japanese[0x80000];
static const uint16_t *lp_instruction_map[]={english,japanese};
#include "native_map_cache.inc"
int main(void)
{
    PRGptr[0]=rom;PRGsize[0]=sizeof(rom);
    Page[16]=(uint8_t *)((uintptr_t)rom+0x1000-0x8000);
    english[0x1123]=17;english[0x1124]=18;japanese[0x1123]=23;
    for(unsigned i=0;i<65536;++i)ARead[i]=CartBR;
    lp_select_release(0);assert(!lp_mapped_instruction(0x8123));
    lp_note_read_handlers(0x8000,0xffff);
    assert(lp_mapped_instruction(0x8123)==17&&lp_mapped_instruction(0x8124)==18);
    assert(!lp_mapped_instruction(0x7fff)&&!lp_mapped_instruction(0x8800));
    ARead[0x8123]=intercepted;lp_note_read_handlers(0x8123,0x8123);
    assert(!lp_mapped_instruction(0x8123)&&!lp_mapped_instruction(0x8124));
    ARead[0x8123]=CartBR;lp_note_read_handlers(0x8123,0x8123);
    assert(lp_mapped_instruction(0x8124)==18);
    lp_select_release(1);assert(lp_mapped_instruction(0x8123)==23);
    encryptOpcodes=1;assert(!lp_mapped_instruction(0x8123));encryptOpcodes=0;
    Page[16]=(uint8_t *)((uintptr_t)rom+0x2000-0x8000);japanese[0x2123]=42;
    lp_refresh_code_page(16);assert(lp_mapped_instruction(0x8123)==42);
    PRGsize[0]=0x2400;lp_refresh_code_page(16);assert(!lp_mapped_instruction(0x8123));
    PRGsize[0]=sizeof(rom);lp_refresh_code_page(16);assert(lp_mapped_instruction(0x8123)==42);
    lp_select_release(-1);assert(!lp_mapped_instruction(0x8123));
    lp_refresh_code_page(32);lp_note_read_handlers(0x10000,0x10001);lp_note_read_handlers(0xffff,0x8000);
    puts("Native map cache: bilingual mappings, bank changes, handler interception/restoration, bounds, encryption and reference fallback passed.");
    return 0;
}
