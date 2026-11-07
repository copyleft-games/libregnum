#!/usr/bin/env python3
# gen.py: turns spec.py into the lrgeditorctl verb tables, MCP tool rows,
# IPC/CLI docs and skill reference text, and splices them into the tree
# between "m67e-generated" markers.
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spec  # noqa: E402

ROOT = '/var/home/zach/source/projects/libregnum/trees/v0.9.0'

TYPES = {
    's': '{"type":"string"}',
    'i': '{"type":"integer"}',
    'n': '{"type":"number"}',
    'b': '{"type":"boolean"}',
    'o': '{"type":"object"}',
    'a': '{"type":"array"}',
    'sa': '{"type":"array","items":{"type":"string"}}',
    'na': '{"type":"array","items":{"type":"number"}}',
    'v': '{}',
}


def parse_params(text):
    out = []
    for tok in text.split():
        name, typ = tok.split(':')
        req = typ.endswith('!')
        typ = typ.rstrip('!')
        assert typ in TYPES, tok
        out.append((name, typ, req))
    return out


def family_of(method):
    return method.split('.')[0]


def verb_of(f, mm):
    if mm['verb']:
        return mm['verb']
    rest = mm['method'][len(family_of(mm['method'])) + 1:]
    return rest.replace('.', '-').replace('_', '-')


def tool_of(mm):
    return mm['tool'] or mm['method'].replace('.', '_')


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


# ---------------------------------------------------------------- MCP rows
def mcp_rows():
    out = []
    for f in spec.FAMILIES:
        out.append('\t/* 0.9.0 %s %s: %s */' % (f.wave, f.name, f.title.lower()))
        for mm in f.methods:
            params = parse_params(mm['params'])
            props = []
            for (name, typ, req) in params:
                if name == 'node' and typ == 's':
                    props.append('NODE_ARG')
                elif name == 'path' and typ == 's':
                    props.append('MAT_PATH_ARG')
                else:
                    props.append(cstr('"%s":%s' % (name, TYPES[typ])))
            prop_c = ' ","\n\t  '.join(props) if props else '""'
            # join: each element is a C string literal or macro; separate with ","
            if props:
                pieces = []
                for i, p in enumerate(props):
                    if i > 0:
                        pieces.append('","')
                    pieces.append(p)
                prop_c = ' '.join(pieces)
            req = [n for (n, t, r) in params if r]
            req_c = cstr(json.dumps(req, separators=(',', ':'))) if req else 'NULL'
            out.append('\t{ %s, %s, TOOL_PASS, %s, %s,\n\t  %s,\n\t  %s, %s },' % (
                cstr(tool_of(mm)), cstr(mm['method']),
                'TRUE' if mm['ro'] else 'FALSE', 'TRUE' if mm['de'] else 'FALSE',
                cstr(mm['desc']), prop_c, req_c))
    return '\n'.join(out) + '\n'


# ---------------------------------------------------------------- CLI tables
def ctl_tables():
    out = []
    for f in spec.FAMILIES:
        if f.name == 'assetnew':
            continue
        out.append('/* %s %s */' % (f.cli, '|'.join(verb_of(f, mm) for mm in f.methods)))
        out.append('static const M67Verb m67_%s_verbs[] = {' % f.name)
        for mm in f.methods:
            keys = mm['cli'].split()
            params = parse_params(mm['params'])
            names = [n for (n, t, r) in params]
            wrap = 'NULL'
            top = []
            if mm['method'] == 'nav.set':
                wrap = '"values"'
                top = ['key', 'value', 'values']
            elif mm['method'] == 'audio.set':
                wrap = '"params"'
                top = ['node', 'params']
            node = 'TRUE' if 'node' in names else 'FALSE'
            kc = ', '.join([cstr(k) for k in keys] + ['NULL'])
            tc = ', '.join([cstr(k) for k in top] + ['NULL'])
            out.append('\t{ %s, %s, { %s }, %s, { %s }, %s },' % (
                cstr(verb_of(f, mm)), cstr(mm['method']), kc, wrap, tc, node))
        out.append('};')
        out.append('')
    # the family table
    out.append('/* the M6/M7/M8 families: lrgeditorctl FAMILY VERB ... */')
    out.append('static const struct')
    out.append('{')
    out.append('\tconst gchar   *family;')
    out.append('\tconst M67Verb *verbs;')
    out.append('\tgsize          n_verbs;')
    out.append('} m67_families[] = {')
    for f in spec.FAMILIES:
        if f.name == 'assetnew':
            continue
        out.append('\t{ %s, m67_%s_verbs, G_N_ELEMENTS (m67_%s_verbs) },' % (cstr(f.cli), f.name, f.name))
    out.append('};')
    return '\n'.join(out) + '\n'


