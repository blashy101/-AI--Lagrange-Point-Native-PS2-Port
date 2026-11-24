"""Create a PS2 ISO9660 image; all runtime drivers are embedded in the ELF."""
import argparse
from io import BytesIO
from pathlib import Path
import pycdlib

BOOT = 'LPNT_001.00'


def build(elf, destination):
    executable = elf.read_bytes()
    if executable[:6] != b'\x7fELF\x01\x01' or int.from_bytes(executable[18:20], 'little') != 8:
        raise ValueError('Expected a little-endian ELF32 MIPS executable.')
    files = {
        '/SYSTEM.CNF;1': f'BOOT2 = cdrom0:\\{BOOT};1\r\nVER = 1.00\r\nVMODE = NTSC\r\n'.encode('ascii'),
        f'/{BOOT};1': executable,
    }
    image = pycdlib.PyCdlib()
    image.new(interchange_level=1, sys_ident='PLAYSTATION', vol_ident='LAGRANGE_POINT')
    for path, data in files.items():
        image.add_fp(BytesIO(data), len(data), iso_path=path)
    image.write(str(destination))
    image.close()

    image.open(str(destination))
    for path, expected in files.items():
        recovered = BytesIO()
        image.get_file_from_iso_fp(recovered, iso_path=path)
        if recovered.getvalue() != expected:
            raise ValueError(f'ISO verification failed for {path}.')
    image.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('iso', type=Path)
    args = parser.parse_args()
    build(args.elf, args.iso)
