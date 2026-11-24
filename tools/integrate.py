"""Build a patched work copy of the pinned FCEUmm device runtime."""
import argparse
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parent.parent


def patch(path, replacements):
    text = path.read_text(encoding='utf-8')
    for old, new in replacements:
        if text.count(old) != 1:
            raise ValueError(f'Pinned file {path.name} differs at {old!r}.')
        text = text.replace(old, new, 1)
    path.write_text(text, encoding='utf-8')


def specialize(src, generated):
    ops = (src / 'ops.h').read_text(encoding='utf-8')
    cases = re.split(r'^case 0x([0-9A-F]{2}):', ops.rsplit('#endif', 1)[0], flags=re.M)
    bodies = {}
    body = ''
    for opcode, part in reversed(list(zip(cases[1::2], cases[2::2]))):
        code = re.sub(r'/\*.*?\*/', '', part, flags=re.S).strip()
        if code:
            body = code
        bodies[int(opcode, 16)] = body
    if len(bodies) != 256 or not body:
        raise ValueError('Expected 256 opcode cases in the pinned runtime.')
    encodings = [bytes.fromhex(raw) for raw in json.loads((generated / 'encodings.json').read_text())]
    hot = set(json.loads((ROOT / 'config/dispatch_layout.json').read_text())['hot_encodings'])
    cpu = (src / 'x6502.c').read_text(encoding='utf-8')
    table = cpu.split('static const uint8_t CycTable[256]', 1)[1].split('};', 1)[0]
    cycles = [int(value) for value in re.findall(r'\d+', re.sub(r'/\*.*?\*/', '', table))]
    if len(cycles) != 256:
        raise ValueError('Expected 256 entries in the pinned CPU timing table.')
    mix_offset = 16 + 0x7E9FC
    mix_bytes = bytes.fromhex('E000 D006 AD0F60 4C09EA BDFF5F 65F3 6522 7D0060 9D0060 E8 E010 D0E4')
    releases = [(generated / f'{edition}.nes').read_bytes() for edition in ('en', 'ja')]
    has_mix = all(rom[mix_offset:mix_offset + len(mix_bytes)] == mix_bytes for rom in releases)
    # Observe the pre-cull pose, the matching OAM shadow and the camera written to the PPU.
    wide_sites = ((0x24, 0x9037, '38 bd807a e539', 'lp_wide_note_npc(_X)'),
                  (0x19, 0x8001, 'a5fb d004 a9c4', 'lp_wide_note_oam_shadow()'),
                  (0x3f, 0xeadb, '8d0520 a5fc 8d0520', 'lp_wide_note_scroll()'))
    hooks = {}
    for bank, pc, signature, call in wide_sites:
        offset = 16 + bank * 8192 + (pc & 8191)
        expected = bytes.fromhex(signature)
        if any(rom[offset:offset + len(expected)] != expected for rom in releases):
            raise ValueError(f'Wide rendering hook differs at {bank:02x}:{pc:04x}.')
        opcode = bytes.fromhex(signature.split()[0])
        hooks[opcode] = f'    if (pc == 0x{pc + 1:04x} && lp_wide_enabled && lp_wide_site(0x{pc:04x}, 0x{bank:02x}))\n        {call};\n'
    with (src / 'native_handlers.inc').open('w', encoding='ascii') as stream:
        stream.write(ops.split('#ifndef', 1)[0])
        stream.write(f'#define LP_HAS_SRAM_MIX {int(has_mix)}\n')
        for index, raw in enumerate(encodings, 1):
            operand = int.from_bytes(raw[1:], 'little')
            placement = ' __attribute__((hot))' if raw.hex() in hot else ''
            stream.write(f'''static uint16_t{placement} lp_run_{index}(uint16_t pc)
{{
    const uint16_t operand_pc __attribute__((unused)) = pc;
    const unsigned operand __attribute__((unused)) = {operand};
    const unsigned count __attribute__((unused)) = {len(raw) - 1};
{hooks.get(raw, '')}    do {{
        {bodies[raw[0]]}
    }} while (0);
    return pc;
}}
''')
        stream.write('static const struct lp_compiled_instruction lp_handlers[] = {\n    {NULL, 0, 0},\n')
        for index, raw in enumerate(encodings, 1):
            stream.write(f'    {{lp_run_{index}, 0x{raw[0]:02X}, {cycles[raw[0]]}}},\n')
        stream.write('};\n')


