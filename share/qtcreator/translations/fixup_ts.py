#!/usr/bin/env python3
"""Post-process machine-translated Qt .ts files.

For each TRANSLATED message, compare it against its English <source> and:
  * placeholder corruption (%1/%n/%Ln set changed)  -> revert to English fallback
  * HTML tag set changed                            -> revert to English fallback
  * lost Qt accelerator (source has one '&X', tr has none) -> re-insert '&'
  * other accelerator-count mismatch                -> revert to English fallback
  * dropped/changed HTML entity only (&nbsp; etc.)  -> keep (harmless)

Also normalise numerus form counts (auto_trans.py hardcodes 2):
  id/th/vi -> 1 form, ro -> 3 forms  (duplicate the translated text).

"Revert to English fallback" = mark the <translation> type="unfinished" and
clear its text so lrelease/Qt fall back to the source string (correct markup).
"""
import sys
import re
import xml.etree.ElementTree as ET

PLACEHOLDER = re.compile(r'%(?:L?\d+|L?n|[sdSioxXfgeEcup])')
HTML_TAG = re.compile(r'</?[A-Za-z][^>]*>')
ENTITY = re.compile(r'&(?:amp|nbsp|lt|gt|quot|apos|copy|reg|deg|times|mdash|ndash|hellip|#\d+|#x[0-9A-Fa-f]+);')

# CLDR plural form counts for the languages we add
FORMS = {'id': 1, 'th': 1, 'vi': 1, 'ro': 3}


def placeholders(t):
    return sorted(PLACEHOLDER.findall(t or ''))


def tags(t):
    return sorted(x.lower() for x in HTML_TAG.findall(t or ''))


def accelerators(t):
    """Count Qt accelerators: '&' + any non-space char (incl. non-Latin
    scripts like Thai), excluding HTML entities, '&&' and '& '."""
    if not t:
        return 0
    s = ENTITY.sub('', t)         # drop HTML entities
    s = s.replace('&&', '')       # drop literal ampersands
    return len(re.findall(r'&[^&\s]', s))


def source_mnemonic(src):
    """The alnum char immediately after the source's '&' accelerator."""
    s = ENTITY.sub('', src).replace('&&', '')
    m = re.search(r'&([0-9A-Za-z])', s)
    return m.group(1) if m else None


def _mask_markup(tr):
    """Return a copy of `tr` with placeholders/tags/entities blanked to spaces,
    so accelerator insertion never lands inside a %n / <tag> / &entity;."""
    masked = list(tr)
    for rx in (PLACEHOLDER, HTML_TAG, ENTITY):
        for m in rx.finditer(tr):
            for i in range(m.start(), m.end()):
                masked[i] = ' '
    return ''.join(masked)


def reinsert_accel(tr, mnemonic):
    """Re-add a single Qt accelerator to the translation, never inside markup.

    1. If the translation contains the source mnemonic letter (Latin,
       case-insensitive) outside markup, put '&' before it -> same hotkey.
    2. Else if it has any Latin letter outside markup, put '&' before the first.
    3. Else (non-Latin script, e.g. Thai), append ' (&X)' with the source
       mnemonic -- the convention Qt uses for CJK/Thai translations.
    """
    masked = _mask_markup(tr)
    if mnemonic:
        m = re.search(re.escape(mnemonic), masked, re.IGNORECASE)
        if m:
            i = m.start()
            return tr[:i] + '&' + tr[i:]
    m = re.search(r'[A-Za-z]', masked)
    if m:
        i = m.start()
        return tr[:i] + '&' + tr[i:]
    if mnemonic:
        return f"{tr} (&{mnemonic.upper()})"
    return None


def classify(src, tr):
    """Return ('keep', None) | ('revert', None) | ('accel', fixed_text)."""
    if placeholders(src) != placeholders(tr):
        return 'revert', None
    if tags(src) != tags(tr):
        return 'revert', None
    a_s, a_t = accelerators(src), accelerators(tr)
    if a_s == a_t:
        return 'keep', None
    if a_s == 1 and a_t == 0:
        fixed = reinsert_accel(tr, source_mnemonic(src))
        if fixed is not None and accelerators(fixed) == 1:
            return 'accel', fixed
        return 'revert', None
    return 'revert', None


def revert(tr):
    """Mark unfinished and clear all text so source is used as fallback."""
    forms = tr.findall('numerusform')
    if forms:
        for nf in forms:
            nf.text = ''
    else:
        tr.text = ''
    tr.set('type', 'unfinished')


def main():
    path = sys.argv[1]
    m = re.search(r'_([a-zA-Z_]+)\.ts$', path)
    lang = m.group(1) if m else ''
    # 0 = unknown language: keep whatever numerus form count is already present
    nforms = FORMS.get(lang, 0)
    tree = ET.parse(path)
    root = tree.getroot()

    stats = dict(translated=0, kept=0, accel_fixed=0, reverted=0, numerus_fixed=0)
    for msg in root.iter('message'):
        src_el = msg.find('source')
        tr = msg.find('translation')
        if src_el is None or tr is None:
            continue
        src = src_el.text or ''
        is_numerus = msg.attrib.get('numerus') == 'yes'
        forms = tr.findall('numerusform')
        # still untranslated -> leave as-is
        ttype = tr.attrib.get('type')
        has_text = (any(nf.text for nf in forms) if forms else bool(tr.text))
        if ttype == 'unfinished' and not has_text:
            continue
        stats['translated'] += 1

        # --- numerus: normalise form count, then classify on form[0] ---
        if is_numerus:
            text = forms[0].text if forms and forms[0].text else (tr.text or '')
            action, fixed = classify(src, text)
            if action == 'revert':
                revert(tr)
                stats['reverted'] += 1
                continue
            if action == 'accel':
                text = fixed
                stats['accel_fixed'] += 1
            else:
                stats['kept'] += 1
            # rebuild exactly `nforms` numerusform children with `text`.
            # nforms==0 -> unknown language: keep the existing form count.
            target = nforms if nforms else max(1, len(forms))
            if len(forms) != target:
                stats['numerus_fixed'] += 1
            for nf in list(forms):
                tr.remove(nf)
            if tr.text:
                tr.text = None
            for _ in range(target):
                ET.SubElement(tr, 'numerusform').text = text
            continue

        # --- plain message ---
        action, fixed = classify(src, tr.text or '')
        if action == 'keep':
            stats['kept'] += 1
        elif action == 'accel':
            tr.text = fixed
            stats['accel_fixed'] += 1
        else:
            revert(tr)
            stats['reverted'] += 1

    tree.write(path, encoding='utf-8', xml_declaration=True)
    # restore Qt's two-line header (ET writes a single-line declaration)
    with open(path, 'r+', encoding='utf-8') as f:
        lines = f.readlines()
        lines[0] = '<?xml version="1.0" encoding="utf-8"?>\n'
        if not lines[1].startswith('<!DOCTYPE'):
            lines.insert(1, '<!DOCTYPE TS>\n')
        f.seek(0)
        f.writelines(lines)
        f.truncate()
    print(f"{path} (lang={lang}, forms={nforms}): {stats}")


if __name__ == '__main__':
    main()
