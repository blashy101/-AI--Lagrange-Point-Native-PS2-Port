"""Compare Linux video, PCM and RAM with native dispatch enabled and disabled."""
import argparse
import ctypes as C
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory


class Stats(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('compiled', 'mapped_fallback', 'ram_fallback')]


def replay(args, mode):
    sys.path.insert(0, str(args.disassembly / 'tools'))
    from trace_headless import ENV, VIDEO, AUDIO, BATCH, POLL, INPUT, BUTTONS, GameInfo, Variable, fm2_inputs

    if args.fm2:
        masks = fm2_inputs(args.fm2)
        if len(masks) < args.frames:
            raise ValueError('The movie is shorter than the requested replay.')
    else:
        masks = [[0, 0] for _ in range(args.frames)]
        for event in json.loads(args.script.read_text()):
            mask = sum(1 << BUTTONS[name] for name in event['buttons'])
            for f in range(event['start'], min(event['end'], args.frames)):
                masks[f][event.get('port', 0)] |= mask

    lib = C.CDLL(str(args.core.resolve()))
    lib.lp_select_release.argtypes = [C.c_int]
    lib.lp_select_release.restype = None
    lib.lp_get_cpu_stats.argtypes = [C.POINTER(Stats)]
    lib.lp_get_cpu_stats.restype = None
    use_wide = args.wide and mode == 'compiled'
    if use_wide:
        lib.lp_set_wide.argtypes = [C.c_int]
        lib.lp_set_wide.restype = None
        lib.lp_wide_frame.restype = C.c_void_p
    lib.retro_get_memory_data.argtypes = [C.c_uint]
    lib.retro_get_memory_data.restype = C.c_void_p
    lib.retro_get_memory_size.argtypes = [C.c_uint]
    lib.retro_get_memory_size.restype = C.c_size_t
    lib.retro_load_game.argtypes = [C.POINTER(GameInfo)]
    lib.retro_load_game.restype = C.c_bool
    variables = {b'fceumm_ramstate': b'fill $ff', b'fceumm_region': b'NTSC',
                 b'fceumm_overclocking': b'disabled', b'fceumm_sndrate_hint': b'48KHz'}
    video_hash, pcm_hash, memory_hash = sha256(), sha256(), sha256()
    frame = pictures = samples = wide_pictures = 0

    @ENV
    def environment(command, data):
        if command == 10:  # SET_PIXEL_FORMAT
            return C.cast(data, C.POINTER(C.c_int))[0] == 1
        if command == 15:  # GET_VARIABLE
            var = C.cast(data, C.POINTER(Variable)).contents
            var.value = variables.get(var.key)
            return var.value is not None
        if command == 17:  # GET_VARIABLE_UPDATE
            C.cast(data, C.POINTER(C.c_bool))[0] = False
            return True
        return command == (51 | 0x10000)  # GET_INPUT_BITMASKS

    @VIDEO
    def video(data, width, height, pitch):
        nonlocal pictures, wide_pictures
        if data:
            video_hash.update(C.string_at(data, height * pitch))
        if use_wide and lib.lp_wide_frame():
            wide_pictures += 1
        pictures += 1

    @BATCH
    def batch(data, count):
        nonlocal samples
        pcm_hash.update(C.string_at(data, 4 * count))
        samples += count
        return count

    @INPUT
    def input_state(port, device, index, button):
        if port > 1 or device != 1 or index:
            return 0
        mask = masks[frame][port]
        return mask if button == 256 else int(bool(mask & (1 << button)))

    setters = {'environment': environment, 'video_refresh': video, 'audio_sample': AUDIO(lambda *_: None),
               'audio_sample_batch': batch, 'input_poll': POLL(lambda: None), 'input_state': input_state}
    for name, callback in setters.items():
        setter = getattr(lib, 'retro_set_' + name)
        setter.argtypes = [type(callback)]
        setter.restype = None
        setter(callback)
    lib.retro_init()
    lib.lp_select_release(('en', 'ja').index(args.edition) if mode == 'compiled' else -1)
    rom_data = C.create_string_buffer(args.rom.read_bytes())
    game = GameInfo(args.rom.name.encode(), C.cast(rom_data, C.c_void_p), len(rom_data) - 1, None)
    if not lib.retro_load_game(C.byref(game)):
        raise RuntimeError('Failed to load game data.')
    if use_wide:
        lib.lp_set_wide(1)
    ram, sram = [(lib.retro_get_memory_data(i), lib.retro_get_memory_size(i)) for i in (2, 0)]
    if ram[1] != 2048 or sram[1] != 8192 or not (ram[0] and sram[0]):
        raise RuntimeError('The core did not expose both RAM regions.')
    for frame in range(args.frames):
        lib.retro_run()
        memory_hash.update(C.string_at(*ram))
        memory_hash.update(C.string_at(*sram))
    stats = Stats()
    lib.lp_get_cpu_stats(C.byref(stats))
    lib.retro_unload_game()
    lib.retro_deinit()
    if pictures != args.frames or not samples:
        raise RuntimeError('Expected video and PCM from the replay.')
    return dict(video_frames=pictures, audio_frames=samples, memory_sha256=memory_hash.hexdigest(),
                video_sha256=video_hash.hexdigest(), pcm_sha256=pcm_hash.hexdigest(), wide_frames=wide_pictures,
                counts={name: getattr(stats, name) for name, _ in Stats._fields_})


def compare(args):
    launch = [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]]
    with TemporaryDirectory(prefix='lp-replay-') as scratch:
        paths = [Path(scratch) / f'{mode}.json' for mode in ('compiled', 'reference')]
        children = [subprocess.Popen([*launch, '--run', path.stem, '--run-report', str(path)]) for path in paths]
        results = [child.wait() for child in children]
        if any(results):
            raise RuntimeError(f'Replay worker failures {results}.')
        compiled, reference = [json.loads(path.read_text()) for path in paths]
    ci, ri = compiled.pop('counts'), reference.pop('counts')
    wide, reference_wide = compiled.pop('wide_frames'), reference.pop('wide_frames')
    if compiled != reference or not ci['compiled'] or ri['compiled'] or sum(ci.values()) != sum(ri.values()):
        raise RuntimeError(f'Replay mismatch: {compiled} {ci} vs {reference} {ri}')
    inputs = args.fm2 or args.script
    report = dict(edition=args.edition, input=inputs.name, input_sha256=sha256(inputs.read_bytes()).hexdigest(),
                  frames=args.frames, **compiled, compiled_run=ci, reference_run=ri)
    if args.wide:
        if not wide or reference_wide:
            raise RuntimeError('Expected wide gameplay pictures in the compiled run.')
        report['wide_frames'] = wide
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('core', type=Path)
    parser.add_argument('disassembly', type=Path)
    parser.add_argument('rom', type=Path)
    parser.add_argument('edition', choices=('en', 'ja'))
    parser.add_argument('report', type=Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--script', type=Path)
    source.add_argument('--fm2', type=Path)
    parser.add_argument('--frames', type=int, required=True)
    parser.add_argument('--wide', action='store_true', help='Render the wide view while checking original video, PCM and RAM.')
    parser.add_argument('--run', choices=('compiled', 'reference'), help=argparse.SUPPRESS)
    parser.add_argument('--run-report', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.frames < 1:
        parser.error('--frames must be positive.')
    if args.run:
        args.run_report.write_text(json.dumps(replay(args, args.run)))
    else:
        compare(args)
