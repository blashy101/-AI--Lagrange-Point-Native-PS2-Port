# Lagrange Point for PS2

The standalone ISO includes the verified Japanese release and Aeon Genesis English v1.01. Select a language before starting. Both editions retain the original game rules, text and graphics.

## Running and options

Boot `LPNT_001.00.Lagrange_Point_PS2.iso` as a disc image. `LAGRANGE.ELF` is also included for a homebrew launcher.

The startup menu contains **Screen**, **Output**, **Language** and **Play**. During play, hold **L2 + R2** to open the paused options menu; **Resume** replaces the language and Play rows.

- **4:3** uses the original view for a 4:3 display. At 720p it is centered inside the widescreen output.
- **16:9** automatically enables expanded field maps and actors. Battles, titles and full-screen menus keep their original framing. Set PCSX2's display aspect ratio to match your choice.
- **Output** offers NTSC 480i, 480p and experimental 720p. Cross confirms a new mode, Circle restores the previous mode, and an unconfirmed mode reverts after about eight seconds.

The sharp filter, +6 dB volume gain and balanced intro audio are always enabled. Stereo peak protection prevents the added gain from clipping louder sounds. These adjustments are no longer menu options.

All game data and support drivers are embedded in the executable. After the loader has read the ELF, the port restarts the IOP without restoring the boot-device drivers. This detaches MX4SIO from SIO2 before controller and memory-card services start, and removes idle USB/disc services. The port does not read the ISO, USB drive or MX4SIO adapter during play. Controller, card and audio drivers come from ROM or their embedded fallback; no external IRX files are required. Actual RPC endpoint checks determine service availability. Startup reset requests and reboot completion have bounded waits. Use a PS2 controller and a physical Slot 1 memory card; loader-provided pad emulation and virtual-card services are not retained by the clean restart.

PCM delivery runs on its own EE thread. The game callback copies audio into a bounded queue and returns without waiting for IOP audio requests. The worker normally submits each block with one RPC, waiting for ring space only after a short transfer. Startup normalizes the game thread's priority so a loader's inherited priority cannot starve audio during frame catch-up. Pausing mutes output while keeping the audio clock running. Resume replaces one ring of stale PCM before unmuting.

The VRC7 mixer processes independent channels in small blocks while preserving every sample and the complete chip state. Translated instruction pages validate ROM read handlers when those handlers change, and invalidate the cached page for intercepted reads. The EE's integer SIMD unit handles wide-image palette conversion and settled +6 dB audio gain. These changes retain the original simulation rate, audio rate and game rules.

Version 12 reduces the CPU dispatch loop from 41,324 to 3,020 bytes by moving the rarely used interpreter into a separate function. Bilingual replay profiles place 46 frequently used instruction handlers and the dispatch loop together in 11,056 bytes of code. Their descriptors are adjacent too. VRC7 temporary sample arrays shrink from 3,072 to 384 bytes with identical output. These changes target cache pressure on the physical PS2; console performance still needs a retest. The working MX4SIO startup path, save format, icon and menu choices are retained.

| PS2 control | Game input |
| --- | --- |
| Cross | A |
| Square | B |
| D-pad | D-pad |
| Start / Select | Start / Select |

## Saving and loading

Use a **formatted PS2 memory card in Slot 1**. In PCSX2, enable the first memory card slot. The game never formats a card automatically.

Use the game's normal save command. The port detects completed, checksummed game records and writes them in a background thread. Wait for **SAVED TO MEMORY CARD** before switching off. Missing, unformatted or full cards produce a message; an unsaved record remains pending for retry.

When starting a language, the port loads its records before the first game frame. Use the game's normal file-selection screen to continue. English and Japanese have separate save files, and both of the original in-game slots are retained for each language.

The memory card browser entry is **LAGRANGE POINT**, with a blue and gold cover-art plaque, in directory `BILPNT-00100`. Saves use alternating, checksummed copies. After a write, the port closes and rereads the file before reporting success. If a newer copy is truncated or damaged, loading can use the previous valid copy.

Existing saves are compatible. When a language's records load, the background worker checks the icon and upgrades it if needed; **SAVE ICON UPDATED** confirms the change. The old model and both languages' game records are retained. If the new icon is already present, this check makes no writes and shows no extra notice. No save deletion or conversion is needed.

Only the original two 656-byte records and their two 16-byte auxiliary blocks are persisted. RNG, map, audio and other working cartridge RAM do not cause repeated card writes. This is ordinary in-game saving, not an emulator save state. Boot the new ISO normally when testing it; an old PCSX2 save state restores the old executable and its memory.

## Widescreen

