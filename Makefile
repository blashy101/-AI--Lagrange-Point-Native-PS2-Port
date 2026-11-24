PYTHON ?= python3
DISASSEMBLY ?= ../Lagrange_Point_Disassembly
GSKIT ?= $(PS2DEV)/gsKit

LIBRETRO := third_party/fceumm/src/drivers/libretro/libretro-common/include
EE_PREFIX ?= mips64r5900el-ps2-elf-
EE_CC := $(EE_PREFIX)gcc
EE_STRIP := $(EE_PREFIX)strip
EE_FLAGS := -D_EE -DPS2 -G0 -O2 -Wall -Wextra -Werror -std=gnu11
EE_CORE_FLAGS := -O3 -mtune=r5900 -flto -fno-semantic-interposition
EE_INCS := -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I$(GSKIT)/include -I$(LIBRETRO)
EE_CORE := build/core-ee/fceumm_libretro_ps2.a
HOST_CORE := build/core-host/fceumm_libretro.so
IOP_IRX := $(PS2SDK)/iop/irx
ELF := build/LAGRANGE.ELF
ISO := build/LPNT_001.00.Lagrange_Point_PS2.iso

.DELETE_ON_ERROR:
.PHONY: all host elf iso check-display check-timing check-audio check-audio-stream check-iop check-icon check-wide check-pixels check-save profile-iso clean
all: elf
elf: $(ELF)
host: $(HOST_CORE)
iso: $(ISO)

build/generated/translation.json: tools/translate.py config/dispatch_layout.json
	$(PYTHON) tools/translate.py "$(DISASSEMBLY)" build/generated

build/core-ee/.prepared build/core-host/.prepared: Makefile tools/integrate.py src/native_cpu.inc src/cpu_fallback.inc src/native_map_cache.inc src/vrc7_fill.inc src/lp_core.h src/wide_field.inc src/ppu_pixels.h build/generated/translation.json
	$(PYTHON) tools/integrate.py $(@D) build/generated
	touch $@

$(EE_CORE): build/core-ee/.prepared
	$(MAKE) -C build/core-ee -f Makefile.libretro platform=ps2 STANDALONE=1 \
	    AR=$(EE_PREFIX)gcc-ar LP_CORE_FLAGS="$(EE_CORE_FLAGS)"

$(HOST_CORE): build/core-host/.prepared
	$(MAKE) -C build/core-host -f Makefile.libretro platform=unix HAVE_NTSC=0 STANDALONE=1

build/ee/frontend.o: src/frontend.c src/performance.h src/display.h src/frame_clock.h src/intro_audio.h src/audio_stream.h src/iop_modules.h src/memory_card.h src/lp_core.h
	mkdir -p $(@D)
	$(EE_CC) $(EE_FLAGS) $(EE_INCS) -c $< -o $@

build/ee/save_data.o: src/save_data.c src/save_data.h
	mkdir -p $(@D)
	$(EE_CC) $(EE_FLAGS) -c $< -o $@

build/ee/memory_card.o: src/memory_card.c src/memory_card.h src/save_data.h
	mkdir -p $(@D)
	$(EE_CC) $(EE_FLAGS) $(EE_INCS) -c $< -o $@

build/ee/audio_stream.o: src/audio_stream.c src/audio_stream.h
	mkdir -p $(@D)
	$(EE_CC) $(EE_FLAGS) $(EE_INCS) -c $< -o $@

build/ee/iop_modules.o: src/iop_modules.c src/iop_modules.h
	mkdir -p $(@D)
	$(EE_CC) $(EE_FLAGS) $(EE_INCS) -c $< -o $@

build/ee/intro_audio.o: src/intro_audio.c src/intro_audio.h
	mkdir -p $(@D)
	$(EE_CC) $(EE_FLAGS) -c $< -o $@

build/ee/assets.o: src/assets.S assets/save.icn build/generated/translation.json
	mkdir -p $(@D)
	$(EE_CC) -G0 -DAUDSRV_IRX='"$(IOP_IRX)/audsrv.irx"' -DLIBSD_IRX='"$(IOP_IRX)/libsd.irx"' -DMCMAN_IRX='"$(IOP_IRX)/mcman.irx"' -DMCSERV_IRX='"$(IOP_IRX)/mcserv.irx"' -DSIO2MAN_IRX='"$(IOP_IRX)/sio2man.irx"' -DPADMAN_IRX='"$(IOP_IRX)/padman.irx"' -c $< -o $@

build/ee/linkfile: tools/make_linkfile.py $(PS2SDK)/ee/startup/linkfile
	$(PYTHON) tools/make_linkfile.py "$(PS2SDK)/ee/startup/linkfile" "$@"

$(ELF): build/ee/frontend.o build/ee/intro_audio.o build/ee/audio_stream.o build/ee/iop_modules.o build/ee/save_data.o build/ee/memory_card.o build/ee/assets.o $(EE_CORE) build/ee/linkfile
	$(EE_CC) $(EE_CORE_FLAGS) -flto=auto -Tbuild/ee/linkfile -Wl,-zmax-page-size=128 \
	    -L"$(PS2SDK)/ee/lib" -L"$(GSKIT)/lib" -o build/lagrange-symbols.elf $(filter %.o %.a,$^) \
	    -lgskit -ldmakit -lpatches -laudsrv -lpad -lmc -lm
	$(EE_STRIP) -o $@ build/lagrange-symbols.elf

