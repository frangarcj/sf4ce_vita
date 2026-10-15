#!/usr/bin/env python3
"""Check the distributable VPK structure without requiring proprietary assets."""
import argparse
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile


def check(path):
    with zipfile.ZipFile(path) as vpk:
        names = set(vpk.namelist())
        required = {'eboot.bin', 'sce_sys/param.sfo', 'sce_sys/icon0.png',
                    'sce_sys/livearea/contents/template.xml',
                    'sce_sys/livearea/contents/bg0.png',
                    'sce_sys/livearea/contents/startup.png'}
        required |= {f'LICENSES/{name}.txt' for name in
                     ('Apache-2.0', 'LGPL-2.1-or-later', 'LGPL-3.0-or-later', 'MIT')}
        missing = required - names
        if missing:
            raise ValueError(f'Missing package files: {sorted(missing)}')
        allowed = required | {'LICENSE', 'THIRD_PARTY_NOTICES.md',
                              'sce_sys/livearea/contents/config.png'}
        if names != allowed:
            raise ValueError(f'Unexpected/missing package entries: {sorted(names ^ allowed)}')
        for name in names:
            if Path(name).suffix.lower() in {'.so', '.obm', '.ckb', '.cks', '.apk', '.xapk', '.mp4'}:
                raise ValueError(f'Game data must be supplied separately: {name}')
            if name.startswith('/') or '..' in Path(name).parts:
                raise ValueError(f'Unsafe package path: {name}')
        if vpk.read('eboot.bin')[:4] != b'SCE\0':
            raise ValueError('eboot.bin is not a Vita SELF')
        livearea = ET.fromstring(vpk.read('sce_sys/livearea/contents/template.xml'))
        if livearea.get('style') != 'a5' or livearea.get('format-ver') != '01.00':
            raise ValueError('Invalid Vita LiveArea format')
        if livearea.findtext('livearea-background/image') != 'bg0.png' or livearea.findtext('gate/startup-image') != 'startup.png':
            raise ValueError('Missing Vita LiveArea background/startup image')
        dimensions = {'sce_sys/icon0.png': (128, 128),
                      'sce_sys/livearea/contents/bg0.png': (840, 500),
                      'sce_sys/livearea/contents/startup.png': (280, 158),
                      'sce_sys/livearea/contents/config.png': (320, 70)}
        for name, expected in dimensions.items():
            png = vpk.read(name)
            if png[:8] != b'\x89PNG\r\n\x1a\n' or png[12:16] != b'IHDR':
                raise ValueError(f'Invalid PNG: {name}')
            width, height, bits, mode = struct.unpack_from('>IIBB', png, 16)
            if (width, height) != expected or bits != 8 or mode != 3:
                raise ValueError(f'Invalid Vita PNG size/format: {name}')
        sfo = vpk.read('sce_sys/param.sfo')
        magic, version, key_offset, data_offset, count = struct.unpack_from('<5I', sfo)
        if magic != 0x46535000:
            raise ValueError('Invalid param.sfo')
        fields = {}
        for i in range(count):
            key, fmt, length, max_length, offset = struct.unpack_from('<HHIII', sfo, 20 + 16 * i)
            start = key_offset + key
            name = sfo[start:sfo.index(b'\0', start)].decode()
            fields[name] = sfo[data_offset + offset:data_offset + offset + length].rstrip(b'\0')
        if fields.get('TITLE_ID') != b'SF4CE0000':
            raise ValueError('Unexpected TITLE_ID')
        print(f'{path}: SELF, TITLE_ID, LiveArea XML and package contents OK ({len(names)} entries)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vpk', nargs='?', type=Path, default=Path('build/SF4CE.vpk'))
    args = parser.parse_args()
    try:
        check(args.vpk)
    except (OSError, ValueError, KeyError, struct.error, zipfile.BadZipFile, ET.ParseError) as error:
        print(f'Package check failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