Expanded field rendering reads the original map, camera, CHR banks and palettes. It extends the map and NPCs horizontally at the same scale, repairs exposed scrolling rows and stale background tiles, and retains sprite priority. Field dialogue and pickup windows span the wide image while preserving glyph size and the original line breaks. Some maps can still expose imperfect borders.

## Optional hardware timing display

Use the normal ISO for playing. If a console still stutters, `diagnostics/LPNT_001.00.Lagrange_Point_PS2_Profile.iso` runs the same game with a timing display in the paused menu. Boot it normally, reproduce a dip, then hold **L2 + R2**. The display freezes the latest sample and shows average/maximum microseconds, frame/field/presentation counts, and audio drops/errors. A photo of that screen can identify the remaining bottleneck. One thousand microseconds is one millisecond.

WORK measures the active update after input scanning, including card polling and rendering; the initial wait for a field is excluded. CORE includes game execution, device synthesis and callbacks. WIDE and AUDIO are portions of CORE, so do not add them to it. AUDIO measures the frontend processing/queue callback, not all synthesis or IOP playback. RENDER includes waiting for an earlier queued presentation. Samples roll over every 240 updates; pausing captures the current partial sample when available. The diagnostic adds measurement overhead; the normal ISO contains no timing instrumentation.

## Build

Use Python 3, GNU Make and [PS2DEV v2.0.0](https://github.com/ps2dev/ps2dev/releases/tag/v2.0.0), including PS2SDK and gsKit. Set `PS2DEV` and `PS2SDK` and add the EE toolchain to `PATH`. Install `pycdlib==1.22.0` for the ISO target and Pillow for rebuilding the icon.

```sh
make -j4 DISASSEMBLY=/path/to/Lagrange_Point_Disassembly iso
make -j4 DISASSEMBLY=/path/to/Lagrange_Point_Disassembly profile-iso
make check-display check-timing check-audio check-audio-stream check-iop check-icon check-wide check-pixels check-save check-code-cache check-vrc7
```

The input is version 61 of the bilingual disassembly. Keep its source, included assets, manifests, required trace CSVs and assembler tools together. The translator verifies both images and generates specialized instruction handlers. The device runtime supplies PPU, APU, VRC7, mapper and timing behavior. The frontend uses gsKit, libpad, libmc and the PS2SDK audio server.

Keep `config/dispatch_layout.json` with the source. It defines deterministic private instruction IDs and the frequently used handler set, derived from the documented English and Japanese replay inputs. Game bytes and guest addresses are unchanged. `tools/make_linkfile.py` derives a linker script from the installed PS2SDK script to group the hot code while retaining its startup and memory layout.

Memory card calls use the standard PS2SDK interface after the clean IOP restart. ROM and embedded open-source drivers provide the services. Service availability is checked before calling the SDK clients that would otherwise wait indefinitely for a missing endpoint. Writes run on a separate EE thread; the main thread owns game RAM and snapshots completed saves for that worker. See `src/save_data.c` and `src/memory_card.c` for the record layout and write/readback sequence.

## Verification

`verification/hardware-performance.json` records version 12's code/data layout, bilingual replay checks, native controller/card tests and timing limitations. Full host checks and test logs accompany the report. `verification/performance-reset.json` and the other earlier reports retain the startup, native SIMD, audio, icon and memory-card evidence from previous releases.

English and Japanese replay hashes are compared with the previous release as well as ordinary instruction execution. VRC7 differential tests compare mixed PCM and complete chip states under register changes, masks, resets, varied block lengths and sample rates. Native PS2 instruction probes check pixel/gain results and buffer guards in PCSX2.

The user confirmed that version 11 boots on MX4SIO and runs well in PCSX2, with frequent dips remaining on physical PS2 hardware. Version 12 retains that startup path. Its short PCSX2 timing workload showed similar performance, not a measured speedup: 600 new presentations over 604 fields for version 11 and 607 fields for version 12. Emulator timing does not establish a physical-console cache benefit. A console retest is required to assess this update. Existing game records and the cover-art icon remain compatible. Boot the new ISO normally: an old PCSX2 save state restores the old executable and memory.

## Credits and licenses

Lagrange Point is by Konami. The English translation is by Aeon Genesis. The version 61 disassembly retains its provenance. The port's code is GPL-2.0-or-later.

The included device runtime is [FCEUmm commit ac0d00a7fc01166f84c7bac01d1084ebfd2b865e](https://github.com/libretro/libretro-fceumm/tree/ac0d00a7fc01166f84c7bac01d1084ebfd2b865e). PS2SDK, gsKit and module license notices are included under `licenses/` and `third_party/`.
