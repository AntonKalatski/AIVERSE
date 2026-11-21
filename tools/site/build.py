#!/usr/bin/env python3
"""AIVERSE website: devlog entries, flagship systems, showcase frame and the static build (stdlib only).

  python3 tools/site/build.py check                      validate docs/devlog
  python3 tools/site/build.py add --system render --title "..." [--media a.png ...]
  python3 tools/site/build.py build [--out build/site]   site/ + devlog.json + media

Rules for agents: AGENTS.md "Ход разработки на сайте", skill skills/workflow/aiverse-devlog.
"""
import argparse
import datetime as dt
import html
import json
from pathlib import Path
import re
import shutil
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
DEVLOG = ROOT / 'docs/devlog'
ENTRIES = DEVLOG / 'entries'
MEDIA = DEVLOG / 'media'
SYSTEMS = DEVLOG / 'systems.json'
SHOWCASE = DEVLOG / 'showcase.json'
SITE = ROOT / 'site'

STATUSES = ('done', 'in-progress', 'experimental')
REQUIRED = ('id', 'date', 'system', 'title', 'summary', 'body', 'status', 'verified', 'media')
OPTIONAL = ('limits', 'branch', 'commit', 'links', 'author')
IMAGE_EXT = {'.png', '.jpg', '.jpeg', '.webp', '.gif'}
STILL_EXT = {'.png', '.jpg', '.jpeg'}
VIDEO_EXT = {'.mp4', '.webm'}
MAX_IMAGE = 3 * 1024 * 1024
MAX_VIDEO = 15 * 1024 * 1024
MAX_FRAME = 6 * 1024 * 1024
MIN_SHOWCASE_W = 1600
MIN_COVER_W = 1280
ID_RE = re.compile(r'^\d{4}-\d{2}-\d{2}-[a-z0-9]+(?:-[a-z0-9]+)*$')
WHERE = {'main': 'в main', 'branch': 'в разработке'}
_ARM = '-72,-220 0,-262 71,-220 71,-58 0,-100 -72,-58'
MARK_LIGHT = ('<svg width="72" height="68" viewBox="-280 -280 560 530"><g fill="#F3EFE8">'
              f'<polygon points="{_ARM}"/><polygon points="{_ARM}" transform="rotate(120)"/>'
              f'<polygon points="{_ARM}" transform="rotate(240)"/></g>'
              '<polygon points="0,-78 78,0 0,78 -78,0" fill="#E9C3A4"/></svg>')


def load_systems():
    data = json.loads(SYSTEMS.read_text(encoding='utf-8'))
    return {s['id']: s for s in data['systems']}, data['systems']


def repo_file(rel):
    """A repository-relative path that must stay inside the repository."""
    if not isinstance(rel, str) or not rel or rel.startswith('/') or '\\' in rel or '..' in Path(rel).parts:
        return None
    return ROOT / rel


def image_size(f):
    """(width, height) of a PNG or JPEG, or None."""
    data = f.read_bytes()
    if data[:8] == b'\x89PNG\r\n\x1a\n':
        return struct.unpack('>II', data[16:24])
    if data[:2] == b'\xff\xd8':
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                i += 1
                continue
            marker = data[i + 1]
            if marker in (0xC0, 0xC1, 0xC2):
                h, w = struct.unpack('>HH', data[i + 5:i + 9])
                return w, h
            i += 2 + struct.unpack('>H', data[i + 2:i + 4])[0]
    return None


def check_frame(p, fr, min_w, keys):
    """A showcase frame or a system cover: real still, landscape, wide enough, captioned."""
    errors = []
    if not isinstance(fr, dict):
        return [f'{p}: must be an object']
    for key in keys:
        if key not in fr:
            errors.append(f'{p}: missing "{key}"')
    if not isinstance(fr.get('approved'), bool):
        errors.append(f'{p}: "approved" must be true or false; only the user sets true')
    f = repo_file(fr.get('src'))
    if f is None or not f.is_file():
        return errors + [f'{p}: image not found: {fr.get("src")!r}']
    if f.suffix.lower() not in STILL_EXT:
        return errors + [f'{p}: must be a PNG or JPG still']
    if f.stat().st_size > MAX_FRAME:
        errors.append(f'{p}: larger than 6 MB; save a JPG')
    size = image_size(f)
    if size is None or size[0] < min_w or size[0] < size[1]:
        errors.append(f'{p}: must be landscape and at least {min_w} px wide, got {size}')
    return errors


