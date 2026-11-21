"""Page generators for the AIVERSE website, one call per language (stdlib only).

Russian pages are written to the site root, other languages to <lang>/ with the same
file names. Shared assets (style.css, favicon.svg, media/) stay at the root; `B` is
the relative prefix to them. Data fields are localised by suffix: `title` (ru) and
`titleEn` (en); a missing translation falls back to the Russian text.
"""
import html
import json
import posixpath
import re

from mdhtml import Converter

_ARM = '-72,-220 0,-262 71,-220 71,-58 0,-100 -72,-58'
MARK_LIGHT = ('<svg width="72" height="68" viewBox="-280 -280 560 530"><g fill="#F3EFE8">'
              f'<polygon points="{_ARM}"/><polygon points="{_ARM}" transform="rotate(120)"/>'
              f'<polygon points="{_ARM}" transform="rotate(240)"/></g>'
              '<polygon points="0,-78 78,0 0,78 -78,0" fill="#E9C3A4"/></svg>')


def esc(text):
    return html.escape(str(text or ''), quote=True)


class Ctx:
    def __init__(self, lang, strings, langs):
        self.lang = lang
        self.T = strings
        self.B = '' if lang == 'ru' else '../'
        self.other = 'en' if lang == 'ru' else 'ru'
        self.langs = langs

    def loc(self, obj, key):
        if self.lang != 'ru':
            v = obj.get(key + self.lang.capitalize())
            if v:
                return v
        return obj.get(key)

    def media(self, src):
        return f'{self.B}media/{src}'

    def other_href(self, page):
        return (f'en/{page}' if self.lang == 'ru' else f'../{page}')

    def fmt_date(self, iso):
        y, m, d = iso.split('-')
        return f'{int(d)}.{m}.{y}' if self.lang == 'ru' else f'{self.T["months"][int(m) - 1][:3]} {int(d)}, {y}'

    def fmt_target(self, target):
        if not target:
            return self.T['noTarget']
        y, m = target.split('-')
        return f'{self.T["months"][int(m) - 1]} {y}'

    def where(self, w):
        return self.T['whereMain'] if w == 'main' else self.T['whereBranch']

    def status(self, st):
        return {'done': self.T['stDone'], 'in-progress': self.T['stProgress'], 'planned': self.T['stPlanned']}[st]


def fill_tokens(text, ctx, page):
    for k, v in ctx.T.items():
        if isinstance(v, str):
            text = text.replace('{{' + k + '}}', v)
    text = text.replace('{{B}}', ctx.B).replace('{{lang}}', ctx.lang).replace('{{otherLang}}', ctx.other)
    return text.replace('{{OTHER_HREF}}', ctx.other_href(page))


def render_page(layout, ctx, page, title, desc, content, nav=None, script='', main_class=''):
    out = fill_tokens(layout, ctx, page)
    out = out.replace('<!--TITLE-->', esc(title)).replace('<!--DESC-->', esc(desc))
    out = out.replace('<!--MAINCLASS-->', main_class)
    out = out.replace('<!--CONTENT-->', content).replace('<!--SCRIPT-->', script)
    if nav:
        out = out.replace(f'data-nav="{nav}"', f'data-nav="{nav}" aria-current="page"')
    return out


# ---------------------------------------------------------------- main page blocks

def showcase_html(frame, systems, ctx):
    more = ''
    if frame.get('entry'):
        more = f'<a href="devlog.html#{esc(frame["entry"])}">{ctx.T["showcaseMore"]}</a>'
    title = ctx.loc(frame, 'title')
    return (
        '<section class="showcase" aria-labelledby="showcase-title">\n'
        f'<figure><img src="{esc(ctx.media(frame["src"]))}" alt="{esc(title)}">\n'
        '<figcaption><div class="wrap"><div>'
        f'<p class="eyebrow">{ctx.T["showcaseEyebrow"]}</p>'
        f'<h2 id="showcase-title">{esc(title)}</h2><p>{esc(ctx.loc(frame, "caption"))}</p></div>'
        f'<div class="meta"><span>{esc(ctx.loc(systems[frame["system"]], "name"))} · {ctx.fmt_date(frame["date"])}</span>{more}</div>'
        '</div></figcaption></figure>\n</section>'
    )


def cover_visual(c, ctx, lazy=True):
    cap = esc(ctx.loc(c, 'caption'))
    if c.get('video'):
        return (f'<video src="{esc(ctx.media(c["video"]))}" poster="{esc(ctx.media(c["src"]))}" autoplay muted loop '
                f'playsinline preload="none" aria-label="{cap}"></video>')
    return f'<img src="{esc(ctx.media(c["src"]))}" alt="{cap}"{" loading=\"lazy\"" if lazy else ""}>'


