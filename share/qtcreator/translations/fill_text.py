#!/usr/bin/env python3
"""Fill untranslated (type="unfinished", empty) entries of Qt .ts files via
Google, by SURGICAL TEXT substitution so the rest of the file (and Qt's
&apos;/&quot; escaping, indentation, locations) stays byte-for-byte identical.

- lxml is used only to read each untranslated <source> (clean, unescaped).
- Writing replaces only the empty <translation> blocks, Qt-escaping the text.
- Numerus: every existing <numerusform> is filled (keeps the language's count).
- One serial Google connection; resumable (re-running only sees still-empty).
"""
import sys
import os
import time
import re
import translators as ts
from lxml import etree

TRD = r'C:\github\openmv-ide\qt-creator\share\qtcreator\translations'
EMPTY_TR = re.compile(
    r'<translation type="unfinished">\s*(?:<numerusform>\s*</numerusform>\s*)*</translation>')


def qt_escape(s):
    return (s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
            .replace('"', '&quot;').replace("'", '&apos;'))


# Qt locale code -> Google Translate code (only those that differ)
GMAP = {'he': 'iw', 'nn': 'no', 'zh_CN': 'zh-CN', 'zh_TW': 'zh-TW',
        'pt_BR': 'pt', 'pt_PT': 'pt-PT'}


def gtrans(s, code):
    g = GMAP.get(code, code)
    for _ in range(6):
        try:
            out = ts.google(s, 'en', g)
            if out is not None:
                return out
        except Exception as e:
            sys.stderr.write(f"  retry({code}): {repr(e)[:70]}\n")
            time.sleep(2)
    return None


def sources_in_order(path):
    root = etree.parse(path, etree.XMLParser(resolve_entities=False)).getroot()
    out = []
    for m in root.iter('message'):
        tr = m.find('translation')
        src = m.find('source')
        if tr is None or src is None or not src.text:
            continue
        if tr.get('type') != 'unfinished':
            continue
        forms = tr.findall('numerusform')
        texts = [nf.text or '' for nf in forms] if forms else [tr.text or '']
        if any(texts):
            continue
        out.append(src.text)
    return out


def fill(path, code):
    srcs = sources_in_order(path)
    if not srcs:
        print(f"{code}: nothing to do", flush=True)
        return 0, 0
    trans = []
    failed = 0
    for i, s in enumerate(srcs):
        t = gtrans(s, code)
        if t is None:
            failed += 1     # leave unfinished (English fallback at runtime, retryable)
        trans.append(t)
        if (i + 1) % 40 == 0:
            print(f"  {code}: {i+1}/{len(srcs)}", flush=True)

    raw = open(path, encoding='utf-8').read()
    cnt = [0]

    def repl(mo):
        i = cnt[0]
        cnt[0] += 1
        if trans[i] is None:
            return mo.group(0)          # failed -> leave unfinished
        esc = qt_escape(trans[i])
        block = mo.group(0).replace(' type="unfinished"', '')
        if '<numerusform>' in block:
            block = re.sub(r'<numerusform>\s*</numerusform>',
                           lambda m: f'<numerusform>{esc}</numerusform>', block)
        else:
            block = re.sub(r'(<translation>)\s*(</translation>)',
                           lambda m: m.group(1) + esc + m.group(2), block)
        return block

    new = EMPTY_TR.sub(repl, raw)
    if cnt[0] != len(srcs):
        print(f"{code}: ABORT match/source mismatch {cnt[0]} vs {len(srcs)}", flush=True)
        return len(srcs), 0
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(new)
    print(f"{code}: filled {len(srcs)} (failed/English={failed})", flush=True)
    return len(srcs), len(srcs) - failed


def main():
    for code in sys.argv[1:]:
        path = os.path.join(TRD, f'qtcreator_{code}.ts')
        t0 = time.time()
        fill(path, code)
        print(f"{code}: done in {time.time()-t0:.0f}s", flush=True)


if __name__ == '__main__':
    main()