$(ISO): $(ELF) tools/make_iso.py
	$(PYTHON) tools/make_iso.py "$<" "$@"

check-display:
	mkdir -p build
	$(CC) -Wall -Wextra -Werror -std=c11 -Isrc tests/display.c -o build/display-test
	build/display-test

check-timing:
	mkdir -p build
	$(CC) -Wall -Wextra -Werror -std=c11 -Isrc tests/frame_clock.c -o build/frame-clock-test
	build/frame-clock-test

check-audio:
	mkdir -p build
	$(CC) -Wall -Wextra -Werror -std=c11 -Isrc tests/intro_audio.c src/intro_audio.c -lm -o build/intro-audio-test
	build/intro-audio-test
	$(CC) -Wall -Wextra -Werror -std=c11 -Isrc tests/audio_vectors.c src/intro_audio.c -lm -o build/audio-vectors-test
	build/audio-vectors-test

check-audio-stream:
	mkdir -p build
	$(CC) -Wall -Wextra -Werror -std=c11 -pthread -Itests/audio_mock -Itests/card_mock -Isrc tests/audio_stream.c src/audio_stream.c -o build/audio-stream-test
	build/audio-stream-test

check-iop:
	mkdir -p build
	$(CC) -Wall -Wextra -Werror -std=c11 -Itests/iop_mock -Isrc tests/iop_modules.c src/iop_modules.c tests/iop_mock/assets.S -o build/iop-modules-test
	$(PYTHON) -c 'import subprocess; [subprocess.run(["build/iop-modules-test", s], check=True, timeout=5) for s in ["opl", "rom", "embedded", "native-lmb", "missing-pad", "missing-sio2", "missing-audio-rpc", "hung-audio-rpc", "patch-failure", "legacy-search-zero", "legacy-search-positive", "residency-flags", "opaque-list", "mx4sio", "usb", "reset-request-timeout", "reset-dma-timeout", "reset-sync-timeout", "resident-mx4sio"]]'

check-icon: assets/save.icn
	$(PYTHON) tests/save_icon.py

check-wide:
	mkdir -p build
	$(CC) -Wall -Wextra -Werror -std=c11 -Isrc tests/wide_field.c -o build/wide-field-test
	build/wide-field-test

check-pixels:
	mkdir -p build
	$(CC) -Wall -Wextra -Werror -std=c11 -Isrc tests/ppu_pixels.c -o build/ppu-pixels-test
	build/ppu-pixels-test

check-save:
	mkdir -p build
	$(CC) -Wall -Wextra -Werror -std=c11 -Isrc tests/save_data.c src/save_data.c -o build/save-data-test
	build/save-data-test
	$(CC) -Wall -Wextra -Werror -std=c11 -pthread -Itests/card_mock -Isrc tests/memory_card.c tests/card_mock/runtime.c src/memory_card.c src/save_data.c tests/card_mock/assets.S -o build/memory-card-test
	$(PYTHON) tests/test_memory_card.py

assets/save.icn: tools/make_save_icon.py assets/save-cover.png
	$(PYTHON) tools/make_save_icon.py

clean:
	rm -rf build

check-code-cache:
	mkdir -p build
	$(CC) -Wall -Wextra -Werror -std=c11 -Isrc tests/native_map_cache.c -o build/native-map-cache-test
	build/native-map-cache-test

check-vrc7:
	mkdir -p build
	$(CC) -O2 -Wall -Wextra -Werror -Wno-calloc-transposed-args -std=gnu11 tests/vrc7_fill.c -lm -o build/vrc7-fill-test
	build/vrc7-fill-test

build/ee/frontend-profile.o: src/frontend.c src/performance.h src/display.h src/frame_clock.h src/intro_audio.h src/audio_stream.h src/iop_modules.h src/memory_card.h src/lp_core.h
	mkdir -p $(@D)
	$(EE_CC) $(EE_FLAGS) -DLP_PERF=1 $(EE_INCS) -c $< -o $@

build/LAGRANGE_PROFILE.ELF: build/ee/frontend-profile.o build/ee/intro_audio.o build/ee/audio_stream.o build/ee/iop_modules.o build/ee/save_data.o build/ee/memory_card.o build/ee/assets.o $(EE_CORE) build/ee/linkfile
	$(EE_CC) $(EE_CORE_FLAGS) -flto=auto -Tbuild/ee/linkfile -Wl,-zmax-page-size=128 -L"$(PS2SDK)/ee/lib" -L"$(GSKIT)/lib" -o build/lagrange-profile-symbols.elf $(filter %.o %.a,$^) -lgskit -ldmakit -lpatches -laudsrv -lpad -lmc -lm
	$(EE_STRIP) -o $@ build/lagrange-profile-symbols.elf

build/LPNT_001.00.Lagrange_Point_PS2_Profile.iso: build/LAGRANGE_PROFILE.ELF tools/make_iso.py
	$(PYTHON) tools/make_iso.py "$<" "$@"

profile-iso: build/LPNT_001.00.Lagrange_Point_PS2_Profile.iso
