"""Read and write the game's rules split over several files (engine docs/game-format.md § Splitting rules.json).

<project>/rules.json lists more files with "include"; the engine assembles them into one document
(engine/data/JsonCompose.cpp). Install scripts work on that one document and write it back here:
every top-level key and every member of a top-level object (a type, a driving vehicle, ...) goes
back into the file it came from; a new member goes next to the member before it.

    import rules_io
    rules = rules_io.rules_file(root / 'project')   # path-like: read_text / write_text / read_bytes / ...
    r = json.loads(rules.read_text()); ...; rules.write_text(json.dumps(r))

    r = rules_io.load(root / 'project'); ...; rules_io.save(root / 'project', r)

A rules file without "include" (test fixtures) reads and writes exactly like a plain file.
"extends" / "abstract" are returned as written; resolve() gives what the engine loads.
"""
from pathlib import Path
import copy, json, os

SEP = '\x1f'


class RulesError(ValueError):
    pass


class Origin:
    def __init__(self):
        self.files = []        # paths, root first, then includes in reading order
        self.where = {}        # piece -> file index
        self.includes = {}     # file index -> its "include" value
        self.docs = {}         # file index -> parsed content as read


def _item_piece(top, item, index):
    if isinstance(item, dict) and isinstance(item.get('id'), str):
        return top + SEP + '#' + item['id']
    return top + SEP + '[%d]' % index


def _describe(piece):
    return piece.replace(SEP, '.')


def compose(path):
    """The root file with its includes merged, the same way as the engine. Returns (doc, Origin)."""
    path = Path(path)
    root_dir = path.parent.resolve()
    origin = Origin()
    out = {}
    stack = []

    def claim(piece, f):
        if piece in origin.where:
            raise RulesError('%s: %r is already defined in %s' % (origin.files[f], _describe(piece), origin.files[origin.where[piece]]))
        origin.where[piece] = f

    def merge_top(key, v, f):
        have = out.get(key)
        if isinstance(v, dict):
            if key in out and not isinstance(have, dict):
                claim(key, f)
            if key not in out:
                origin.where.setdefault(key, f)
                out[key] = have = {}
            for m, x in v.items():
                claim(key + SEP + m, f)
                have[m] = x
        elif isinstance(v, list):
            if key in out and not isinstance(have, list):
                claim(key, f)
            if key not in out:
                origin.where.setdefault(key, f)
                out[key] = have = []
            for x in v:
                claim(_item_piece(key, x, len(have)), f)
                have.append(x)
        else:
            claim(key, f)
            out[key] = v

    def add(p):
        p = Path(os.path.normpath(p))
        if p in stack:
            raise RulesError('%s: included from itself (%s)' % (p, stack[-1]))
        if not p.is_file():
            raise RulesError('cannot open %s' % p if not stack else "%s: include '%s' not found" % (stack[-1], p))
        doc = json.loads(p.read_text(encoding='utf-8'))
        if not isinstance(doc, dict):
            raise RulesError('%s: must hold a JSON object' % p)
        f = len(origin.files)
        origin.files.append(p)
        origin.docs[f] = doc
        inc = doc.get('include')
        includes = [inc] if isinstance(inc, str) else list(inc or [])
        if 'include' in doc:
            origin.includes[f] = inc
        for k, v in doc.items():
            if k != 'include':
                merge_top(k, v, f)
        stack.append(p)
        for rel in includes:
            full = Path(os.path.normpath(p.parent / rel))
            if os.path.relpath(full.resolve(), root_dir).startswith('..'):
                raise RulesError("%s: include '%s' points outside the game folder" % (p, rel))
            add(full)
        stack.pop()

    add(path)
    return out, origin


def _deep_merge(base, over):
    if not isinstance(base, dict) or not isinstance(over, dict):
        return copy.deepcopy(over)
    out = copy.deepcopy(base)
    for k, v in over.items():
        out[k] = _deep_merge(base.get(k), v)
    return out


def resolve(doc):
    """A copy with every "extends" applied and abstract entries removed (engine resolveExtends)."""
    def is_entry(e):
        return isinstance(e, dict) and ('extends' in e or 'abstract' in e)
    out = {}
    for section, v in doc.items():
        entries = v.values() if isinstance(v, dict) else v if isinstance(v, list) else []
        if not any(is_entry(e) for e in entries):
            out[section] = copy.deepcopy(v)
            continue
        if isinstance(v, dict):
            by_name = {m: e for m, e in v.items() if isinstance(e, dict)}
        else:
            by_name = {}
            for e in v:
                if isinstance(e, dict) and isinstance(e.get('id'), str):
                    if e['id'] in by_name:
                        raise RulesError("%s: id '%s' is used twice" % (section, e['id']))
                    by_name[e['id']] = e
        done, path = {}, []

        def res(name):
            if name in done:
                return done[name]
            if name in path:
                raise RulesError('%s: extends cycle %s' % (section, ' -> '.join(path + [name])))
            e = by_name[name]
            path.append(name)
            ext = e.get('extends')
            bases = [ext] if isinstance(ext, str) else list(ext or [])
            r = {}
            for b in bases:
                if b not in by_name:
                    raise RulesError("%s '%s': extends unknown '%s'" % (section, name, b))
                base = copy.deepcopy(res(b))
                base.pop('abstract', None)
                r = _deep_merge(r, base)
            r = _deep_merge(r, e)
            r.pop('extends', None)
            path.pop()
            done[name] = r
            return r

        if isinstance(v, dict):
            res_v = {}
            for m, e in v.items():
                if not isinstance(e, dict):
                    res_v[m] = copy.deepcopy(e)
                    continue
                x = copy.deepcopy(res(m))
                if x.get('abstract'):
                    continue
                x.pop('abstract', None)
                res_v[m] = x
        else:
            res_v = []
            for e in v:
                if not isinstance(e, dict) or not isinstance(e.get('id'), str):
                    if is_entry(e):
                        raise RulesError('%s: an entry with extends / abstract needs an "id"' % section)
                    res_v.append(copy.deepcopy(e))
                    continue
                x = copy.deepcopy(res(e['id']))
                if x.get('abstract'):
                    continue
                x.pop('abstract', None)
                res_v.append(x)
        out[section] = res_v
    return out