def check_entry(path, systems):
    errors = []
    try:
        e = json.loads(path.read_text(encoding='utf-8'))
    except Exception as exc:  # noqa: BLE001 - report any parse error
        return None, [f'{path.name}: invalid JSON: {exc}']
    p = path.name
    for key in REQUIRED:
        if key not in e:
            errors.append(f'{p}: missing "{key}"')
    for key in e:
        if key not in REQUIRED and key not in OPTIONAL:
            errors.append(f'{p}: unknown key "{key}"')
    if errors:
        return None, errors
    if not ID_RE.match(e['id']):
        errors.append(f'{p}: id must be YYYY-MM-DD-system-slug in lowercase latin')
    if path.stem != e['id']:
        errors.append(f'{p}: file name must be <id>.json')
    try:
        dt.date.fromisoformat(e['date'])
    except ValueError:
        errors.append(f'{p}: date must be YYYY-MM-DD')
    if not e['id'].startswith(e['date'] + '-'):
        errors.append(f'{p}: id must start with the date')
    if e['system'] not in systems:
        errors.append(f'{p}: unknown system "{e["system"]}" (see docs/devlog/systems.json)')
    if e['status'] not in STATUSES:
        errors.append(f'{p}: status must be one of {STATUSES}')
    for key in ('title', 'summary', 'verified'):
        if not isinstance(e[key], str) or not e[key].strip():
            errors.append(f'{p}: "{key}" must be non-empty text')
    if len(e.get('summary') or '') > 280:
        errors.append(f'{p}: summary longer than 280 characters')
    if not isinstance(e['body'], list) or not all(isinstance(x, str) and x.strip() for x in e['body']):
        errors.append(f'{p}: body must be a list of paragraphs')
    media = e['media']
    if not isinstance(media, list) or not media:
        errors.append(f'{p}: at least one screenshot or video in "media"')
        media = []
    for m in media:
        src = m.get('src') if isinstance(m, dict) else None
        f = repo_file(src)
        if f is None:
            errors.append(f'{p}: media src must be a repository-relative path: {src!r}')
            continue
        if not f.is_file():
            errors.append(f'{p}: media file not found: {src}')
            continue
        ext = f.suffix.lower()
        if ext in IMAGE_EXT:
            if f.stat().st_size > MAX_IMAGE:
                errors.append(f'{p}: {src} is larger than 3 MB; save a JPG/WebP')
        elif ext in VIDEO_EXT:
            if f.stat().st_size > MAX_VIDEO:
                errors.append(f'{p}: {src} is larger than 15 MB')
        else:
            errors.append(f'{p}: {src}: only {sorted(IMAGE_EXT | VIDEO_EXT)}')
        if not (m.get('caption') or '').strip():
            errors.append(f'{p}: {src}: caption is required')
    for link in e.get('links') or []:
        href = link.get('href', '') if isinstance(link, dict) else ''
        if not href.startswith('https://'):
            f = repo_file(href)
            if f is None or not f.exists():
                errors.append(f'{p}: link not found: {href!r}')
    return e, errors


def check_systems(systems):
    errors = []
    for s in systems.values():
        p = f'systems.json {s["id"]}'
        if s.get('doc') and not (ROOT / s['doc']).is_file():
            errors.append(f'{p}: doc not found: {s["doc"]}')
        if not s.get('flagship'):
            continue
        for key in ('tagline', 'essence', 'coverBrief', 'where'):
            if not (s.get(key) or '').strip():
                errors.append(f'{p}: flagship system needs "{key}"')
        if s.get('where') not in WHERE:
            errors.append(f'{p}: where must be one of {list(WHERE)}')
        if s.get('cover') is not None:
            errors += check_frame(f'{p} cover', s['cover'], MIN_COVER_W, ('src', 'caption', 'approved'))
            video = s['cover'].get('video')
            if video is not None:
                f = repo_file(video)
                if f is None or not f.is_file() or f.suffix.lower() not in VIDEO_EXT or f.stat().st_size > MAX_VIDEO:
                    errors.append(f'{p} cover: video must be an MP4/WebM in the repository, at most 15 MB: {video!r}')
    return errors