def usage_lines():
    out = []
    for f in spec.FAMILIES:
        if f.name == 'assetnew':
            continue
        verbs = f.hybrid + [verb_of(f, mm) for mm in f.methods]
        line = '  %s ' % f.cli
        cur = line
        lines = []
        for i, v in enumerate(verbs):
            piece = v + ('|' if i < len(verbs) - 1 else '')
            if len(cur) + len(piece) > 100:
                lines.append(cur)
                cur = ' ' * (len(f.cli) + 3)
            cur += piece
        cur += ' ... [key=value...]'
        lines.append(cur)
        lines.append(' ' * 35 + f.title.replace(' methods', '').lower() + ' (' + f.reg + ')')
        out.extend(lines)
    return out


def completion_lines():
    out = []
    for f in spec.FAMILIES:
        if f.name == 'assetnew':
            continue
        verbs = ' '.join(f.hybrid + [verb_of(f, mm) for mm in f.methods])
        out.append("\\t\\t%s) mapfile -t COMPREPLY < <(compgen -W '%s' -- \\\"${cur}\\\") ;;\\n" % (f.cli, verbs))
    return out


# ---------------------------------------------------------------- docs
def org_params(mm):
    ps = parse_params(mm['params'])
    if not ps:
        return ''
    return ', '.join('=%s%s=' % (n, '' if r else '?') for (n, t, r) in ps)


def org_esc(s):
    return s.replace('|', '\\vert{}')


def ipc_index_rows():
    out = []
    for f in spec.FAMILIES:
        for mm in f.methods:
            out.append('| =%s= | 0.9.0 | %s | [[#%s][%s]] |' % (mm['method'], f.reg, f.sec,
                                                            f.title if f.name != 'assetnew' else 'Asset file'))
    return '\n'.join(out) + '\n'


def ipc_sections():
    out = []
    for f in spec.FAMILIES:
        if f.name == 'assetnew':
            continue
        out.append('** %s' % f.title)
        out.append(':PROPERTIES:')
        out.append(':CUSTOM_ID: %s' % f.sec)
        out.append(':END:')
        out.append(f.intro)
        out.append('')
        out.append('| Method | Params | Result |')
        out.append('|--------+--------+--------|')
        for mm in f.methods:
            res = mm['result']
            if not res.startswith('=') and not res.startswith('as ') and not res.startswith('the '):
                res = '=' + res + '='
            out.append('| =%s= | %s | %s |' % (mm['method'], org_esc(org_params(mm)), org_esc(res)))
        out.append('')
    return '\n'.join(out)


def ctl_doc():
    out = []
    for f in spec.FAMILIES:
        if f.name == 'assetnew':
            continue
        out.append('**** %s' % f.cli)
        out.append(':PROPERTIES:')
        out.append(':CUSTOM_ID: verbs-%s' % f.cli)
        out.append(':END:')
        out.append(f.cli_intro + ' Methods: [[file:ipc-protocol.org::#%s][%s]].' % (f.sec, f.title))
        out.append('')
        out.append('| Command | Method |')
        out.append('|---------+--------|')
        for mm in f.methods:
            keys = []
            for k in mm['cli'].split():
                keys.append(k.lstrip('@~').upper())
            cmd = '%s %s%s [key=value...]' % (f.cli, verb_of(f, mm), (' ' + ' '.join(keys)) if keys else '')
            out.append('| =%s= | =%s= |' % (org_esc(cmd), mm['method']))
        out.append('')
        if f.examples:
            out.append('#+begin_src sh')
            out.extend(f.examples)
            out.append('#+end_src')
            out.append('')
    return '\n'.join(out)