def split(doc, origin):
    """[(path, content)] for every file of origin, the way the engine's splitComposed writes it back."""
    n = max(1, len(origin.files))
    out = [dict() for _ in range(n)]
    for f, inc in origin.includes.items():
        out[f]['include'] = inc
    def file_of(piece, fallback):
        return origin.where.get(piece, fallback)
    for k, v in doc.items():
        if k == 'include':
            continue
        home = file_of(k, 0)
        if isinstance(v, dict):
            last = home
            if not v:
                out[home][k] = {}
            for m, x in v.items():
                last = file_of(k + SEP + m, last)
                out[last].setdefault(k, {})[m] = x
        elif isinstance(v, list):
            last = home
            if not v:
                out[home][k] = []
            for i, x in enumerate(v):
                last = file_of(_item_piece(k, x, i), last)
                out[last].setdefault(k, []).append(x)
        else:
            out[home][k] = v
    return [(origin.files[i], out[i]) for i in range(n)]


def _rules_path(project_or_file):
    p = Path(project_or_file)
    return p if p.suffix == '.json' else p / 'rules.json'


def dumps(content):
    return json.dumps(content, indent=1, ensure_ascii=False) + '\n'


def _ordered_equal(a, b):
    if isinstance(a, dict) and isinstance(b, dict):
        return list(a) == list(b) and all(_ordered_equal(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(_ordered_equal(x, y) for x, y in zip(a, b))
    return type(a) == type(b) and a == b


def load(project_or_file):
    """The rules as one document (unresolved: "extends" stays as written)."""
    return compose(_rules_path(project_or_file))[0]


def save(project_or_file, doc, text=None):
    """Write the document back into the files it is made of. Unchanged files are not rewritten.
    A single file without includes gets `text` verbatim when given (the caller's own formatting)."""
    path = _rules_path(project_or_file)
    if not path.exists():
        path.write_text(text if text is not None else dumps(doc), encoding='utf-8')
        return
    _, origin = compose(path)
    if len(origin.files) == 1 and not origin.includes:
        path.write_text(text if text is not None else dumps(doc), encoding='utf-8')
        return
    for p, content in split(doc, origin):
        old = origin.docs[origin.files.index(p)]
        if not _ordered_equal(old, content):
            p.write_text(dumps(content), encoding='utf-8')
    again, _ = compose(path)
    want = {k: v for k, v in doc.items() if k != 'include'}
    # Top-level order is not part of the game (the root file's own keys come first); entry order is.
    if set(again) != set(want) or not all(_ordered_equal(again[k], want[k]) for k in want):
        # Only order across files can differ (an entry moved before entries of an earlier file).
        raise RulesError('%s: the rules could not be written back in this order; '
                         'add new entries after the existing ones of their collection' % path)


class RulesFile:
    """A stand-in for the path of rules.json that reads and writes the whole composed document."""
    def __init__(self, project_or_file):
        self.path = _rules_path(project_or_file)
        self.name, self.parent, self.suffix = self.path.name, self.path.parent, self.path.suffix

    def _single(self):
        _, origin = compose(self.path)
        return len(origin.files) == 1 and not origin.includes

    def exists(self):
        return self.path.exists()

    def read_text(self, encoding='utf-8', errors=None):
        if self._single():
            return self.path.read_text(encoding=encoding)
        return dumps(load(self.path))

    def read_bytes(self):
        return self.read_text().encode('utf-8')

    def write_text(self, text, encoding='utf-8', errors=None, newline=None):
        save(self.path, json.loads(text), text)
        return len(text)

    def write_bytes(self, data):
        return self.write_text(data.decode('utf-8'))

    def open(self, mode='r', encoding='utf-8', **kw):
        import io
        if 'r' in mode:
            return io.StringIO(self.read_text()) if 'b' not in mode else io.BytesIO(self.read_bytes())
        target = self
        class _W(io.StringIO):
            def close(s):
                if not s.closed:
                    target.write_text(s.getvalue())
                super().close()
            def __exit__(s, *a):
                s.close()
        return _W()

    def __fspath__(self):
        # For handing the path to a program that reads the rules itself (the engine composes the includes).
        # open() / shutil.copy() on it see only the root file: use read_text() / files() instead.
        return str(self.path)

    def stat(self):
        return self.path.stat()

    def resolve(self):
        return self.path.resolve()

    def __str__(self):
        return str(self.path)

    def __repr__(self):
        return 'RulesFile(%r)' % str(self.path)

    def __eq__(self, other):
        return str(self) == str(other)

    def __hash__(self):
        return hash(str(self))


def rules_file(project_or_file):
    return RulesFile(project_or_file)


def files(project_or_file):
    """Every file the rules are made of (root first): copy all of them when copying a project."""
    return compose(_rules_path(project_or_file))[1].files
