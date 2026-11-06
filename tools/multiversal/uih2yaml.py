#!/usr/bin/env python3
"""uih2yaml.py HEADER.h [--library LIB] [--dispatcher NAME:TRAP]: a Universal
Interfaces 3.4 header as multiversal YAML (executor/core/multiversal/defs).

The declarations only: enums, typedefs, structs (an anonymous union member
as a union named <Struct>Union), opaque types, callback types (funptr) and
functions, with the dispatcher and selector of a function whose
inline is `move.w #sel,D0; _Trap` (THREEWORDINLINE(0x303C, sel, trap)) and the
library its Availability comment names. Comments are dropped; nothing gets an
`executor:` key (added by hand as Executor implements each call). The output
is a starting point to edit, not a file to regenerate.
"""
import argparse
import re
import sys


def clean(text):
    text = text.replace('\r\n', '\n').replace('\r', '\n')
    return text


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def ctype(t):
    t = ' '.join(t.split())
    t = re.sub(r'\s*\*', '*', t)
    t = t.replace('* *', '**')
    return t


def value_expr(v):
    v = ' '.join(v.split())
    v = re.sub(r"FOUR_CHAR_CODE\(\s*('[^']{4}')\s*\)", r'\1', v)
    v = re.sub(r'\b(0x[0-9A-Fa-f]+|\d+)[UL]+\b', r'\1', v)
    return v


def yaml_value(v):
    if re.fullmatch(r'-?\d+|0x[0-9A-Fa-f]+', v):
        return v
    return '"' + v.replace('"', '\\"') + '"'