def mcp_doc_sentence():
    waves = []
    for f in spec.FAMILIES:
        if f.wave not in waves:
            waves.append(f.wave)
    return ' '.join(mcp_doc_wave(w) for w in waves)


def mcp_doc_wave(wave):
    parts = []
    for f in [f for f in spec.FAMILIES if f.wave == wave]:
        names = []
        for mm in f.methods:
            n = '=%s=' % tool_of(mm)
            if mm['ro']:
                n += ' (read-only)'
            elif mm['de']:
                n += ' (destructive)'
            names.append(n)
        parts.append('the %s tools %s' % (f.title.replace(' methods', '').lower(), ', '.join(names)))
    return 'Added in 0.9.0 %s (one per IPC method, arguments passed through): ' % wave + '; '.join(parts) + '.'


# ---------------------------------------------------------------- skill md
M8_FAMS = set(f.name for f in spec.FAMILIES if f.wave == 'M8')


def skill_ipc():
    out = []
    for f in spec.FAMILIES:
        if f.name == 'assetnew':
            continue
        items = []
        for mm in f.methods:
            ps = parse_params(mm['params'])
            pp = ', '.join('%s%s' % (n, '' if r else '?') for (n, t, r) in ps)
            short = mm['method'][len(family_of(mm['method'])) + 1:]
            res = mm['result']
            res = res.strip('=') if (res.startswith('={') or (res.startswith('=[') and family_of(mm['method']) in M8_FAMS)) else ''
            items.append('%s{%s}%s' % (short, pp, (' -> ' + res) if res else ''))
        out.append('%s.* (%s): %s.' % (f.name, re.sub(r'=([^=]+)=', r'`\1`', f.intro.split('. ')[0]), '; '.join(items)))
    return out


def skill_ctl():
    out = []
    for f in spec.FAMILIES:
        if f.name == 'assetnew':
            continue
        vs = []
        for mm in f.methods:
            keys = ' '.join(k.lstrip('@~').upper() for k in mm['cli'].split())
            vs.append(verb_of(f, mm) + ((' ' + keys) if keys else ''))
        out.append('- `%s %s` [key=value..]' % (f.cli, ' | '.join(vs)))
    return out


def skill_mcp():
    out = []
    for f in spec.FAMILIES:
        names = []
        for mm in f.methods:
            n = tool_of(mm)
            if mm['ro']:
                n += ' (ro)'
            elif mm['de']:
                n += ' (destructive)'
            names.append(n)
        out.append('%s: %s.' % (f.title.replace(' methods', ''), ', '.join(names)))
    return out


def splice(path, begin, end, text):
    with open(path) as fh:
        s = fh.read()
    i = s.index(begin) + len(begin)
    j = s.index(end, i)
    s = s[:i] + '\n' + text + s[j:]
    with open(path, 'w') as fh:
        fh.write(s)


def index_rows(path, rows):
    """Puts the method index rows after physasset.save's (idempotent),
    and asset.new into the asset file methods table."""
    methods = set()
    for f in spec.FAMILIES:
        for mm in f.methods:
            methods.add(mm['method'])
    with open(path) as fh:
        lines = fh.read().split('\n')
    keep = []
    for ln in lines:
        mo = re.match(r'^\| =([a-z0-9_.]+)= \| ', ln)
        if mo and mo.group(1) in methods:
            continue
        keep.append(ln)
    out = []
    for ln in keep:
        out.append(ln)
        if ln.startswith('| =physasset.save= | 0.9.0 |'):
            out.extend(rows.rstrip('\n').split('\n'))
        if ln.startswith('| =asset.orphans=    |'):
            out.append('| =asset.new=       | =kind=, =path?=, =name?=, =image?= | ={path, relative, is_dir, guid, kind}=; writes a new file: =kind= a document format nick (=bt fsm particles weather mixer tileset sprite water nav-settings material= ...) or =terrain= (a flat 4x4-chunk =.rterrain= with sidecars); without =path= a free =<name>.<ext>= in the panel\'s folder; =sprite= with =image= auto-slices that image | an unknown kind, a taken path or a path outside the project is =invalid-params=; a missing image =not-found= |')
    with open(path, 'w') as fh:
        fh.write('\n'.join(out))


