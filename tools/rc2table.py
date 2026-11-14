#!/usr/bin/env python3
"""Generate platform/common/j3dgui/gui_templates.c from Jones3D/Indy3D.rc (PORT-PLAN.md P6.2).

The port has no resource compiler: the dialog templates, their DLGINIT data and the icon table become C tables that
platform/common/j3dgui/gui_dialog.c builds dialogs from, as DialogBoxParam built them from the .rsrc section.

Each control keyword gets the style the Windows resource compiler gives it (RC.EXE's documented defaults), plus
WS_CHILD | WS_VISIBLE; `NOT X` in a style clears X. Style names are written as they appear, so the C compiler resolves
them against the shim headers (platform/apple/shim), the same values the game's code sees.

Usage: tools/rc2table.py [Jones3D/Indy3D.rc] [platform/common/j3dgui/gui_templates.c]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# keyword: (window class, default style, takes text)
KEYWORDS = {
    'DEFPUSHBUTTON': ('Button', 'BS_DEFPUSHBUTTON | WS_TABSTOP', True),
    'PUSHBUTTON':    ('Button', 'BS_PUSHBUTTON | WS_TABSTOP', True),
    'GROUPBOX':      ('Button', 'BS_GROUPBOX', True),
    'LTEXT':         ('Static', 'SS_LEFT | WS_GROUP', True),
    'CTEXT':         ('Static', 'SS_CENTER | WS_GROUP', True),
    'RTEXT':         ('Static', 'SS_RIGHT | WS_GROUP', True),
    'ICON':          ('Static', 'SS_ICON', True),     # the text is the icon's resource ID
    'COMBOBOX':      ('ComboBox', 'CBS_SIMPLE | WS_TABSTOP', False),
    'LISTBOX':       ('ListBox', 'LBS_NOTIFY | WS_BORDER', False),
    'EDITTEXT':      ('Edit', 'ES_LEFT | WS_BORDER | WS_TABSTOP', False),
    'SCROLLBAR':     ('ScrollBar', 'SBS_HORZ', False),
}

TOKEN_RE = re.compile(r'"(?:[^"]|"")*"|[^\s,]+')


def c_string(text):
    out = []
    for ch in text:
        if ch in '\\"':
            out.append('\\' + ch)
        elif ord(ch) < 32 or ord(ch) > 126:
            out.append('\\x%02x""' % ord(ch))
        else:
            out.append(ch)
    return '"' + ''.join(out) + '"'


def unquote(token):
    return token[1:-1].replace('""', '"') if token.startswith('"') else token


def style_expr(parts, base=None):
    """`A | B | NOT C` -> C expression: (base | A | B) & ~(C). NOT applies after everything is or'ed, the base too."""
    on, off = [], []
    tokens = ' '.join(parts).replace('|', ' | ').split()
    negate = False
    for tok in tokens:
        if tok == '|':
            continue
        if tok == 'NOT':
            negate = True
            continue
        (off if negate else on).append(tok)
        negate = False
    if base:
        on = base + on
    expr = ' | '.join(on) if on else '0'
    if off:
        expr = '(%s) & ~(%s)' % (expr, ' | '.join(off))
    return expr


def split_args(rest):
    """Comma-separated arguments; a style argument may hold `A | NOT B`."""
    args, cur, quoted = [], '', False
    for ch in rest:
        if ch == '"':
            quoted = not quoted
        if ch == ',' and not quoted:
            args.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        args.append(cur.strip())
    return args


