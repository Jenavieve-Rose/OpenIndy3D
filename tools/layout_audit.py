#!/usr/bin/env python3
"""
64-bit layout audit (PORT-PLAN.md Phase 1): compare record layouts between the original 32-bit ABI and arm64.

Every struct that is read from or written to a file as raw bytes must have the same size and field offsets as the
1999 build, or levels and resources are silently misread. This tool compiles the translation units that do such I/O
twice with clang's record-layout dump, once for the port (arm64-apple-macos) and once approximating the retail ABI
(i386 with MSVC's 8-byte double/int64 alignment; wchar_t is checked by hand, -fshort-wchar breaks the SDK headers), and reports every audited type whose layout
differs.

Usage: python3 tools/layout_audit.py [--all]     (from the repository root, after a CMake configure in build/)
  --all   report every differing record seen in those translation units, not only the audited on-disk types
Exit status is 1 if an audited type differs.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Translation units that read or write raw structs.
SOURCES = [
    'Libs/sith/World/sithWorld.c', 'Libs/sith/World/sithThing.c', 'Libs/sith/World/sithSector.c',
    'Libs/sith/World/sithSurface.c', 'Libs/sith/World/sithMaterial.c', 'Libs/sith/World/sithPVS.c',
    'Libs/sith/Engine/sithPuppet.c', 'Libs/sith/Cog/sithCog.c', 'Libs/sith/Dss/sithGamesave.c',
    'Libs/sound/Sound.c', 'Libs/rdroid/Engine/rdMaterial.c', 'Libs/rdroid/Primitives/rdFont.c',
    'Libs/std/Win95/stdGob.c', 'Libs/std/General/stdBmp.c',
]

# Types whose bytes cross a file boundary, by typedef name. 'retail' = must match the shipped data files exactly;
# 'save' = written and read back by the port itself (savegames), so only self-consistency is required, but a
# difference still means pointers or size_t are being persisted and deserve a look.
AUDITED = {
    # Compiled level (.cnd) sections
    'CndWorld': 'retail', 'CndMaterialInfo': 'retail', 'CndSectorInfo': 'retail', 'CndSurfaceAdjoin': 'retail',
    'CndSurfaceInfo': 'retail', 'CndSurfaceVertInfo': 'retail', 'CndKeyframeInfo': 'retail',
    'CndKeyframeMarker': 'retail', 'CndKeyframeNode': 'retail', 'rdKeyframeNodeEntry': 'retail',
    'SithThingBinaryHeader': 'retail', 'SithThingBinaryRecord': 'retail', 'SithPhysicsBinaryRecord': 'retail',
    'SithPathFrame': 'retail', 'SithActorBinaryRecord': 'retail', 'SithWeaponBinaryRecord': 'retail',
    'SithExplosionBinaryRecord': 'retail', 'SithItemBinaryRecord': 'retail', 'SithHintBinaryRecord': 'retail',
    'SithParticleBinaryRecord': 'retail', 'SithAIBinaryRecord': 'retail',
    'SoundInfoFile': 'retail',  # SoundInfo itself is in-memory only; Sound.c converts
    # Cog symbol values are stored as strings in .cnd files; the union only reaches disk inside NdsHeader.
    'SithCogValue': 'save',
    # Resources
    'rdMatHeader': 'retail', 'rdMatCelInfo': 'retail', 'rdMatTextureCelInfo': 'retail',
    'rdMatTextureHeader': 'retail', 'rdGlyphMetrics': 'retail', 'tGobFileHeader': 'retail',
    'tGobFileEntry': 'retail', 'BITMAPFILEHEADER': 'retail', 'BITMAPINFOHEADER': 'retail',
    # Savegames
    'NdsHeader': 'save',
}

TARGETS = {
    'retail': ['-target', 'i386-apple-macos10.13', '-malign-double'],
    'port':   ['-target', 'arm64-apple-macos13'],
}


def compile_flags():
    return [
        '-DJ3D_PORT=1', '-DNDEBUG', '-DNONMATCHING_MATH',
        f'-I{ROOT}/build/generated', f'-I{ROOT}/Libs', f'-I{ROOT}', f'-I{ROOT}/platform/apple/shim',
        '-std=gnu11', '-fno-strict-aliasing', '-fwrapv', '-w', '-fsyntax-only',
        '-Xclang', '-fdump-record-layouts',
    ]


def typedef_tags():
    """Map struct/union tag -> typedef name, from 'typedef struct sTag ... } Name;' and 'typedef struct sTag Name;'."""
    tags = {}
    for path in list(ROOT.glob('Libs/**/*.[ch]')) + list(ROOT.glob('Jones3D/**/*.[ch]')) + \
            list(ROOT.glob('platform/**/*.h')):
        text = path.read_text(errors='replace')
        for m in re.finditer(r'typedef\s+(?:struct|union)\s+(\w+)\s+(\w+)\s*;', text):
            tags.setdefault(m.group(1), m.group(2))
        for m in re.finditer(r'typedef\s+(?:struct|union)\s+(\w+)\s*(?:(?://[^\n]*|/\*.*?\*/)\s*)*\{', text):
            depth, i = 1, m.end()
            while depth and i < len(text):
                depth += {'{': 1, '}': -1}.get(text[i], 0)
                i += 1
            name = re.match(r'\s*(\w+)', text[i:])
            if name:
                tags.setdefault(m.group(1), name.group(1))
    return tags


_unnamed_cache = {}


def unnamed_typedef(path, line, col):
    """Name of the typedef whose anonymous struct body starts at path:line:col, or the location if there is none."""
    key = (path, line, col)
    if key not in _unnamed_cache:
        name = f'{pathlib.Path(path).name}:{line}'
        try:
            lines = pathlib.Path(path).read_text(errors='replace').split('\n')
            text = '\n'.join(lines[line - 1:])[col - 1:]
            start = text.index('{')
            depth, i = 1, start + 1
            while depth and i < len(text):
                depth += {'{': 1, '}': -1}.get(text[i], 0)
                i += 1
            m = re.match(r'\s*(\w+)', text[i:])
            if m:
                name = m.group(1)
        except (OSError, ValueError):
            pass
        _unnamed_cache[key] = name
    return _unnamed_cache[key]


LAYOUT_RE = re.compile(r'\*\*\* Dumping AST Record Layout\n(.*?)\n\s+\| \[sizeof=(\d+), (?:dsize=\d+, )?align=(\d+)', re.S)


def parse_layouts(dump):
    """Return {record name: (size, align, [(offset, field)...] for top-level fields)}."""
    layouts = {}
    for m in LAYOUT_RE.finditer(dump):
        lines = m.group(1).split('\n')
        head = re.match(r'\s*\d+\s+\|\s+(?:struct|union)\s+(.+)$', lines[0])
        if not head:
            continue
        name = head.group(1).strip()
        fields = []
        for line in lines[1:]:
            fm = re.match(r'\s*(\d+)(?::\d+-\d+)?\s+\|(\s+)(.*)$', line)
            if fm and len(fm.group(2)) == 3:  # depth 1: '|' followed by exactly three spaces
                fields.append((int(fm.group(1)), fm.group(3).strip()))
        layouts[name] = (int(m.group(2)), int(m.group(3)), fields)
    return layouts


def dump(target):
    layouts = {}
    for src in SOURCES:
        cmd = ['clang'] + TARGETS[target] + compile_flags() + [str(ROOT / src)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            sys.exit(f'{target}: {src} failed to compile:\n{res.stderr[-2000:]}')
        layouts.update(parse_layouts(res.stdout))
    return layouts


def main():
    report_all = '--all' in sys.argv
    tags = typedef_tags()
    retail, port = dump('retail'), dump('port')

    def display(name):
        # Anonymous 'typedef struct { ... } Name;' is dumped as '(unnamed struct at <file>:<line>:<col>)'.
        m = re.match(r'\(unnamed (?:struct|union) at (.+?):(\d+):(\d+)\)', name)
        if m:
            return unnamed_typedef(m.group(1), int(m.group(2)), int(m.group(3)))
        tag = name.split()[-1]
        return tags.get(tag, tag)

    bad = 0
    seen = set()
    for name in sorted(set(retail) & set(port), key=display):
        r, p = retail[name], port[name]
        tname = display(name)
        kind = AUDITED.get(tname)
        seen.add(tname)
        if r == p or (not kind and not report_all):
            continue
        if kind == 'retail':
            bad += 1
        print(f'{tname} ({kind or "not audited"}): retail size {r[0]} align {r[1]}, port size {p[0]} align {p[1]}')
        rf, pf = dict((f, o) for o, f in r[2]), dict((f, o) for o, f in p[2])
        for off, field in r[2]:
            if pf.get(field) != off:
                print(f'    {field:60s} retail @{off:<5} port @{pf.get(field)}')
        print()

    missing = sorted(t for t in AUDITED if t not in seen)
    if missing:
        print('Audited types not seen in any dump (add their translation unit to SOURCES):', ', '.join(missing))
    print(f'{bad} retail on-disk type(s) differ.')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