def check_showcase(systems, ids):
    data = json.loads(SHOWCASE.read_text(encoding='utf-8'))
    frames, candidates = data.get('frames'), data.get('candidates', [])
    if not isinstance(frames, list) or not frames:
        return ['showcase.json: "frames" must be a non-empty list']
    if not isinstance(candidates, list):
        return ['showcase.json: "candidates" must be a list']
    errors = []
    keys = ('src', 'title', 'caption', 'system', 'date', 'approved')
    for kind, items in (('frames', frames), ('candidates', candidates)):
        for i, fr in enumerate(items):
            p = f'showcase.json {kind}[{i}]'
            errors += check_frame(p, fr, MIN_SHOWCASE_W, keys)
            if fr.get('system') not in systems:
                errors.append(f'{p}: unknown system {fr.get("system")!r}')
            if fr.get('entry') and fr['entry'] not in ids:
                errors.append(f'{p}: devlog entry not found: {fr["entry"]}')
            if kind == 'candidates' and fr.get('approved'):
                errors.append(f'{p}: an approved frame belongs in "frames"')
    return errors


def check():
    systems, _ = load_systems()
    errors, entries, ids = check_systems(systems), [], set()
    for path in sorted(ENTRIES.glob('*.json')):
        e, errs = check_entry(path, systems)
        errors += errs
        if e:
            if e['id'] in ids:
                errors.append(f'{path.name}: duplicate id')
            ids.add(e['id'])
            entries.append(e)
    for path in ENTRIES.iterdir() if ENTRIES.is_dir() else []:
        if path.suffix != '.json':
            errors.append(f'entries/{path.name}: only <id>.json files belong here; media go to docs/devlog/media/<id>/')
    errors += check_showcase(systems, ids)
    return entries, errors


def esc(text):
    return html.escape(str(text or ''), quote=True)


def showcase_html(frame, systems):
    more = ''
    if frame.get('entry'):
        more = f'<a href="devlog.html#{esc(frame["entry"])}">Подробнее в ходе разработки →</a>'
    return (
        '<section class="showcase" aria-labelledby="showcase-title">\n'
        f'<figure><img src="media/{esc(frame["src"])}" alt="{esc(frame["title"])}">\n'
        '<figcaption><div class="wrap"><div>'
        '<p class="eyebrow">Кадр движка</p>'
        f'<h2 id="showcase-title">{esc(frame["title"])}</h2><p>{esc(frame["caption"])}</p></div>'
        f'<div class="meta"><span>{esc(systems[frame["system"]]["name"])} · {esc(frame["date"])}</span>{more}</div>'
        '</div></figcaption></figure>\n</section>'
    )


def systems_html(systems, counts):
    cards = []
    for s in systems:
        if not s.get('flagship'):
            continue
        n = counts.get(s['id'], 0)
        log = (f'<a class="sys-link" href="devlog.html#system={esc(s["id"])}">Ход разработки · {n}</a>'
               if n else '<span class="sys-link muted">Записей пока нет</span>')
        badge = f'<span class="badge{" badge-ink" if s["where"] == "main" else ""}">{WHERE[s["where"]]}</span>'
        head = f'<div class="sys-head"><h3>{esc(s["name"])}</h3>{badge}</div>'
        text = f'<p class="sys-tag">{esc(s["tagline"])}</p><p>{esc(s["essence"])}</p>'
        if s.get('cover'):
            c = s['cover']
            if c.get('video'):
                visual = (f'<video src="media/{esc(c["video"])}" poster="media/{esc(c["src"])}" autoplay muted loop '
                          f'playsinline preload="none" aria-label="{esc(c["caption"])}"></video>')
            else:
                visual = f'<img src="media/{esc(c["src"])}" alt="{esc(c["caption"])}" loading="lazy">'
            cards.append(
                f'<article class="sys-card" id="sys-{esc(s["id"])}">'
                f'<figure>{visual}'
                f'<figcaption>{esc(c["caption"])}</figcaption></figure>'
                f'<div class="sys-body">{head}{text}{log}</div></article>')
        else:
            cards.append(
                f'<article class="sys-card sys-pending" id="sys-{esc(s["id"])}">'
                f'<div class="sys-placeholder" aria-hidden="true">{MARK_LIGHT}<span>{esc(s["name"])}</span></div>'
                f'<div class="sys-body">{head}{text}<p class="sys-wait">Кадр системы готовится</p>{log}</div></article>')
    return '\n'.join(cards)


def cmd_check(_args):
    entries, errors = check()
    for err in errors:
        print('error:', err)
    print(f'{len(entries)} entries, {len(errors)} errors')
    return 1 if errors else 0


def slugify(text):
    text = re.sub(r'[^a-z0-9]+', '-', text.lower()).strip('-')
    return text[:48].strip('-') or 'update'


