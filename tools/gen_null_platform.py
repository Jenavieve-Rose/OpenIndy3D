#!/usr/bin/env python3
"""
Generate the null platform (PORT-PLAN.md, Phase 0): one C file per replaced Windows module, implementing every function
its public header declares and defining every variable it declares extern.

Each generated function returns zero / NULL / nothing unless an override below gives it a body. Overrides exist only
where a zero would break the engine's control flow (a loop that waits for -1, a startup check that needs success, a
display environment that must not be NULL). Phases 2 to 5 replace these files with real backends one at a time.

Usage: python3 tools/gen_null_platform.py   (from the repository root; rewrites platform/common/null/*_null.c)
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'platform' / 'common' / 'null'

# (header, output file, extra includes, phase that replaces it)
MODULES = [
    ('Libs/std/Win95/std3D.h',             'std3D_null.c',       ['std/Win95/stdDisplay.h'], 'Phase 2 (Metal renderer)'),
    ('Libs/std/Win95/stdDisplay.h',        'stdDisplay_null.c',  ['std/General/std.h'],      'Phase 2 (SDL3 display)'),
    ('Libs/std/Win95/stdControl.h',        'stdControl_null.c',  [],                         'Phase 3 (SDL3 input)'),
    ('Libs/std/Win95/stdComm.h',           'stdComm_null.c',     [],                         'never: the game has no multiplayer'),
    ('Libs/sound/Driver.h',                'SoundDriver_null.c', ['std/General/stdPlatform.h'], 'Phase 4 (miniaudio)'),
    ('Libs/rdroid/Primitives/rdModel3K.h', 'rdModel3K_null.c',   [],                         'never: the SSE path is dropped'),
]

# Bodies that differ from "return 0". Keyed by function name.
OVERRIDES = {
    # --- std3D: the startup sequence needs a display environment with one display and one 3D device to enumerate.
    'std3D_BuildDisplayEnvironment': '''
    static StdVideoMode     mode;
    static Device3D         device;
    static StdDisplayInfo   info;
    static StdDisplayEnvironment env;

    memset(&mode, 0, sizeof(mode));
    mode.aspectRatio                    = 1.0f;
    mode.rasterInfo.width               = 640;
    mode.rasterInfo.height              = 480;
    mode.rasterInfo.colorInfo.colorMode = STDCOLOR_RGB;
    mode.rasterInfo.colorInfo.bpp       = 32;

    memset(&device, 0, sizeof(device));
    device.caps.bHAL       = 1;
    device.caps.hasZBuffer = 1;
    strcpy(device.deviceName, "Null");
    strcpy(device.deviceDescription, "Null HAL (no renderer until Phase 2)");

    memset(&info, 0, sizeof(info));
    info.displayDevice.bHAL = 1;
    strcpy(info.displayDevice.aDeviceName, "Null display");
    strcpy(info.displayDevice.aDriverName, "null");
    info.numModes   = 1;
    info.aModes     = &mode;
    info.numDevices = 1;
    info.aDevices   = &device;

    env.numInfos      = 1;
    env.aDisplayInfos = &info;
    return &env;''',
    # Textures are converted to the backend's format at load time, so even the null renderer must name one. 32 bpp
    # BGRA (D3D's A8R8G8B8 / X8R8G8B8), the layout the Phase 2 Metal backend uploads. No colour key.
    'std3D_GetTextureFormat': '''
    memset(pDest, 0, sizeof(ColorInfo));
    pDest->colorMode     = STDCOLOR_RGBA;
    pDest->bpp           = 32;
    pDest->redBPP        = 8;
    pDest->greenBPP      = 8;
    pDest->blueBPP       = 8;
    pDest->redPosShift   = 16;
    pDest->greenPosShift = 8;
    pDest->bluePosShift  = 0;
    if ( type != STDCOLOR_FORMAT_RGB )
    {
        pDest->alphaBPP      = 8;
        pDest->alphaPosShift = 24;
    }
    else
    {
        pDest->colorMode = STDCOLOR_RGB;
    }
    *pbColorKeySet = 0;
    *pColorKey     = 0;''',
    # Pure function of the colour info; the retail body (std3D.c).
    'std3D_GetColorFormat': '''
    switch ( pCi->alphaBPP )
    {
        case 0:
            return STDCOLOR_FORMAT_RGB;

        case 1:
            return STDCOLOR_FORMAT_RGBA_1BIT_ALPHA;

        default:
            return STDCOLOR_FORMAT_RGBA;
    }''',
    'std3D_FreeDisplayEnvironment': '''
    (void)pEnv; // static storage, see std3D_BuildDisplayEnvironment''',

    # --- stdDisplay: startup succeeds, opening a device fails. This is the Phase 0 stopping point.
    'stdDisplay_Startup': '''
    return 1;''',
    'stdDisplay_Open': '''
    (void)deviceNum;
    if ( std_pHS && std_pHS->pErrorPrint )
    {
        std_pHS->pErrorPrint("null platform: stdDisplay_Open: no display backend yet (PORT-PLAN.md Phase 2)\\n");
    }
    return 0;''',
    'stdDisplay_GetBackBufferSize': '''
    if ( pWidth )  *pWidth  = 0;
    if ( pHeight ) *pHeight = 0;''',

    # --- stdComm: -1 is "no more messages"; sithComm loops until it sees it.
    'stdComm_Receive': '''
    (void)pSender; (void)pData; (void)pLength;
    return -1;''',
    'stdComm_GetPlayerID': '''
    (void)playerNum;
    return DPID_UNKNOWN;''',
    'stdComm_CreatePlayer': '''
    (void)pPlayerName;
    return DPID_UNKNOWN;''',
    'stdComm_CreateGame': '''
    (void)pSettings;
    return E_NOTIMPL;''',
    'stdComm_SetGameParams': '''
    (void)pSettings;
    return E_NOTIMPL;''',

    # --- SoundDriver: opening fails, so Sound runs silent; time still has to advance.
    'SoundDriver_GetTimeMsec': '''
    return timeGetTime();''',
    'SoundDriver_Sleep': '''
    Sleep(dwMilliseconds);''',
}

# Functions with a real implementation elsewhere in platform/; the generator leaves them out of the null file.
IMPLEMENTED_ELSEWHERE = {
    # platform/common/stdDisplay_vbuffer.c: system-memory VBuffers, needed to load materials without a display
    'stdDisplay_VBufferNew', 'stdDisplay_VBufferFree', 'stdDisplay_VBufferLock', 'stdDisplay_VBufferUnlock',
    'stdDisplay_VBufferSetColorKey', 'stdDisplay_VBufferConvertColorFormat',
}

# Prototypes, with or without the J3DAPI marker (rdModel3K.h omits it). typedefs and macros never match: they start
# with a keyword the return-type group rejects, or with '#'.
PROTO_RE = re.compile(r'^(?!\s*(?:typedef|return|#|extern))\s*(?P<ret>[A-Za-z_][\w\s\*]*?[\s\*])(?:J3DAPI\s+)?(?P<name>[A-Za-z_]\w*)\s*\((?P<args>[^;()]*)\)\s*;')
EXTERN_RE = re.compile(r'^\s*extern\s+(?P<decl>[^;]+);')


def zero_return(ret):
    ret = ' '.join(ret.split())
    if ret == 'void':
        return None
    if '*' in ret or ret.startswith('LP') or ret in ('HDC', 'HWND', 'HANDLE', 'HINSTANCE'):
        return 'NULL'
    if ret == 'float':
        return '0.0f'
    if ret == 'HRESULT':
        return 'E_NOTIMPL'
    return '(%s)0' % ret


def generate(header, out_name, extra_includes, phase):
    text = (ROOT / header).read_text(encoding='latin-1')
    include = header[len('Libs/'):]
    protos, externs = [], []
    for line in text.splitlines():
        m = PROTO_RE.match(line)
        if m:
            ret = ' '.join(m.group('ret').replace('J3DAPI', ' ').split())
            protos.append((ret, m.group('name'), m.group('args').strip()))
            continue
        m = EXTERN_RE.match(line)
        if m and '(' not in m.group('decl'):
            externs.append(m.group('decl').strip())

    lines = [
        '/*',
        ' * GENERATED by tools/gen_null_platform.py from %s. Do not edit; change the generator\'s overrides.' % header,
        ' *',
        ' * Null implementation of %s (PORT-PLAN.md, Phase 0). Replaced by: %s.' % (pathlib.Path(header).name, phase),
        ' */',
        '#include <string.h>',
        '#include <%s>' % include,
    ]
    lines += ['#include <%s>' % inc for inc in extra_includes]
    lines.append('')
    for decl in externs:
        lines.append('%s;' % decl)
    if externs:
        lines.append('')

    for ret, name, args in protos:
        if name in IMPLEMENTED_ELSEWHERE:
            continue
        lines.append('%s J3DAPI %s(%s)' % (ret, name, args))
        lines.append('{')
        if name in OVERRIDES:
            lines.extend(OVERRIDES[name].strip('\n').split('\n'))
        else:
            value = zero_return(ret)
            if value is not None:
                lines.append('    return %s;' % value)
        lines.append('}')
        lines.append('')

    (OUT / out_name).write_text('\n'.join(lines).rstrip('\n') + '\n')
    return len(protos), len(externs)


# ---------------------------------------------------------------------------------------------------------------------
# win32_null.c: every WINAPI function the shim headers declare and platform/common/crt_posix.c does not implement.
# The shim's prototypes have unnamed parameters, so names p0..pN are synthesised.
# ---------------------------------------------------------------------------------------------------------------------
SHIM_HEADERS = ['platform/apple/shim/win32_shim.h', 'platform/apple/shim/win32_shim_gui.h']
REAL_IMPL = 'platform/common/crt_posix.c'

WIN32_OVERRIDES = {
    # The game's exit path is PostMessage(WM_CLOSE); wkernel_null.c plays out the rest (see null_platform.h).
    'PostMessageA':     '    (void)p0; (void)p2; (void)p3;\n    null_PostMessage(p1);\n    return TRUE;',
    'PostQuitMessage':  '    (void)p0;\n    null_RequestQuit();',
    'MessageBoxA':      '    (void)p0;\n    fprintf(stderr, "[MessageBox] %s: %s\\n", p2 ? p2 : "", p1 ? p1 : "");\n    return (p3 & 0xF) == MB_YESNO || (p3 & 0xF) == MB_YESNOCANCEL ? IDYES : IDOK;',
    'DialogBoxParamA':  '    (void)p0; (void)p2; (void)p3; (void)p4;\n'
                        '    fprintf(stderr, "null platform: DialogBoxParam(%u): no dialog system until PORT-PLAN.md Phase 6\\n", (unsigned)(uintptr_t)p1);\n'
                        '    return -1;',
    # A single-instance mutex that always succeeds: GetLastError() stays ERROR_SUCCESS, so WinMain proceeds.
    'CreateMutexA':     '    (void)p0; (void)p1; (void)p2;\n    static int mutex;\n    return &mutex;',
    'CloseHandle':      '    (void)p0;\n    return TRUE;',
    'ReleaseMutex':     '    (void)p0;\n    return TRUE;',
    'CreateFileA':      '    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5; (void)p6;\n    return INVALID_HANDLE_VALUE;',
    'FindFirstFileA':   None,  # real, crt_posix.c
    'LocalAlloc':       '    return (p0 & LMEM_ZEROINIT) ? calloc(1, p1) : malloc(p1);',
    'LocalFree':        '    free(p0);\n    return NULL;',
    'GlobalAlloc':      '    return (p0 & GMEM_ZEROINIT) ? calloc(1, p1) : malloc(p1);',
    'GlobalFree':       '    free(p0);\n    return NULL;',
    'GlobalLock':       '    return p0;',
    'GlobalUnlock':     '    (void)p0;\n    return TRUE;',
    'GetDesktopWindow': '    return NULL;',
    'IsWindow':         '    (void)p0;\n    return FALSE;',
}

WINAPI_RE = re.compile(r'^\s*(?P<ret>[A-Za-z_][\w\*]*(?:\s+[\w\*]+)*?)\s+WINAPI\s+(?P<name>\w+)\s*\((?P<args>[^;]*)\)\s*;')


def split_params(args):
    args = args.strip()
    if not args or args == 'void':
        return []
    return [a.strip() for a in args.split(',')]


def generate_win32():
    implemented = set(re.findall(r'\bWINAPI\s+(\w+)\s*\(', (ROOT / REAL_IMPL).read_text()))
    lines = [
        '/*',
        ' * GENERATED by tools/gen_null_platform.py from %s. Do not edit; change the generator\'s' % ', '.join(SHIM_HEADERS),
        ' * WIN32_OVERRIDES.',
        ' *',
        ' * Null Win32: every function the shim declares that %s does not implement for real. Windows,' % REAL_IMPL,
        ' * dialogs, GDI and common controls do nothing and report failure; the dialog shell code that calls them is',
        ' * replaced by the j3dgui emulation in PORT-PLAN.md Phase 6, and this file shrinks as each module is ported.',
        ' */',
        '#include <stdint.h>',
        '#include <stdio.h>',
        '#include <stdlib.h>',
        '',
        '#include <Windows.h>',
        '#include <CommCtrl.h>',
        '',
        '#include "null_platform.h"',
        '',
    ]
    count = 0
    seen = set()
    for header in SHIM_HEADERS:
        for line in (ROOT / header).read_text().splitlines():
            m = WINAPI_RE.match(line)
            if not m:
                continue
            ret, name, params = ' '.join(m.group('ret').split()), m.group('name'), split_params(m.group('args'))
            if name in implemented or name in seen:
                continue
            seen.add(name)
            named = ', '.join('%s p%d' % (p, i) for i, p in enumerate(params)) or 'void'
            lines.append('%s WINAPI %s(%s)' % (ret, name, named))
            lines.append('{')
            body = WIN32_OVERRIDES.get(name)
            if body:
                lines.extend(body.split('\n'))
            else:
                if params:
                    lines.append('    ' + ' '.join('(void)p%d;' % i for i in range(len(params))))
                value = zero_return(ret)
                if value is not None:
                    lines.append('    return %s;' % value)
            lines.append('}')
            lines.append('')
            count += 1

    # The message-queue state shared with wkernel_null.c.
    lines += [
        '/* ---- null_platform.h --------------------------------------------------------------------------------------- */',
        'static int null_bClosePending = 0;',
        'static int null_bQuit = 0;',
        '',
        'void null_PostMessage(unsigned int msg)',
        '{',
        '    if ( msg == WM_CLOSE )',
        '    {',
        '        null_bClosePending = 1;',
        '    }',
        '    else if ( msg == WM_QUIT )',
        '    {',
        '        null_bQuit = 1;',
        '    }',
        '}',
        '',
        'int null_TakeCloseRequest(void)',
        '{',
        '    int bPending = null_bClosePending;',
        '    null_bClosePending = 0;',
        '    return bPending;',
        '}',
        '',
        'void null_RequestQuit(void)',
        '{',
        '    null_bQuit = 1;',
        '}',
        '',
        'int null_IsQuitRequested(void)',
        '{',
        '    return null_bQuit;',
        '}',
        '',
        '_Noreturn void null_Unreachable(const char* pFunction)',
        '{',
        '    fprintf(stderr, "null platform: %s reached with no backend; a startup check is missing\\n", pFunction);',
        '    abort();',
        '}',
    ]
    (OUT / 'win32_null.c').write_text('\n'.join(lines) + '\n')
    stale = {k for k, v in WIN32_OVERRIDES.items() if v and k not in seen}
    return count, stale


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    unused = set(OVERRIDES)
    for header, out_name, extra, phase in MODULES:
        nfuncs, nvars = generate(header, out_name, extra, phase)
        text = (OUT / out_name).read_text()
        unused -= {name for name in OVERRIDES if (' %s(' % name) in text}
        print('%-22s %3d functions, %2d variables  <- %s' % (out_name, nfuncs, nvars, header))
    nwin32, stale = generate_win32()
    print('%-22s %3d functions               <- %s' % ('win32_null.c', nwin32, ', '.join(SHIM_HEADERS)))
    unused |= stale
    if unused:
        sys.exit('overrides that match no prototype: %s' % ', '.join(sorted(unused)))


if __name__ == '__main__':
    main()