def optimize_ppu(src):
    tiles = src / 'pputile.h'
    text = tiles.read_text(encoding='utf-8')
    start = text.index('#ifdef MSB_FIRST')
    end = text.index('#if defined(__GNUC__)', start)
    tile_pack = text[start:end].replace('uint64_t packed =', 'return')
    patch(tiles, [(text[start:end], '''#ifdef PPUT_CACHE_PIXELS
		if (tile_pens != pixdata) {
			tile_pens = pixdata;
			tile_pixels = FCEU_PackBgPixels(pixdata);
		}
		uint64_t packed = tile_pixels;
#else
		uint64_t packed = FCEU_PackBgPixels(pixdata);
#endif
'''),
        ('#if defined(__GNUC__) || defined(__clang__)\n', '''#if defined(RENDER_GSKIT_PS2)
		/* XBuf is aligned and P advances in eight-byte tiles. */
		typedef uint64_t fceu_u64_tile __attribute__((may_alias));
		*(fceu_u64_tile *)P = packed;
#elif defined(__GNUC__) || defined(__clang__)
'''),
    ])
    patch(src / 'video.c', [
        ('#include <stdlib.h>', '#include <stdlib.h>\n#ifdef RENDER_GSKIT_PS2\n#include <malloc.h>\n#endif'),
        ('''   if (!XBuf)
      XBuf = (uint8_t*)(FCEU_malloc(256 * (256 + extrascanlines + 8)));''', '''   if (!XBuf) {
#ifdef RENDER_GSKIT_PS2
      XBuf = memalign(16, 256 * (256 + extrascanlines + 8));
#else
      XBuf = FCEU_malloc(256 * (256 + extrascanlines + 8));
#endif
   }'''),
    ])
    patch(src / 'ppu.c', [
        ('static DECLFW(B2000) {',
         'static inline uint64_t FCEU_PackBgPixels(uint32_t pixdata)\n{\n' + tile_pack + '}\n\nstatic DECLFW(B2000) {'),
        ('uint8_t PPUSPL;\nuint8_t NTARAM', '''uint8_t PPUSPL;
/* OAM Y is at most 255 and sprites cover up to 16 lines. */
static uint8_t sprite_first[34], sprite_end[34], sprite_range_height;
uint8_t NTARAM'''),
        ('''\tif (PPUSPL >= 8) {
		if (PPU[3] >= 8)
			SPRAM[PPU[3]] = V;
	} else {
		SPRAM[PPUSPL] = V;
	}''', '''\tunsigned index = PPUSPL < 8 ? PPUSPL : PPU[3];
	if (PPUSPL < 8 || index >= 8) {
		if (!(index & 3) && SPRAM[index] != V)
			sprite_range_height = 0;
		SPRAM[index] = V;
	}'''),
        ('static uint8_t numsprites, SpriteBlurp;', '''static uint8_t numsprites, SpriteBlurp;

/* Bound the OAM search in groups of eight scanlines. The fetch loop
 * still checks each candidate's Y in OAM order. */
static void FCEU_BuildSpriteRanges(unsigned height)
{
	memset(sprite_first, 64, sizeof(sprite_first));
	memset(sprite_end, 0, sizeof(sprite_end));
	for (unsigned i = 0; i < 64; ++i) {
		unsigned y = SPRAM[i * 4];
		unsigned last = (y + height - 1) >> 3;
		for (unsigned row = y >> 3; row <= last; ++row) {
			if (sprite_first[row] == 64)
				sprite_first[row] = i;
			sprite_end[row] = i + 1;
		}
	}
	sprite_range_height = height;
}'''),
        ('\tif (!PPU_hook)\n\t\tfor (n = 63; n >= 0; n--, spr++) {', '''\tif (!PPU_hook) {
		unsigned row = (unsigned)scanline >> 3;
		unsigned first = 0, end = 64;
		if (row < sizeof(sprite_first)) {
			if (sprite_range_height != H)
				FCEU_BuildSpriteRanges(H);
			first = sprite_first[row];
			end = sprite_end[row];
		}
		spr += first;
		for (n = 63 - (int)first; n >= 64 - (int)end; n--, spr++) {'''),
        ('\n\telse\n\t\tfor (n = 63; n >= 0; n--, spr++) {',
         '\n\t} else\n\t\tfor (n = 63; n >= 0; n--, spr++) {'),
        ('int FCEUPPU_Loop(int skip) {\n',
         'int FCEUPPU_Loop(int skip) {\n\t/* Cover OAM restored outside the register handlers. */\n'
         '\tsprite_range_height = 0;\n'),
        ('''\tint x, colour_emphasis;
	uint8_t *target = NULL;
	uint8_t *dtarget = NULL;''', '\tint x;\n\tuint8_t *target;'),
        ('\tdtarget = XDBuf + ((scanline < 240 ? scanline : 240) << 8);\n', ''),
        ('''\t/* write the actual colour emphasis */
	colour_emphasis = ((PPU[1] >> 5) << 24) | ((PPU[1] >> 5) << 16) | ((PPU[1] >> 5) << 8) | ((PPU[1] >> 5) << 0);
	for (x = 63; x >= 0; x--)
		*(uint32_t*)&dtarget[x << 2] = colour_emphasis;''', '''#ifndef RENDER_GSKIT_PS2
	/* The software path reads this buffer. GS uses the indexed picture. */
	memset(XDBuf + ((scanline < 240 ? scanline : 240) << 8), PPU[1] >> 5, 256);
#endif'''),
        ('''\t\t\tfor (X1 = firsttile; X1 < lasttile; X1++) {
				#include "pputile.h"
			}
		}
	}

#undef vofs''', '''\t\t\t/* Reuse the packed tile for repeated background pens. */
			uint32_t tile_pens = 0;
			uint64_t tile_pixels = FCEU_PackBgPixels(tile_pens);
			#define PPUT_CACHE_PIXELS
			for (X1 = firsttile; X1 < lasttile; X1++) {
				#include "pputile.h"
			}
			#undef PPUT_CACHE_PIXELS
		}
	}

#undef vofs'''),
    ])


