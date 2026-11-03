"""
Translate the simple dialog routines of the UI module (seg 108E) into C.
Only straight-line code built from known primitives is accepted; anything
else is reported so it can be written by hand.  Text is never copied: the
generated code references strings by their offset in the original data
segment (ds_str(0x....)), which the C port reads from the user's DN1.EXE.
"""
import re, subprocess, sys, json

NAMES = {}
for line in open('src/globals.def'):
    m = re.match(r'GA?\s*\(\s*(\w+),\s*(\w+),\s*(0x[0-9a-f]+)', line)
    if m: NAMES[int(m.group(3), 16)] = m.group(2)

CALLS = {
    'sub_108e_37b6': 'ui_text', 'sub_108e_3d8c': 'ui_box', 'sub_0097_0042': 'play_sound',
    'sub_108e_2870': 'ui_wait_cursor', 'sub_108e_01d5': 'ui_flip', 'sub_17bb_0002': 'pf_delay_ms',
    'sub_108e_0036': 'ui_fade_in', 'sub_108e_01a9': 'ui_fade_out', 'sub_108e_0009': 'ui_fade_grey',
    'sub_108e_3114': 'ui_clear_page0', 'sub_0000_05eb': 'set_draw_page', 'sub_0000_05f7': 'set_display_page',
    'sub_108e_385b': 'ui_char', 'sub_108e_2789': 'draw_number', 'sub_108e_29a2': 'ui_frame',
    'sub_0097_0b85': 'add_score', 'sub_108e_3fe1': 'ui_show_title_text', 'sub_108e_2d58': 'ui_draw_highscore_frame',
}

def gname(tok):
    m = re.fullmatch(r'([gb])([0-9a-f]{4})', tok)
    if not m: return tok
    off = int(m.group(2), 16)
    return NAMES.get(off, 'g_%04x' % off)

def conv(fn):
    src = subprocess.run(['python3', 'tools/lift.py', fn], capture_output=True, text=True).stdout
    out, bad = [], []
    for l in src.split('\n')[1:]:
        s = l.strip()
        if not s: continue
        if re.match(r'^(bx|dx|es|cx|si|di) = ', s) and 'sub_' not in s: continue
        if re.match(r'^ax = [^s]', s): continue
        m = re.match(r'^L([0-9a-f]{4}):$', s)
        if m: out.append('L%s:' % m.group(1)); continue
        if s.startswith('return'): out.append('    return;'); continue
        m = re.match(r'^ax = (sub_\w+)\((.*)\);$', s)
        if m:
            f, args = m.group(1), m.group(2)
            if f not in CALLS: bad.append(s); out.append('    /* TODO */ ' + s); continue
            if f == 'sub_108e_37b6':
                a = [x.strip() for x in args.split(',')]
                if a[0] == 'DS' and re.fullmatch(r'0x[0-9a-f]+', a[1]):
                    out.append('    ui_text(ds_str(%s), %s, %s);' % (a[1], a[2], a[3])); continue
                bad.append(s); out.append('    /* TODO */ ' + s); continue
            args = re.sub(r'\b[gb][0-9a-f]{4}\b', lambda m: gname(m.group(0)), args)
            out.append('    %s(%s);' % (CALLS[f], args)); continue
        m = re.match(r'^([gb][0-9a-f]{4}) = (-?\w+);$', s)
        if m: out.append('    %s = %s;' % (gname(m.group(1)), gname(m.group(2)))); continue
        m = re.match(r'^if \(([gb][0-9a-f]{4}) (==|!=|<|>|<=|>=|<u|>u|<=u|>=u) (-?\w+)\) goto (L\w+);$', s)
        if m:
            op = m.group(2).rstrip('u')
            out.append('    if (%s %s %s) goto %s;' % (gname(m.group(1)), op, m.group(3), m.group(4))); continue
        m = re.match(r'^if \(\(([gb][0-9a-f]{4}) & (0x\w+)\) (==|!=) 0\) goto (L\w+);$', s)
        if m: out.append('    if ((%s & %s) %s 0) goto %s;' % (gname(m.group(1)), m.group(2), m.group(3), m.group(4))); continue
        m = re.match(r'^goto (L\w+);$', s)
        if m: out.append('    goto %s;' % m.group(1)); continue
        bad.append(s); out.append('    /* TODO */ ' + s)
    # spin loops must service the platform: "Lx: if (cond) goto Lx;" -> SPIN
    txt = '\n'.join(out)
    txt = re.sub(r'(L\w+):\n    if \((.*)\) goto \1;', lambda m: '%s:\n    SPIN_WHILE(%s);' % (m.group(1), m.group(2)), txt)
    used = set(re.findall(r'goto (L\w+);', txt))
    txt = '\n'.join(l for l in txt.split('\n') if not (re.fullmatch(r'L\w+:', l) and l[:-1] not in used))
    return txt, bad

if __name__ == '__main__':
    fns = sys.argv[1:]
    for f in fns:
        body, bad = conv(f)
        print('/* %s */%s' % (f.replace('_', ':'), ('  /* MANUAL: %d */' % len(bad)) if bad else ''))
        print(body)
        print()
