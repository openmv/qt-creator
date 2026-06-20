#!/usr/bin/env python3
"""Verify & repair markup in translated Qt .ts files, by SURGICAL TEXT edits
(preserves Qt formatting/escaping; only changed <translation> blocks differ).

Per translated message vs its English <source>:
  * placeholder set (%1/%n/%Ln) changed  -> revert to English fallback
  * HTML tag-name multiset changed       -> revert to English fallback
  * Qt accelerator '&X' lost/broken      -> repair (fullwidth ＆, '& X', drop) or revert
Numerus: checks each form; reverts on corruption; never collapses forms.
"""
import sys
import os
import re
from lxml import etree

TRD = r'C:\github\openmv-ide\qt-creator\share\qtcreator\translations'

PLACEHOLDER = re.compile(r'%(?:L?\d+|L?n|[sdSioxXfgeEcup])')
ENTITY = re.compile(r'&(?:amp|nbsp|lt|gt|quot|apos|copy|reg|deg|times|mdash|ndash|hellip|#\d+|#x[0-9A-Fa-f]+);')
HTML_NAMES = {
    'a', 'abbr', 'address', 'b', 'big', 'blockquote', 'body', 'br', 'center',
    'cite', 'code', 'dd', 'dfn', 'div', 'dl', 'dt', 'em', 'font', 'h1', 'h2',
    'h3', 'h4', 'h5', 'h6', 'head', 'hr', 'html', 'i', 'img', 'kbd', 'li',
    'nobr', 'ol', 'p', 'pre', 'qt', 's', 'samp', 'small', 'span', 'strong',
    'sub', 'sup', 'table', 'tbody', 'td', 'tfoot', 'th', 'thead', 'title',
    'tr', 'tt', 'u', 'ul', 'var',
}
TAG = re.compile(r'</?\s*([a-zA-Z][a-zA-Z0-9]*)')
TRANS_BLOCK = re.compile(r'<translation\b[^>]*/>|<translation\b[^>]*>.*?</translation>', re.S)


def placeholders(t):
    return sorted(PLACEHOLDER.findall(t or ''))


def htmlnames(t):
    return sorted(n.lower() for n in TAG.findall(t or '') if n.lower() in HTML_NAMES)


def accelerators(t):
    if not t:
        return 0
    s = ENTITY.sub('', t).replace('&&', '')
    return len(re.findall(r'&[^&\s]', s))


def _mask(tr):
    masked = list(tr)
    for rx in (PLACEHOLDER, re.compile(r'<[^>]+>'), ENTITY):
        for m in rx.finditer(tr):
            for i in range(m.start(), m.end()):
                masked[i] = ' '
    return ''.join(masked)


def source_mnemonic(src):
    s = ENTITY.sub('', src).replace('&&', '')
    m = re.search(r'&([0-9A-Za-z])', s)
    return m.group(1) if m else None


def _reinsert(tr, mn):
    masked = _mask(tr)
    if mn:
        m = re.search(re.escape(mn), masked, re.IGNORECASE)
        if m:
            return tr[:m.start()] + '&' + tr[m.start():]
    m = re.search(r'[A-Za-z]', masked)
    if m:
        return tr[:m.start()] + '&' + tr[m.start():]
    if mn:
        return f"{tr} (&{mn.upper()})"
    return None


def repair_accel(src, tr):
    a_s = accelerators(src)
    if a_s == 0:
        return None
    cand = (tr or '').replace('＆', '&')
    cand = re.sub(r'&[ \t]+(\S)', r'&\1', cand)
    if accelerators(cand) == a_s:
        return cand if cand != (tr or '') else None
    if accelerators(cand) == 0 and a_s == 1:
        f = _reinsert(cand, source_mnemonic(src))
        if f and accelerators(f) == 1:
            return f
    return None


def markup_corrupt(src, t):
    return placeholders(src) != placeholders(t) or htmlnames(src) != htmlnames(t)


def qt_escape(s):
    return (s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
            .replace('"', '&quot;').replace("'", '&apos;'))


def decide(msg):
    """Return ('keep'|'revert'|'fix'|'fixnum', payload)."""
    tr = msg.find('translation')
    src_el = msg.find('source')
    if tr is None or src_el is None or not src_el.text:
        return 'keep', None
    src = src_el.text
    ttype = tr.get('type')
    forms = tr.findall('numerusform')
    texts = [nf.text or '' for nf in forms] if forms else [tr.text or '']
    if ttype in ('obsolete', 'vanished'):
        return 'keep', None
    if ttype == 'unfinished' and not any(texts):
        return 'keep', None
    if forms:
        if any(markup_corrupt(src, t) for t in texts):
            return 'revert', None
        new = [repair_accel(src, t) for t in texts]
        if any(n is not None for n in new):
            return 'fixnum', [n if n is not None else t for n, t in zip(new, texts)]
        return 'keep', None
    # plain
    t = texts[0]
    if markup_corrupt(src, t):
        return 'revert', None
    if accelerators(src) > 0 and accelerators(t) != accelerators(src):
        r = repair_accel(src, t)
        return ('fix', r) if r is not None else ('revert', None)
    return 'keep', None


def revert_block(block):
    block = re.sub(r'<translation\b[^>]*>', '<translation type="unfinished">', block, count=1)
    if '<numerusform>' in block:
        block = re.sub(r'<numerusform>.*?</numerusform>', '<numerusform></numerusform>', block, flags=re.S)
    else:
        block = re.sub(r'(<translation type="unfinished">).*?(</translation>)',
                       lambda m: m.group(1) + m.group(2), block, flags=re.S)
    return block


def fix_block(block, newtext):
    return re.sub(r'(<translation\b[^>]*>).*?(</translation>)',
                  lambda m: '<translation>' + qt_escape(newtext) + '</translation>', block, flags=re.S)


def fixnum_block(block, newforms):
    it = iter(newforms)
    return re.sub(r'<numerusform>.*?</numerusform>',
                  lambda m: '<numerusform>' + qt_escape(next(it)) + '</numerusform>', block, flags=re.S)


def process(path, code):
    root = etree.parse(path, etree.XMLParser(resolve_entities=False)).getroot()
    actions = []
    for m in root.iter('message'):
        if m.find('translation') is None:
            continue
        actions.append(decide(m))
    raw = open(path, encoding='utf-8').read()
    cnt = [0]
    st = dict(keep=0, revert=0, fix=0, fixnum=0)

    def repl(mo):
        i = cnt[0]
        cnt[0] += 1
        act, payload = actions[i]
        st[act] += 1
        if act == 'revert':
            return revert_block(mo.group(0))
        if act == 'fix':
            return fix_block(mo.group(0), payload)
        if act == 'fixnum':
            return fixnum_block(mo.group(0), payload)
        return mo.group(0)

    new = TRANS_BLOCK.sub(repl, raw)
    if cnt[0] != len(actions):
        print(f"{code}: ABORT block/action mismatch {cnt[0]} vs {len(actions)}", flush=True)
        return
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(new)
    print(f"{code}: {st}", flush=True)


def main():
    for code in sys.argv[1:]:
        process(os.path.join(TRD, f'qtcreator_{code}.ts'), code)


if __name__ == '__main__':
    main()