def optimize_sound(src):
    sound = src / 'sound.c'
    patch(sound, [(f'static {decl}', decl) for decl in (
        'int32_t fhcnt = 0;', 'int32_t DMCacc = 1;', 'int32_t DMCSize = 0;', 'char DMCHaveDMA = 0;',
    )] + [
        ('void FASTAPASS(1) FCEU_SoundCPUHook(int cycles) {', '''/* A silent DMC can cross bit clocks without reading memory or mixing audio. */
int FCEU_CanBatchSoundCPU(int cycles)
{
	return fhcnt > cycles * 48 && (!DMCSize || DMCHaveDMA) &&
	       (DMCacc > cycles || (!DMCHaveDMA && !DMCHaveSample));
}

void FASTAPASS(1) FCEU_SoundCPUHook(int cycles) {'''),
        ('''	} else {
		for (V = start; V < end; V++)
			Wave[V >> 4] += totalout;
	}
}''', '''	} else if (totalout) {
		/* The DAC output is constant through this interval. */
		while (start < end) {
			int32_t count = 16 - (start & 15);
			if (end - start < count)
				count = end - start;
			Wave[start >> 4] += count * totalout;
			start += count;
		}
	}
}'''),
    ])
    patch(src / 'sound.h', [('void FASTAPASS(1) FCEU_SoundCPUHook(int);', '''void FASTAPASS(1) FCEU_SoundCPUHook(int);

extern int32_t fhcnt, DMCacc, DMCSize;
extern char DMCHaveDMA;
int FCEU_CanBatchSoundCPU(int cycles);

/* Use the full hook whenever a timer expires or DMC needs the CPU bus. */
static inline int FCEU_CanAdvanceSoundCPU(int cycles)
{
	return fhcnt > cycles * 48 && DMCacc > cycles && (!DMCSize || DMCHaveDMA);
}

static inline void FCEU_AdvanceSoundCPU(int cycles)
{
	if (!FCEU_CanAdvanceSoundCPU(cycles)) {
		FCEU_SoundCPUHook(cycles);
		return;
	}
	fhcnt -= cycles * 48;
	DMCacc -= cycles;
}''')])
    patch(src / 'boards/vrc7.c', [
        ('#include "emu2413.h"', '#include "emu2413.h"\n#include "lp_core.h"'),
        ('static DECLFW(VRC7Write) {', 'static void VRC7IRQHook(int cycles);\n\nstatic DECLFW(VRC7Write) {'),
        ('\t\t\tIRQa = V & 2;\n', '\t\t\tIRQa = V & 2;\n\t\t\tMapIRQHook = IRQa ? VRC7IRQHook : NULL;\n'),
        ('\t\t\tIRQa = IRQd;\n', '\t\t\tIRQa = IRQd;\n\t\t\tMapIRQHook = IRQa ? VRC7IRQHook : NULL;\n'),
        ('static void VRC7Power(void) {\n', 'static void VRC7Power(void) {\n\tMapIRQHook = IRQa ? VRC7IRQHook : NULL;\n'),
        ('static void StateRestore(int version) {\n', 'static void StateRestore(int version) {\n\tMapIRQHook = IRQa ? VRC7IRQHook : NULL;\n'),
        ('\tMapIRQHook = VRC7IRQHook;\n', ''),
        ('static void StateRestore(int version) {', '''/* Advance only for a block that fits before the mapper asserts an IRQ. */
int lp_try_advance_vrc7(int cycles)
{
	if (!MapIRQHook)
		return 1;
	if (MapIRQHook != VRC7IRQHook)
		return 0;
	int next = CycleCount + cycles * 3;
	if (next >= 341) {
		int ticks = next / 341;
		if (IRQCount + ticks >= 0x100)
			return 0;
		IRQCount += ticks;
		next -= ticks * 341;
	}
	CycleCount = next;
	return 1;
}

static void StateRestore(int version) {'''),
    ])

    patch(src / 'boards/emu2413.c', [('void OPLL_fillbuf(OPLL* opll, int32_t *buf, int32_t len, int shift) {\n\twhile (len > 0) {\n\t\t*buf += (calc(opll) + 32768) << shift;\n\t\tbuf++;\n\t\tlen--;\n\t}\n}\n', '#include \"vrc7_fill.inc\"\n')])