def parse(rc_text):
    lines = [l.split('//')[0].rstrip() for l in rc_text.splitlines()]
    dialogs, inits, icons = [], [], []
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        m = re.match(r'^(\d+)\s+DIALOG\s+(-?\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*,\s*(\d+)$', line)
        if m:
            dlg = {'id': int(m.group(1)), 'rect': [int(m.group(k)) for k in range(2, 6)], 'style': '0', 'caption': '',
                   'font': (8, 'MS Sans Serif'), 'items': []}
            i += 1
            while lines[i].strip() != 'BEGIN':
                head = lines[i].strip()
                if head.startswith('STYLE'):
                    dlg['style'] = style_expr([head[5:]])
                elif head.startswith('CAPTION'):
                    dlg['caption'] = unquote(head[7:].strip())
                elif head.startswith('FONT'):
                    size, name = head[4:].split(',', 1)
                    dlg['font'] = (int(size), unquote(name.strip()))
                i += 1
            i += 1
            while lines[i].strip() != 'END':
                item = lines[i].strip()
                i += 1
                if not item:
                    continue
                keyword, rest = item.split(None, 1)
                args = split_args(rest)
                if keyword == 'CONTROL':
                    text, ident, cls, style = unquote(args[0]), args[1], unquote(args[2]), args[3]
                    x, y, cx, cy = args[4:8]
                    ex = args[8] if len(args) > 8 else '0'
                    style = style_expr([style], ['WS_CHILD', 'WS_VISIBLE'])
                    icon = 0
                else:
                    cls, default, has_text = KEYWORDS[keyword]
                    if has_text:
                        text, args = unquote(args[0]), args[1:]
                    else:
                        text = ''
                    ident, x, y, cx, cy = args[0:5]
                    extra = args[5] if len(args) > 5 else ''
                    ex = args[6] if len(args) > 6 else '0'
                    style = style_expr([default] + (['|', extra] if extra else []), ['WS_CHILD', 'WS_VISIBLE'])
                    icon = 0
                    if keyword == 'ICON':
                        icon, text = int(text), ''
                dlg['items'].append({'class': cls, 'text': text, 'id': int(ident), 'style': style, 'ex': ex,
                                     'rect': [int(x), int(y), int(cx), int(cy)], 'icon': icon, 'keyword': keyword})
            i += 1
            dialogs.append(dlg)
            continue
        m = re.match(r'^(\d+)\s+DLGINIT$', line)
        if m:
            dlg_id = int(m.group(1))
            i += 2  # BEGIN
            while lines[i].strip() != 'END':
                head = [t for t in TOKEN_RE.findall(lines[i]) if t]
                i += 1
                if head == ['0']:
                    continue
                ctrl, msg = int(head[0]), int(head[1], 0)
                data = unquote(lines[i].strip())
                i += 1
                inits.append((dlg_id, ctrl, msg, data.replace('\\000', '')))
            i += 1
            continue
        m = re.match(r'^(\d+)\s+ICON\s+"([^"]+)"$', line)
        if m:
            icons.append((int(m.group(1)), m.group(2)))
        i += 1
    return dialogs, inits, icons


def emit(dialogs, inits, icons, rc_name):
    out = [
        '/*',
        ' * GENERATED by tools/rc2table.py from %s. Do not edit; change the .rc or the generator.' % rc_name,
        ' *',
        ' * The dialog templates, their DLGINIT data and the icon table, as C (PORT-PLAN.md P6.2). Coordinates are dialog',
        ' * units; styles include what the resource compiler adds for each control keyword and WS_CHILD | WS_VISIBLE.',
        ' */',
        '#include <Windows.h>',
        '#include <CommCtrl.h>',
        '',
        '#include "gui_templates.h"',
        '',
    ]
    for dlg in dialogs:
        out.append('static const J3DGuiTemplateItem gui_aItems%d[] = {' % dlg['id'])
        for it in dlg['items']:
            x, y, cx, cy = it['rect']
            out.append('    { %s, %s, %d, %s, %s, %d, %d, %d, %d, %d }, // %s' % (
                c_string(it['class']), c_string(it['text']), it['id'], '(DWORD)(' + it['style'] + ')',
                '(DWORD)(' + it['ex'] + ')', x, y, cx, cy, it['icon'], it['keyword']))
        out.append('};')
        out.append('')
    out.append('const J3DGuiTemplate J3DGui_aTemplates[] = {')
    for dlg in dialogs:
        x, y, cx, cy = dlg['rect']
        out.append('    { %d, (DWORD)(%s), %d, %d, %d, %d, %s, %s, %d, gui_aItems%d, %d },' % (
            dlg['id'], dlg['style'], x, y, cx, cy, c_string(dlg['caption']), c_string(dlg['font'][1]), dlg['font'][0],
            dlg['id'], len(dlg['items'])))
    out.append('};')
    out.append('const int J3DGui_numTemplates = %d;' % len(dialogs))
    out.append('')
    out.append('const J3DGuiTemplateInit J3DGui_aTemplateInits[] = {')
    for dlg_id, ctrl, msg, data in inits:
        out.append('    { %d, %d, 0x%X, %s },' % (dlg_id, ctrl, msg, c_string(data)))
    out.append('};')
    out.append('const int J3DGui_numTemplateInits = %d;' % len(inits))
    out.append('')
    out.append('// The icon resources: ID and the file the retail .rc named (the images are read from the player\'s INDY3D.EXE).')
    out.append('const J3DGuiIconResource J3DGui_aIconResources[] = {')
    for icon_id, name in icons:
        out.append('    { %d, %s },' % (icon_id, c_string(name)))
    out.append('};')
    out.append('const int J3DGui_numIconResources = %d;' % len(icons))
    return '\n'.join(out) + '\n'


def main():
    rc = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'Jones3D/Indy3D.rc'
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / 'platform/common/j3dgui/gui_templates.c'
    dialogs, inits, icons = parse(rc.read_text(encoding='latin-1'))
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(emit(dialogs, inits, icons, rc.relative_to(ROOT) if rc.is_absolute() and ROOT in rc.parents else rc))
    print('%s: %d dialogs, %d controls, %d DLGINIT entries, %d icons' % (
        dst, len(dialogs), sum(len(d['items']) for d in dialogs), len(inits), len(icons)))


if __name__ == '__main__':
    main()