def top_words(path):
    """The top-level completion words: every generated family after
    "physics physasset" (idempotent; hybrid families are already listed)."""
    with open(path) as fh:
        s = fh.read()
    fams = []
    head = s[s.index("compgen -W 'sessions "):s.index(' physics physasset')].split()
    for f in spec.FAMILIES:
        if f.name == 'assetnew' or f.cli in head or f.cli in fams:
            continue
        fams.append(f.cli)
    s2 = re.sub(r"(physics physasset)[a-z0-9 ]*(' -- )", lambda mo: mo.group(1) + ' ' + ' '.join(fams) + mo.group(2), s, count=1)
    assert s2 != s or (' '.join(fams) + "' -- ") in s
    with open(path, 'w') as fh:
        fh.write(s2)


def main():
    out_dir = os.path.join(HERE, 'out')
    os.makedirs(out_dir, exist_ok=True)
    files = {
        'mcp_rows.c': mcp_rows(),
        'ctl_tables.c': ctl_tables(),
        'usage.txt': '\n'.join(usage_lines()) + '\n',
        'completion.txt': '\n'.join(completion_lines()) + '\n',
        'ipc_index.org': ipc_index_rows(),
        'ipc_sections.org': ipc_sections(),
        'ctl_doc.org': ctl_doc(),
        'mcp_doc.txt': mcp_doc_sentence() + '\n',
        'skill_ipc.md': '\n'.join(skill_ipc()) + '\n',
        'skill_ctl.md': '\n'.join(skill_ctl()) + '\n',
        'skill_mcp.md': '\n'.join(skill_mcp()) + '\n',
    }
    for k, v in files.items():
        with open(os.path.join(out_dir, k), 'w') as fh:
            fh.write(v)
    if '--splice' in sys.argv:
        B, E = '/* m67e-generated: begin */', '/* m67e-generated: end */'
        splice(ROOT + '/src/editorctl/lrgeditorctl-mcp.c', B, '\t' + E, files['mcp_rows.c'])
        splice(ROOT + '/src/editorctl/lrgeditorctl.c', B, E, files['ctl_tables.c'])
        u = ''.join('\t"%s\\n"\n' % l.replace('\\', '\\\\').replace('"', '\\"') for l in usage_lines())
        splice(ROOT + '/src/editorctl/lrgeditorctl.c', '/* m67e-usage: begin */', '\t/* m67e-usage: end */', u)
        c = ''.join('\t"%s"\n' % l for l in completion_lines())
        splice(ROOT + '/src/editorctl/lrgeditorctl.c', '/* m67e-completion: begin */', '\t/* m67e-completion: end */', c)
        top_words(ROOT + '/src/editorctl/lrgeditorctl.c')
        OB, OE = '# m67e-generated: begin', '# m67e-generated: end'
        index_rows(ROOT + '/docs/editor/ipc-protocol.org', files['ipc_index.org'])
        splice(ROOT + '/docs/editor/ipc-protocol.org', OB, OE, files['ipc_sections.org'] + '\n')
        splice(ROOT + '/docs/editor/lrgeditorctl.org', OB, OE, files['ctl_doc.org'] + '\n')
        splice(ROOT + '/docs/editor/lrgeditorctl.org', '# m67e-mcp: begin', '# m67e-mcp: end', files['mcp_doc.txt'])
        MB, ME = '<!-- m67e-generated: begin -->', '<!-- m67e-generated: end -->'
        splice(ROOT + '/skills/skill-libregnum/references/ipc-protocol.md', MB, ME, files['skill_ipc.md'])
        splice(ROOT + '/skills/skill-libregnum/references/lrgeditorctl.md', MB, ME, files['skill_ctl.md'])
        splice(ROOT + '/skills/skill-libregnum/references/mcp.md', MB, ME, files['skill_mcp.md'])


if __name__ == '__main__':
    main()