def systems_html(systems, ctx):
    cards = []
    for s in systems:
        if not s.get('flagship'):
            continue
        sid = esc(s['id'])
        link = f'<a class="sys-link" href="system-{sid}.html">{ctx.T["cardMore"]}</a>'
        badge = f'<span class="badge{" badge-ink" if s["where"] == "main" else ""}">{ctx.where(s["where"])}</span>'
        head = f'<div class="sys-head"><h3><a href="system-{sid}.html">{esc(s["name"])}</a></h3>{badge}</div>'
        text = f'<p class="sys-tag">{esc(ctx.loc(s, "tagline"))}</p><p>{esc(ctx.loc(s, "essence"))}</p>'
        if s.get('cover'):
            c = s['cover']
            cards.append(f'<article class="sys-card" id="sys-{sid}"><figure>{cover_visual(c, ctx)}'
                         f'<figcaption>{esc(ctx.loc(c, "caption"))}</figcaption></figure>'
                         f'<div class="sys-body">{head}{text}{link}</div></article>')
        else:
            cards.append(f'<article class="sys-card sys-pending" id="sys-{sid}">'
                         f'<div class="sys-placeholder" aria-hidden="true">{MARK_LIGHT}<span>{esc(s["name"])}</span></div>'
                         f'<div class="sys-body">{head}{text}<p class="sys-wait">{ctx.T["coverPending"]}</p>{link}</div></article>')
    return '\n'.join(cards)


# ---------------------------------------------------------------- system pages and roadmap

def roadmap_item(it, ctx, system=None):
    st = it['status']
    when = ctx.fmt_date(it['date']) if st == 'done' else ctx.fmt_target(it.get('target'))
    chip = (f'<a class="rm-sys" href="system-{esc(system["id"])}.html">{esc(system["name"])}</a>' if system else '')
    text = ctx.loc(it, 'text')
    text = f'<p>{esc(text)}</p>' if text else ''
    data = f' data-sys="{esc(system["id"])}"' if system else ''
    return (f'<li{data} class="rm-item rm-{st}"><span class="rm-dot" aria-hidden="true"></span>'
            f'<div><div class="rm-meta">{chip}<span class="st st-{st}">{ctx.status(st)}</span>'
            f'<time>{esc(when)}</time></div><h3>{esc(ctx.loc(it, "title"))}</h3>{text}</div></li>')


def sorted_roadmap(items):
    order = {'done': 0, 'in-progress': 1, 'planned': 2}
    return sorted(items, key=lambda it: (order[it['status']], it.get('date') or it.get('target') or '9999'))


