"""Cavedog TDF text format (Total Annihilation .fbi units, .tdf weapons / features / sides, .gui menus, .ota maps).

    [SECTION] { key=value; [SUB] { key=value; } }

Keys and section names are case-insensitive (stored lower case); values are strings, trimmed. // and /* */
comments are skipped. parse() -> {section: {key: value or {subsection...}}} keeping file order; a repeated
section name gets a #2, #3 suffix so nothing is lost."""
import re

_TOKEN = re.compile(r'\[([^\]]*)\]|\{|\}|([^=;{}\[\]]+)=([^;{}]*);?|;')


def _strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def parse(text):
    """TDF text -> nested dicts (lower-case keys)."""
    text = _strip_comments(text)
    root, stack, pending = {}, [], None
    cur = root
    for m in _TOKEN.finditer(text):
        tok = m.group(0)
        if m.group(1) is not None:
            pending = m.group(1).strip().lower()
        elif tok == '{':
            name = pending if pending is not None else ''
            key, n = name, 2
            while key in cur:
                key, n = '%s#%d' % (name, n), n + 1
            child = {}
            cur[key] = child
            stack.append(cur)
            cur, pending = child, None
        elif tok == '}':
            cur = stack.pop() if stack else root
        elif m.group(2) is not None:
            cur[m.group(2).strip().lower()] = m.group(3).strip()
    return root


def load(path):
    with open(path, 'rb') as f:
        return parse(f.read().decode('latin-1'))


def num(d, key, default=0.0):
    """A numeric value of a section (TA writes 1, 1.5, .25, -3); default when missing or not a number."""
    v = d.get(key)
    if v is None:
        return default
    m = re.match(r'\s*[-+]?(\d+\.?\d*|\.\d+)', v)
    return float(m.group(0)) if m else default
