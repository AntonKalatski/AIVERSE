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
        self.rel = {'released': set(), 'next': ''}

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


def version_badge(s, ctx):
    """Release badge of a flagship system: shipped version, the next one, or in development."""
    r = s.get('release')
    if r and r in ctx.rel['released']:
        return (f'<a class="badge badge-ink" href="releases.html#v{esc(r)}" title="{esc(ctx.T["verSinceTitle"].format(v=r))}">'
                f'{esc(ctx.T["verSince"].format(v=r))}</a>')
    nxt = r or ctx.rel['next']
    if s.get('where') == 'main' and nxt:
        return (f'<a class="badge badge-accent" href="releases.html#next" title="{esc(ctx.T["verNextTitle"].format(v=nxt))}">'
                f'{esc(ctx.T["verNext"].format(v=nxt))}</a>')
    return f'<span class="badge" title="{esc(ctx.T["verBranchTitle"])}">{ctx.T["verBranch"]}</span>'


def systems_html(systems, ctx):
    cards = []
    for s in systems:
        if not s.get('flagship'):
            continue
        sid = esc(s['id'])
        link = f'<a class="sys-link" href="system-{sid}.html">{ctx.T["cardMore"]}</a>'
        badge = version_badge(s, ctx)
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
            f'<div class="sp-title"><h1>{esc(s["name"])}</h1>{version_badge(s, ctx)}</div><p class="sp-tag">{esc(ctx.loc(s, "tagline"))}</p>'
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


# ---------------------------------------------------------------- releases

def release_chip(releases, ctx):
    if not releases:
        return ''
    r = releases[0]
    return f'<a class="chip chip-link" href="releases.html">{ctx.T["relChip"].format(v=esc(r["version"]))}</a>'


def releases_page(data, entries, systems, ctx, repo):
    rels = data['releases']
    builds = data['builds']
    latest = rels[0] if rels else None
    since = latest['date'] if latest else '0000-00-00'
    names = {s['id']: ctx.loc(s, 'name') for s in systems}
    pages = {s['id'] for s in systems if s.get('flagship')}

    def sys_tag(sid):
        label = esc(names.get(sid, sid))
        if sid in pages:
            return f'<a class="rm-sys" href="system-{esc(sid)}.html">{label}</a>'
        return f'<span class="rm-sys">{label}</span>'
    fresh = [e for e in entries if e['date'] > since]
    head = (f'<section class="dl-hero"><div><p class="eyebrow">{ctx.T["relEyebrow"]}</p><h1>{ctx.T["relTitle"]}</h1>'
            f'<p>{ctx.T["relLead"]}</p></div>'
            f'<div class="dl-stats"><div><strong>{esc(latest["version"]) if latest else "—"}</strong><span>{ctx.T["relLatest"]}</span></div>'
            f'<div><strong>{len(rels)}</strong><span>{ctx.T["relCount"]}</span></div></div></section>')
    nxt = ''
    if latest:
        items = ''.join(
            f'<li><span class="rm-meta"><span>{ctx.fmt_date(e["date"])}</span>'
            f'{sys_tag(e["system"])}</span>'
            f'<a href="devlog.html#{esc(e["id"])}">{esc(e["title"])}</a></li>'
            for e in fresh[:12])
        body = (f'<ul class="rel-next-list">{items}</ul>' if items else f'<p>{ctx.T["relNextEmpty"]}</p>')
        more = (f'<a class="rel-more" href="devlog.html">{ctx.T["relNextAll"].format(n=len(fresh))}</a>'
                if len(fresh) > 12 else '')
        nxt = (f'<section class="rel-next" id="next"><div class="rel-next-head"><h2>'
               f'{ctx.T["relNextTitleV"].format(v=esc(data["next"]["version"])) if data.get("next") else ctx.T["relNextTitle"]}</h2>'
               f'<p>{ctx.T["relNextLead"].format(v=esc(latest["version"]), d=ctx.fmt_date(latest["date"]))}</p></div>'
               f'{body}{more}</section>')
    cards = []
    for i, r in enumerate(rels):
        hl = ''.join(f'<li><h3>{esc(ctx.loc(h, "title"))}</h3><p>{esc(ctx.loc(h, "text"))}</p></li>'
                     for h in r.get('highlights') or [])
        ver = ctx.loc(r, 'verified')
        notes = f'{repo}/releases/tag/{r["tag"]}'
        cards.append(
            f'<article class="rel-card{" rel-latest" if i == 0 else ""}" id="v{esc(r["version"])}">'
            f'<div class="rel-side"><span class="rel-ver">{esc(r["version"])}</span>'
            f'<span class="rel-date">{ctx.fmt_date(r["date"])}</span>'
            + (f'<span class="rel-badge">{ctx.T["relLatestBadge"]}</span>' if i == 0 else '') +
            f'<a class="rel-commit" href="{repo}/commit/{esc(r["commit"])}"><code>{esc(r["commit"][:9])}</code></a></div>'
            f'<div class="rel-main"><h2>{esc(ctx.loc(r, "title"))}</h2><p class="rel-summary">{esc(ctx.loc(r, "summary"))}</p>'
            + (f'<ul class="rel-hl">{hl}</ul>' if hl else '')
            + (f'<p class="rel-verified"><b>{ctx.T["relVerified"]}</b> {esc(ver)}</p>' if ver else '') +
            f'<div class="rel-dl" data-version="{esc(r["version"])}"><span class="rel-dl-title">{ctx.T["relDownloads"]}</span>'
            f'<div class="rel-dl-list"><a class="btn btn-line btn-sm" href="{esc(builds["branch"])}">{ctx.T["relBuildsBranch"]}</a></div></div>'
            f'<a class="rel-notes" href="{notes}">{ctx.T["relNotes"]}</a></div></article>')
    other = (f'<section class="rel-other" hidden><h2>{ctx.T["relOtherTitle"]}</h2><p>{ctx.T["relOtherLead"]}</p>'
             f'<div class="rel-other-list"></div></section>')
    return head + nxt + f'<div class="rel-list">{"".join(cards)}</div>' + other