def system_page(s, flagships, entries, ctx, repo_blob, doc_pages):
    sid = esc(s['id'])
    mine = [e for e in entries if e['system'] == s['id']]
    buttons = []
    art = s.get('article')
    if art and art in doc_pages:
        buttons.append(f'<a class="btn btn-ink" href="docs-{doc_pages[art]}.html">{ctx.T["docsPage"]}</a>')
    elif art:
        href = art if art.startswith('https://') else f'{repo_blob}/{art}'
        buttons.append(f'<a class="btn btn-ink" href="{esc(href)}">{ctx.T["article"]}</a>')
    buttons.append(f'<a class="btn btn-line" href="devlog.html#system={sid}">{ctx.T["devlogN"]} · {len(mine)}</a>')
    hero = (f'<section class="sp-hero"><p class="crumbs"><a href="./#systems">{ctx.T["crumbSystems"]}</a> / {esc(s["name"])}</p>'
            f'<div class="sp-title"><h1>{esc(s["name"])}</h1><span class="badge{" badge-ink" if s["where"] == "main" else ""}">'
            f'{ctx.where(s["where"])}</span></div><p class="sp-tag">{esc(ctx.loc(s, "tagline"))}</p>'
            f'<p class="sp-lead">{esc(ctx.loc(s, "essence"))}</p>'
            f'<p class="sp-since mono">{ctx.T["since"]} {ctx.fmt_date(s["started"])}</p>'
            f'<div class="actions">{"".join(buttons)}</div></section>')
    c = s.get('cover')
    if c:
        cover = f'<figure class="sp-cover">{cover_visual(c, ctx, lazy=False)}<figcaption>{esc(ctx.loc(c, "caption"))}</figcaption></figure>'
    else:
        cover = (f'<div class="sp-cover sp-pending"><div class="sys-placeholder">{MARK_LIGHT}<span>{esc(s["name"])}</span></div>'
                 f'<p><strong>{ctx.T["pendingTitle"]}</strong> {esc(ctx.loc(s, "coverBrief"))}</p></div>')
    inc = ''.join(f'<article class="card"><h3>{esc(ctx.loc(i, "name"))}</h3><p>{esc(ctx.loc(i, "text"))}</p></article>'
                  for i in s['includes'])
    ena = ''.join(f'<li>{esc(x)}</li>' for x in (ctx.loc(s, 'enables') or []))
    rm = ''.join(roadmap_item(it, ctx) for it in sorted_roadmap(s['roadmap']))
    shots = []
    for e in mine[:6]:
        m = next((m for m in e['media'] if not re.search(r'\.(mp4|webm)$', m['src'], re.I)), None)
        img = f'<img src="{esc(m["src"])}" alt="{esc(m["caption"])}" loading="lazy">' if m else ''
        shots.append(f'<a class="sp-entry" href="devlog.html#{esc(e["id"])}">{img}<span class="mono">{ctx.fmt_date(e["date"])}</span>'
                     f'<strong>{esc(e["title"])}</strong><span>{esc(e["summary"])}</span></a>')
    log = (f'<div class="sp-entries">{"".join(shots)}</div>' if shots else f'<p class="empty">{ctx.T["noEntries"]}</p>')
    others = ''.join(f'<a class="chip{" chip-ink" if o["id"] == s["id"] else ""}" href="system-{esc(o["id"])}.html">{esc(o["name"])}</a>'
                     for o in flagships)
    return (hero + cover +
            f'<section class="sp-sec"><h2 class="h2">{ctx.T["secIncludes"]}</h2><div class="grid sp-inc">{inc}</div></section>'
            f'<section class="sp-sec"><h2 class="h2">{ctx.T["secEnables"]}</h2><ul class="sp-ena">{ena}</ul></section>'
            f'<section class="sp-sec"><div class="sp-sec-head"><h2 class="h2">{ctx.T["secRoadmap"]}</h2>'
            f'<a href="roadmap.html#{sid}">{ctx.T["secRoadmapAll"]}</a></div><ol class="rm-list">{rm}</ol></section>'
            f'<section class="sp-sec"><h2 class="h2">{ctx.T["secDevlog"]}</h2>{log}</section>'
            f'<section class="sp-sec sp-others"><p class="eyebrow">{ctx.T["otherSystems"]}</p><div class="chips">{others}</div></section>')


def roadmap_page(flagships, ctx):
    chips = [f'<button class="filter" type="button" data-sys="" aria-pressed="true">{ctx.T["rmAll"]}</button>']
    chips += [f'<button class="filter" type="button" data-sys="{esc(s["id"])}" aria-pressed="false">{esc(s["name"])}</button>'
              for s in flagships]
    cols = []
    for st in ('done', 'in-progress', 'planned'):
        items = [(it, s) for s in flagships for it in s['roadmap'] if it['status'] == st]
        if st == 'done':
            items.sort(key=lambda x: x[0]['date'], reverse=True)
        else:
            items.sort(key=lambda x: (x[0].get('target') or '9999', x[1]['name']))
        lis = ''.join(roadmap_item(it, ctx, s) for it, s in items)
        cols.append(f'<section class="rm-col rm-col-{st}"><h2>{ctx.status(st)} <span class="count">{len(items)}</span></h2>'
                    f'<ol class="rm-list">{lis}</ol></section>')
    total = sum(len(s['roadmap']) for s in flagships)
    return (f'<section class="dl-hero"><div><p class="eyebrow">{ctx.T["rmEyebrow"]}</p><h1>{ctx.T["rmTitle"]}</h1>'
            f'<p>{ctx.T["rmLead"]}</p></div>'
            f'<div class="dl-stats"><div><strong>{len(flagships)}</strong><span>{ctx.T["rmSystems"]}</span></div>'
            f'<div><strong>{total}</strong><span>{ctx.T["rmItems"]}</span></div></div></section>'
            f'<div class="dl-filters rm-filters" role="group" aria-label="{ctx.T["rmFilter"]}">{"".join(chips)}</div>'
            f'<div class="rm-board">{"".join(cols)}</div>')


ROADMAP_JS = """<script>
(function(){var b=document.querySelectorAll('.rm-filters .filter');function set(id){b.forEach(function(x){x.setAttribute('aria-pressed',String(x.dataset.sys===id));});
document.querySelectorAll('.rm-item').forEach(function(li){li.hidden=!!id&&li.dataset.sys!==id;});
document.querySelectorAll('.rm-col').forEach(function(c){c.querySelector('.count').textContent=c.querySelectorAll('.rm-item:not([hidden])').length;});
history.replaceState(null,'',id?'#'+id:location.pathname);}
b.forEach(function(x){x.addEventListener('click',function(){set(x.dataset.sys);});});
var h=location.hash.slice(1);if(h&&document.querySelector('.rm-filters [data-sys="'+h+'"]'))set(h);})();
</script>"""