def parse_args(argtext):
    argtext = ' '.join(argtext.split())
    if argtext in ('', 'void'):
        return []
    out = []
    for a in argtext.split(','):
        a = a.strip()
        m = re.match(r'(.*?)(\w+)\s*(\[\s*\w*\s*\])?$', a)
        if not m or not m.group(1).strip():
            out.append({'type': ctype(a)})
            continue
        typ = ctype(m.group(1))
        if m.group(3):
            typ += '*'
        out.append({'name': m.group(2), 'type': typ})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('header')
    ap.add_argument('--library', help='library for functions whose comment names none')
    ap.add_argument('--dispatcher', action='append', default=[],
                    help='NAME:TRAP, e.g. AppearanceDispatch:0xAA74')
    opts = ap.parse_args()
    raw = clean(open(opts.header, encoding='mac_roman').read())
    dispatchers = {int(t, 16): n for n, t in (d.split(':') for d in opts.dispatcher)}

    # Availability comments: "Non-Carbon CFM:   in AppearanceLib 1.0 and later",
    # before each function (from the raw text, comments intact).
    avail = {}
    for m in re.finditer(r'\*\s+(\w+)\(\)\s*\n(?:\s*\*[^\n]*\n)*?\s*\*\s+Non-Carbon CFM:\s+(?:in )?(\w+)', raw):
        avail[m.group(1)] = m.group(2)

    text = strip_comments(raw)
    text = re.sub(r'^\s*#.*$', '', text, flags=re.M)
    out = []

    def emit(kind, lines):
        out.append((kind, lines))

    # A struct's anonymous union member (`union { ... } u;`) becomes a named
    # union, <Struct>Union, emitted before the struct, which then has a
    # member of that type.
    unions = {}

    def name_unions(m):
        sname, body = m.group(1), m.group(2)
        def one(u):
            members = [' '.join(x.split()) for x in u.group(1).split(';') if x.strip()]
            lines = ['- union:', f'    name: {sname}Union', '    members:']
            for mem in members:
                mm = re.match(r'(.*?)(\w+)$', mem)
                lines += [f'      - name: {mm.group(2)}', f'        type: {ctype(mm.group(1))}']
            unions.setdefault(sname, []).append(lines)
            return f'{sname}Union {u.group(2)};'
        body = re.sub(r'\bunion\s*\{([^}]*)\}\s*(\w+)\s*;', one, body)
        return f'struct {sname} {{{body}}};'
    text = re.sub(r'\bstruct\s+(\w+)\s*\{((?:[^{}]|\{[^{}]*\})*)\}\s*;', name_unions, text)

    pos = 0
    item = re.compile(
        r'(?P<enum>\benum\s*\w*\s*\{(?P<ebody>[^}]*)\}\s*;)'
        r'|(?P<struct>\bstruct\s+(?P<sname>\w+)\s*\{(?P<sbody>[^}]*)\}\s*;)'
        r'|(?P<cb>\btypedef\s+CALLBACK_API(?:_C)?\(\s*(?P<cbret>[^,]+?)\s*,\s*(?P<cbname>\w+)\s*\)\s*\((?P<cbargs>[^)]*)\)\s*;)'
        r'|(?P<upp>\btypedef\s+(?:STACK_UPP_TYPE|REGISTER_UPP_TYPE)\(\s*(?P<uppproc>\w+)\s*\)\s*(?P<uppname>\w+)\s*;)'
        r'|(?P<td>\btypedef\s+(?P<tdtype>[^;{}()]+?)\s*(?P<tdname>\w+)\s*;)'
        r'|(?P<fn>\bEXTERN_API(?:_C)?\(\s*(?P<fret>[^)]*?)\s*\)\s*(?P<fname>\w+)\s*\((?P<fargs>[^)]*)\)\s*(?P<finl>\w+INLINE\([^)]*\))?\s*;)',
        re.S)
    callbacks = {}
    for m in item.finditer(text):
        if m.group('enum'):
            vals = []
            last = None
            for e in m.group('ebody').split(','):
                e = e.strip()
                if not e:
                    continue
                if '=' in e:
                    n, v = e.split('=', 1)
                    n, v = n.strip(), value_expr(v)
                else:
                    n = e
                    v = '0' if last is None else f'{last} + 1'
                if n.startswith('upp') and n.endswith('ProcInfo'):
                    last = n
                    continue
                vals.append((n, v))
                last = n
            if vals:
                emit('enum', ['- enum:', '    values:'] +
                     sum(([f'      - name: {n}', f'        value: {yaml_value(v)}'] for n, v in vals), []))
        elif m.group('struct'):
            for u in unions.get(m.group('sname'), []):
                emit('union', u)
            lines = ['- struct:', f'    name: {m.group("sname")}', '    members:']
            for mem in m.group('sbody').split(';'):
                mem = ' '.join(mem.split())
                if not mem:
                    continue
                mm = re.match(r'(.*?)(\w+)\s*(\[\s*(\w+)\s*\])?$', mem)
                typ = ctype(mm.group(1))
                if mm.group(3):
                    typ += f'[{mm.group(4)}]'
                lines += [f'      - name: {mm.group(2)}', f'        type: {typ}']
            emit('struct', lines)
        elif m.group('cb'):
            callbacks[m.group('cbname')] = (ctype(m.group('cbret')), parse_args(m.group('cbargs')))
        elif m.group('upp'):
            ret, args = callbacks.get(m.group('uppproc'), ('void', []))
            lines = ['- funptr:', f'    name: {m.group("uppname")}']
            if ret != 'void':
                lines.append(f'    return: {ret}')
            if args:
                lines.append('    args:')
                for a in args:
                    if 'name' in a:
                        lines += [f'      - name: {a["name"]}', f'        type: {a["type"]}']
                    else:
                        lines.append(f'      - type: {a["type"]}')
            emit('funptr', lines)
        elif m.group('td'):
            typ = ctype(m.group('tdtype'))
            name = m.group('tdname')
            so = re.fullmatch(r'struct (\w+)', typ)
            if so and so.group(1) == name:
                continue  # typedef struct X X: the struct entry names it
            sp = re.fullmatch(r'struct (Opaque\w+)\*', typ)
            if sp:
                emit('typedef', ['- struct:', f'    name: {sp.group(1)}', '', '- typedef:',
                                 f'    name: {name}', f'    type: {sp.group(1)}*'])
                continue
            typ = re.sub(r'^struct ', '', typ)
            emit('typedef', ['- typedef:', f'    name: {name}', f'    type: {typ}'])
        elif m.group('fn'):
            name = m.group('fname')
            if re.match(r'(New|Dispose|Invoke)\w+UPP$', name):
                continue
            if avail.get(name) == 'not':
                continue  # "Non-Carbon CFM: not available": Carbon's only
            ret = ctype(m.group('fret'))
            lines = ['- function:', f'    name: {name}']
            if ret != 'void':
                lines.append(f'    return: {ret}')
            args = parse_args(m.group('fargs'))
            if args:
                lines.append('    args:')
                for a in args:
                    lines += [f'      - name: {a.get("name", "arg")}', f'        type: {a["type"]}']
            inl = m.group('finl') or ''
            words = [int(w, 16) for w in re.findall(r'0x([0-9A-Fa-f]+)', inl)]
            if len(words) == 3 and words[0] == 0x303C and words[2] in dispatchers:
                lines += [f'    dispatcher: {dispatchers[words[2]]}', f'    selector: 0x{words[1]:04X}']
            lib = avail.get(name, opts.library)
            if lib and lib != 'InterfaceLib':
                lines.append(f'    library: {lib}')
            emit('function', lines)

    sys.stdout.write('\n# ####\n\n'.join('\n'.join(l) + '\n' for _, l in out))


if __name__ == '__main__':
    main()