RELEASES_JS = """<script>
(function(){var cfg=window.REL_CFG;if(!cfg)return;
function mb(n){return (n/1048576).toFixed(0)+' MB';}
function btn(p){var a=document.createElement('a');a.className='btn btn-ink btn-sm rel-pkg';a.href=p.url;
a.textContent=(cfg.os[p.platform]||p.platform)+' · '+mb(p.size);a.title='sha256 '+p.sha256+' · '+p.commit;return a;}
fetch(cfg.index,{cache:'no-store'}).then(function(r){if(!r.ok)throw 0;return r.json();}).then(function(d){
var pk=d.packages||[],known={},other=[];
document.querySelectorAll('.rel-dl').forEach(function(el){known[el.dataset.version]=el;});
var used={};pk.forEach(function(p){var el=known[p.version];
if(el&&!used[p.version+p.platform]){used[p.version+p.platform]=1;var l=el.querySelector('.rel-dl-list');
if(!el.dataset.filled){l.textContent='';el.dataset.filled='1';}l.appendChild(btn(p));}else if(!el)other.push(p);});
document.querySelectorAll('.rel-dl:not([data-filled])').forEach(function(el){var s=document.createElement('span');
s.className='rel-dl-none';s.textContent=cfg.none;el.querySelector('.rel-dl-list').prepend(s);});
if(other.length){var box=document.querySelector('.rel-other'),list=box.querySelector('.rel-other-list'),g={};
other.forEach(function(p){var k=p.version+' · '+p.commit;(g[k]=g[k]||[]).push(p);});
Object.keys(g).forEach(function(k){var row=document.createElement('div');row.className='rel-other-row';
var c=document.createElement('code');c.textContent=k;row.appendChild(c);g[k].forEach(function(p){row.appendChild(btn(p));});list.appendChild(row);});
box.hidden=false;}}).catch(function(){});})();
</script>"""