# ---------------------------------------------------------------- documentation

def is_russian(text):
    letters = re.findall(r'[A-Za-zА-Яа-яЁё]', text[:20000])
    cyr = sum(1 for ch in letters if re.match(r'[А-Яа-яЁё]', ch))
    return bool(letters) and cyr / len(letters) > 0.3


def docs_nav(manifest, ctx, current=None):
    parts = []
    for sec in manifest['sections']:
        links = ''.join(f'<a href="docs-{esc(p["id"])}.html"{" aria-current=\"page\"" if p["id"] == current else ""}>'
                        f'{esc(ctx.loc(p, "title"))}</a>' for p in sec['pages'])
        parts.append(f'<div class="dn-sec"><span>{esc(ctx.loc(sec, "title"))}</span>{links}</div>')
    return f'<nav class="docs-nav" aria-label="{ctx.T["navDocs"]}">{"".join(parts)}</nav>'


def doc_page(page, manifest, ctx, root, repo, doc_pages):
    src = page['source']
    text = (root / src).read_text(encoding='utf-8')
    base_dir = posixpath.dirname(src)
    raw = repo.replace('https://github.com/', 'https://raw.githubusercontent.com/') + '/main/'

    def link(href):
        if re.match(r'^(https?:|mailto:|#)', href):
            return href
        path, _, anchor = href.partition('#')
        target = posixpath.normpath(posixpath.join(base_dir, path)) if path else src
        if target in doc_pages:
            return f'docs-{doc_pages[target]}.html' + (f'#{anchor}' if anchor else '')
        kind = 'tree' if not posixpath.splitext(target)[1] else 'blob'
        return f'{repo}/{kind}/main/{target}' + (f'#{anchor}' if anchor else '')

    def image(href):
        if re.match(r'^https?:', href):
            return href
        return raw + posixpath.normpath(posixpath.join(base_dir, href))

    conv = Converter(link, image)
    body = conv.convert(text)
    toc = ''.join(f'<a class="toc-{lvl}" href="#{hid}">{esc(t[:90])}</a>'
                  for lvl, hid, t in conv.headings if lvl in (2, 3))[:20000]
    note_key = 'docsLangNoteRu' if is_russian(text) else 'docsLangNote'
    note = ctx.T.get(note_key) or ''
    if note_key == 'docsLangNoteRu' and ctx.lang == 'ru':
        note = ''
    note_html = f'<p class="doc-note">{esc(note)}</p>' if note else ''
    return (f'<div class="docs-layout">{docs_nav(manifest, ctx, page["id"])}'
            f'<article class="doc" lang="{"ru" if is_russian(text) else "en"}">{note_html}{body}'
            f'<p class="doc-src"><a href="{esc(repo)}/blob/main/{esc(src)}">{ctx.T["docsSource"]}: {esc(src)}</a></p></article>'
            f'<aside class="doc-toc" aria-label="{ctx.T["docsOnPage"]}"><span>{ctx.T["docsOnPage"]}</span>{toc}</aside></div>'), conv.headings


def docs_index(manifest, ctx):
    secs = []
    for sec in manifest['sections']:
        cards = ''.join(f'<a class="doc-card" href="docs-{esc(p["id"])}.html"><strong>{esc(ctx.loc(p, "title"))}</strong>'
                        f'<span>{esc(ctx.loc(p, "lead") or "")}</span><code>{esc(p["source"])}</code></a>'
                        for p in sec['pages'])
        secs.append(f'<section class="sp-sec"><h2 class="h2">{esc(ctx.loc(sec, "title"))}</h2><div class="doc-cards">{cards}</div></section>')
    return (f'<section class="dl-hero"><div><p class="eyebrow">{ctx.T["docsEyebrow"]}</p><h1>{ctx.T["docsTitle"]}</h1>'
            f'<p>{ctx.T["docsLead"]}</p></div></section>' + ''.join(secs) + '<div style="height:96px"></div>')


def localized_entries(entries, ctx):
    out = []
    for e in entries:
        e2 = json.loads(json.dumps(e))
        for k in ('title', 'summary', 'body', 'verified', 'limits'):
            e2[k] = ctx.loc(e, k)
        for m in e2['media']:
            m['caption'] = ctx.loc(m, 'caption')
            m.pop('captionEn', None)
            m['src'] = ctx.B + m['src']
        for l in e2.get('links') or []:
            l['title'] = ctx.loc(l, 'title')
            l.pop('titleEn', None)
        for k in list(e2):
            if k.endswith('En'):
                e2.pop(k)
        out.append(e2)
    return out
