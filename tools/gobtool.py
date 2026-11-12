#!/usr/bin/env python3
"""
List or extract the contents of Jones3D GOB archives (PORT-PLAN.md Phase 1 test inputs).

Format (Libs/std/types.h, stdGob.c): header { char[4] "GOB ", int32 version = 20, int32 dirOffset }; at dirOffset an
int32 entry count, then entries { int32 offset, int32 size, char[128] name }. Names use '\\' and are lower case.

Usage:
  gobtool.py list    <file.gob> [glob]          print "size  name" for every entry (glob matches the name, e.g. '*.cnd')
  gobtool.py extract <file.gob> <outdir> [glob]  write matching entries under outdir, '\\' turned into '/'
"""
import fnmatch
import pathlib
import struct
import sys


def read_directory(path):
    with open(path, 'rb') as f:
        sig, version, dir_offset = struct.unpack('<4sii', f.read(12))
        if sig != b'GOB ' or version != 20:
            sys.exit(f'{path}: not a version-20 GOB (signature {sig!r}, version {version})')
        f.seek(dir_offset)
        (count,) = struct.unpack('<i', f.read(4))
        entries = []
        for _ in range(count):
            offset, size, raw = struct.unpack('<ii128s', f.read(136))
            entries.append((offset, size, raw.split(b'\0', 1)[0].decode('latin-1')))
        return entries


def matching(entries, pattern):
    return [e for e in entries if pattern is None or fnmatch.fnmatch(e[2].lower(), pattern.lower())]


def main(argv):
    if len(argv) < 3 or argv[1] not in ('list', 'extract'):
        sys.exit(__doc__)

    gob = pathlib.Path(argv[2])
    entries = read_directory(gob)

    if argv[1] == 'list':
        for _, size, name in matching(entries, argv[3] if len(argv) > 3 else None):
            print(f'{size:10d}  {name}')
        return 0

    if len(argv) < 4:
        sys.exit(__doc__)
    outdir = pathlib.Path(argv[3]).resolve()
    selected = matching(entries, argv[4] if len(argv) > 4 else None)
    with open(gob, 'rb') as f:
        for offset, size, name in selected:
            dest = (outdir / name.replace('\\', '/')).resolve()
            if outdir not in dest.parents:  # names come from the archive; never write outside outdir
                sys.exit(f'refusing to extract {name!r} outside {outdir}')
            dest.parent.mkdir(parents=True, exist_ok=True)
            f.seek(offset)
            dest.write_bytes(f.read(size))
    print(f'{len(selected)} file(s) extracted to {outdir}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