def prepare(target, generated):
    shutil.copytree(ROOT / 'third_party/fceumm', target, dirs_exist_ok=True)
    src = target / 'src'
    for name in ('native_cpu.inc', 'cpu_fallback.inc', 'native_map_cache.inc', 'vrc7_fill.inc', 'wide_field.inc', 'lp_core.h', 'ppu_pixels.h'):
        shutil.copyfile(ROOT / 'src' / name, src / name)
    shutil.copyfile(generated / 'native_map.inc', src / 'native_map.inc')
    specialize(src, generated)
    notice = '/* Lagrange Point PS2 build change, 2026-11-15. */'
    cpu = src / 'x6502.c'
    cpu_text = cpu.read_text(encoding='utf-8')
    decoder_start = cpu_text.index('\t\tif (encryptOpcodes == 10)')
    decoder_end = cpu_text.index('\n\t\tswitch (b1)', decoder_start)
    decoder = cpu_text[decoder_start:decoder_end]
    patch(cpu, [
        ('#include "x6502.h"', '#include "x6502.h"\n#include <stdint.h>'),
        ('void X6502_Run(int32_t cycles)\n{',
         notice + '\n#include "native_cpu.inc"\n#include "cpu_fallback.inc"\n\nvoid __attribute__((hot)) X6502_Run(int32_t cycles)\n{'),
        ('\t\t_PI = _P;\n\t\tb1 = RdMem(_PC);', '''
#if LP_HAS_SRAM_MIX
        if (_PC == 0xe9fc || _PC == 0xea09) {
            uint16_t next = lp_run_sram_mix(_PC);
            if (next) {
                _PC = next;
                continue;
            }
        }
#endif
        _PI = _P;
        uint16_t native_id = lp_mapped_instruction(_PC);
        const struct lp_compiled_instruction *native = native_id ? &lp_handlers[native_id] : NULL;
        if (__builtin_expect(native != NULL, 1)) {
            b1 = _DB = native->opcode;
            LP_COUNT(compiled);
        } else {
            if (_PC >= 0x8000)
                LP_COUNT(mapped_fallback);
            else
                LP_COUNT(ram_fallback);
            b1 = RdMem(_PC);
        }'''),
        (decoder + '\n\t\tswitch (b1) {\n\t\t\t#include "ops.h"\n\t\t}', '''
        if (__builtin_expect(native != NULL, 1)) {
            _PC = native->run(_PC);
        } else {
            _PC = lp_interpret_instruction(_PC, b1);
        }'''),
        ('\t\tADDCYC(CycTable[b1]);', '\t\tADDCYC(native ? native->cycles : CycTable[b1]);'),
        ('\t\t\tFCEU_SoundCPUHook(temp);', '\t\t\tFCEU_AdvanceSoundCPU(temp);'),
    ])
    retro = src / 'drivers/libretro'
    patch(retro / 'libretro.c', [('      strlcpy(content_path, info->path,\n',
                                 f'      {notice}\n'
                                 '      content_data = info->data;\n'
                                 '      content_size = info->size;\n'
                                 '      strlcpy(content_path, info->path,\n')])
    patch(retro / 'link.T', [('retro_*;', 'retro_*;\n\tlp_select_release;\n\tlp_get_cpu_stats;'
                             '\n\tlp_set_wide;\n\tlp_wide_frame;\n\tlp_wide_palette;')])
    # RefreshLine can run several times per scanline. Reuse its table only
    # when all 16 palette bytes, including the sprite priority bits, match.
    patch(src / 'ppu.c', [
        (' * Rebuilt INSIDE RefreshLine just before the pputile.h includes, so',
         ' * Checked INSIDE RefreshLine just before the pputile.h includes, so'),
        ('static uint16_t fceu_bg_pair_lut[256];\n\nstatic void FCEU_BuildBgPairLUT(void)\n{\n\tint b;',
         '''static uint16_t fceu_bg_pair_lut[256];

static void FCEU_BuildBgPairLUT(void)
{
	/* Lagrange Point PS2 build change, 2026-11-15. */
	static uint8_t cached_palette[16];
	static int cache_valid;
	int b;
	if (cache_valid && memcmp(PALRAM, cached_palette, sizeof(cached_palette)) == 0)
		return;
	memcpy(cached_palette, PALRAM, sizeof(cached_palette));
	cache_valid = 1;'''),
    ])
    patch(target / 'Makefile.common', [('ifneq ($(STATIC_LINKING), 1)',
                                       '# Supply the support objects for the standalone frontend.\n'
                                       'ifneq ($(STATIC_LINKING):$(STANDALONE),1:)')])
    patch(target / 'Makefile.libretro', [
        ('all: $(TARGET)\n$(TARGET): $(OBJECTS)', '''all: $(TARGET)
ifeq ($(STANDALONE),1)
$(OBJECTS): .prepared
endif
ifneq ($(LP_CORE_FLAGS),)
lp_hot = $(filter src/x6502.o src/ppu.o src/sound.o src/fceu.o src/cart.o src/boards/vrc7.o src/boards/emu2413.o src/drivers/libretro/libretro.o,$(OBJECTS))
$(lp_hot): CFLAGS += $(LP_CORE_FLAGS)
endif
$(TARGET): $(OBJECTS)'''),
    ])
    patch(src / 'fceu.c', [
        ('#include "fceu.h"', '#include "fceu.h"\n#include "lp_core.h"'),
        ('\t\t\tARead[x] = func;\n}', '\t\t\tARead[x] = func;\n\tlp_note_read_handlers(start, end);\n}'),
        ('   RWWrap = 0;\n}', '   RWWrap = 0;\n   lp_note_read_handlers(0x8000, 0xffff);\n}'),
    ])
    patch(src / 'cart.c', [
        ('#include "cart.h"', '#include "cart.h"\n#include "lp_core.h"'),
        ('\t\t\tPage[AB + x] = p - A;',
         '\t\t\tPage[AB + x] = p - A;\n\t\t\tlp_refresh_code_page(AB + x);'),
        ('\t\t\tPage[AB + x] = 0;',
         '\t\t\tPage[AB + x] = 0;\n\t\t\tlp_refresh_code_page(AB + x);'),
        ('\t\tPRGsize[x] = CHRsize[x] = 0;',
         '\t\tPRGsize[x] = CHRsize[x] = 0;\n\t\tlp_refresh_code_page(x);'),
    ])
    optimize_ppu(src)
    optimize_sound(src)
    ppu = src / 'ppu.c'
    text = ppu.read_text(encoding='utf-8')
    start = text.index('\tif (ScreenON || SpriteON) {\t/* Yes, very el-cheapo. */')
    end = text.index('\n#ifndef RENDER_GSKIT_PS2', start)
    text = text[:start] + '\tlp_finish_ppu_line(target, PPU[1]);\n' + text[end:]
    marker = '#include        "ppu.h"'
    if text.count(marker) != 1:
        raise ValueError("Pinned PPU include differs.")
    text = text.replace(marker, marker + '\n#include "ppu_pixels.h"', 1)
    ppu.write_text(text, encoding='utf-8')
    patch(src / 'ppu.c', [
        ('static DECLFW(B4014) {', '#include "lp_core.h"\nstatic void lp_wide_latch_oam(void);\n\n'
                                 'static DECLFW(B4014) {\n\tif (lp_wide_enabled && V == 2)\n'
                                 '\t\tlp_wide_latch_oam();'),
        ('int FCEUPPU_Loop(int skip) {', '#include "wide_field.inc"\n\nint FCEUPPU_Loop(int skip) {\n'
                                     '\tlp_wide_visible = 0;'),
        ('\t\t\tResetRL(XBuf);', '\t\t\tif (lp_wide_enabled)\n\t\t\t\tlp_wide_begin_picture();\n'
                             '\t\t\tResetRL(XBuf);'),
    ])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', type=Path)
    parser.add_argument('generated', type=Path)
    args = parser.parse_args()
    prepare(args.target, args.generated)
