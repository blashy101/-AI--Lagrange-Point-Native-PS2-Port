"""Build both verified images and their physical PRG instruction maps."""
import argparse
import csv
from hashlib import sha256
import json
from pathlib import Path
import re
import sys

LAYOUT = Path(__file__).resolve().parent.parent / 'config/dispatch_layout.json'

BANK = 8192
PRG = 64 * BANK
RELEASES = {
    'en': '333cfe66a6e687f34a347e83cf5b82a997cb32415fc449f9d74eddfc616a5db7',
    'ja': '3e8b4b68da3424a03c51aeceb5acb345d1d2738ad87342899a4053064a243b25',
}
LINE = re.compile(r'; \$([0-9A-F]{4}) \[[TSCR]\] ([0-9A-F ]+)$')


def instruction_sites(root, segments, prg):
    sites = {}
    for bank, segment in enumerate(segments[1:]):
        for line in (root / segment['source']).read_text(encoding='utf-8').splitlines():
            match = LINE.search(line)
            if not match:
                continue
            raw = bytes.fromhex(match[2])
            relative = int(match[1], 16) - segment['cpu_base']
            offset = bank * BANK + relative
            if not 0 <= relative <= BANK - len(raw) or len(raw) not in (1, 2, 3):
                raise ValueError(f'Instruction crosses a bank at {offset:05X}.')
            if offset in sites or prg[offset:offset + len(raw)] != raw:
                raise ValueError(f'Invalid instruction boundary at {offset:05X}.')
            sites[offset] = raw
    return sites


def translate(root, out):
    sys.path.insert(0, str(root / 'tools'))
    from assemble import assemble
    from project import build_manifest

    out.mkdir(parents=True, exist_ok=True)
    maps, audit = {}, {}
    for release, digest in RELEASES.items():
        image = assemble(root, release)[0]
        if len(image) != 16 + PRG or sha256(image).hexdigest() != digest:
            raise ValueError(f'{release} build differs from the verified release.')
        manifest = build_manifest(root, release)
        (out / f'{release}.nes').write_bytes(image)
        sites = maps[release] = instruction_sites(root, manifest['segments'], image[16:])
        missing = set()
        for name in manifest['trace_files']:
            with (root / name).open(newline='', encoding='utf-8') as trace:
                for row in csv.DictReader(trace):
                    offset = int(row['prg_offset'], 16)
                    if not 0 <= offset < PRG:
                        raise ValueError(f'Trace offset outside PRG at {offset:05X}.')
                    if offset not in sites:
                        missing.add(offset)
        audit[release] = dict(sha256=digest, translated_prg_starts=len(sites),
                              other_traced_prg_starts=[f'{i:05X}' for i in sorted(missing)])

    found = {raw for sites in maps.values() for raw in sites.values()}
    layout = json.loads(LAYOUT.read_text())
    encodings = [bytes.fromhex(raw) for raw in layout['encoding_order']]
    if len(encodings) != len(set(encodings)) or set(encodings) != found:
        raise ValueError('Dispatch layout must cover each verified instruction encoding exactly once.')
    if len(set(layout['hot_encodings'])) != len(layout['hot_encodings']) or not set(layout['hot_encodings']) <= set(layout['encoding_order']):
        raise ValueError('Hot dispatch entries must be unique verified encodings.')
    if len(encodings) >= 65535:
        raise ValueError('The compiled handler map exceeds 16-bit indices.')
    indexes = {raw: i for i, raw in enumerate(encodings, 1)}
    (out / 'encodings.json').write_text(json.dumps([raw.hex() for raw in encodings]))
    with (out / 'native_map.inc').open('w', encoding='ascii') as stream:
        stream.write(f'static const uint16_t lp_instruction_map[2][{PRG}] = {{\n')
        for sites in maps.values():
            stream.write('    {\n')
            for offset, raw in sorted(sites.items()):
                stream.write(f'        [0x{offset:05X}] = {indexes[raw]},\n')
            stream.write('    },\n')
        stream.write('};\n')
    audit['unique_native_encodings'] = len(encodings)
    audit['dispatch_layout_sha256'] = sha256(LAYOUT.read_bytes()).hexdigest()
    audit['hot_native_encodings'] = len(layout['hot_encodings'])
    (out / 'translation.json').write_text(json.dumps(audit, indent=2) + '\n')
    print(json.dumps(audit))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disassembly', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    translate(args.disassembly.resolve(), args.destination)