def cmd_add(args):
    systems, _ = load_systems()
    if args.system not in systems:
        sys.exit(f'unknown system {args.system}; known: {", ".join(systems)}')
    date = args.date or dt.date.today().isoformat()
    eid = f'{date}-{args.system}-{slugify(args.slug or args.title)}'
    target = ENTRIES / f'{eid}.json'
    if target.exists():
        sys.exit(f'{target.relative_to(ROOT)} already exists; edit it instead')
    media = []
    for i, src in enumerate(args.media or []):
        s = Path(src).resolve()
        if not s.is_file():
            sys.exit(f'media not found: {src}')
        try:
            rel = s.relative_to(ROOT)
        except ValueError:
            dest = MEDIA / eid / f'{i + 1:02d}{s.suffix.lower()}'
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(s, dest)
            rel = dest.relative_to(ROOT)
        media.append({'src': rel.as_posix(), 'caption': 'TODO: что на снимке'})
    entry = {
        'id': eid, 'date': date, 'system': args.system, 'title': args.title,
        'summary': 'TODO: одно-два предложения о том, что стало возможно',
        'body': ['TODO: как это работает и где лежит'],
        'status': args.status, 'verified': 'TODO: какие тесты и снимки реально запускались',
        'limits': None, 'branch': args.branch, 'commit': None,
        'media': media, 'links': [], 'author': args.author,
    }
    ENTRIES.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(entry, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(target.relative_to(ROOT))
    return 0


def cmd_build(args):
    entries, errors = check()
    if errors:
        for err in errors:
            print('error:', err)
        return 1
    _, systems = load_systems()
    by_id = {s['id']: s for s in systems}
    out = Path(args.out).resolve()
    if out == ROOT or (ROOT in out.parents and out.relative_to(ROOT).parts[0] not in ('build', 'dist')):
        sys.exit('--out must be outside the repository or under build/ or dist/')
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(SITE, out, ignore=shutil.ignore_patterns('manifest.json', '.DS_Store'))
    manifest = json.loads((SITE / 'manifest.json').read_text(encoding='utf-8'))
    copied = set()

    def copy_media(rel):
        if rel in copied:
            return
        f = repo_file(rel)
        if f is None or not f.is_file():
            sys.exit(f'site media not found: {rel}')
        dest = out / 'media' / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, dest)
        copied.add(rel)

    for rel in manifest.get('media', []):
        copy_media(rel)
    frame = json.loads(SHOWCASE.read_text(encoding='utf-8'))['frames'][0]
    copy_media(frame['src'])
    for s in systems:
        if s.get('flagship') and s.get('cover'):
            copy_media(s['cover']['src'])
            if s['cover'].get('video'):
                copy_media(s['cover']['video'])
    counts = {}
    for e in entries:
        counts[e['system']] = counts.get(e['system'], 0) + 1
    index = out / 'index.html'
    page = index.read_text(encoding='utf-8')
    for mark in ('<!--SHOWCASE-->', '<!--SYSTEMS-->'):
        if mark not in page:
            sys.exit(f'site/index.html lost the {mark} placeholder')
    page = page.replace('<!--SHOWCASE-->', showcase_html(frame, by_id))
    page = page.replace('<!--SYSTEMS-->', systems_html(systems, counts))
    index.write_text(page, encoding='utf-8')

    entries.sort(key=lambda e: (e['date'], e['id']), reverse=True)
    for e in entries:
        for m in e['media']:
            copy_media(m['src'])
            m['src'] = 'media/' + m['src']
        for link in e.get('links') or []:
            if not link['href'].startswith('https://'):
                link['href'] = f'{manifest["repoBlob"]}/{link["href"]}'
    public = [{k: s[k] for k in ('id', 'name', 'nameEn') if k in s} for s in systems]
    data = {
        'generated': dt.datetime.now(dt.timezone.utc).isoformat(timespec='seconds'),
        'repo': manifest['repo'], 'systems': public, 'entries': entries,
    }
    (out / 'devlog.json').write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding='utf-8')
    (out / '.nojekyll').write_text('', encoding='utf-8')
    print(f'{out}: {len(entries)} entries, {len(copied)} media files')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('check').set_defaults(fn=cmd_check)
    a = sub.add_parser('add')
    a.add_argument('--system', required=True)
    a.add_argument('--title', required=True)
    a.add_argument('--slug')
    a.add_argument('--date')
    a.add_argument('--status', default='in-progress', choices=STATUSES)
    a.add_argument('--branch')
    a.add_argument('--author')
    a.add_argument('--media', nargs='*')
    a.set_defaults(fn=cmd_add)
    b = sub.add_parser('build')
    b.add_argument('--out', default=str(ROOT / 'build/site'))
    b.set_defaults(fn=cmd_build)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == '__main__':
    sys.exit(main())
