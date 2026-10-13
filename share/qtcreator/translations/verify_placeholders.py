#!/usr/bin/env python3
"""Verify machine-translated Qt .ts files did not butcher format placeholders,
HTML tags, or accelerator markers. Compares each <source> against its
<translation> (or each <numerusform>). Reports mismatches per file."""
import sys
import re
import xml.etree.ElementTree as ET

# Qt/printf-style placeholders that MUST survive translation byte-for-byte
#   %1..%99, %L1 (localized), %n / %Ln (numerus count), %s %d etc.
PLACEHOLDER = re.compile(r'%(?:L?\d+|L?n|[sdSioxXfgeEcup%])')
HTML_TAG = re.compile(r'</?[A-Za-z][^>]*>')


def tokens(text):
    if text is None:
        text = ''
    ph = sorted(PLACEHOLDER.findall(text))
    tags = sorted(t.lower() for t in HTML_TAG.findall(text))
    amp = text.count('&')
    return ph, tags, amp


def check(path):
    tree = ET.parse(path)
    root = tree.getroot()
    total = translated = ph_bad = tag_bad = amp_bad = 0
    issues = []
    for msg in root.iter('message'):
        src = msg.find('source')
        if src is None:
            continue
        src_text = src.text or ''
        tr = msg.find('translation')
        if tr is None:
            continue
        ttype = tr.attrib.get('type')
        total += 1
        # collect translated strings (plain or numerus forms)
        forms = [nf.text or '' for nf in tr.findall('numerusform')]
        if not forms:
            forms = [tr.text or '']
        # skip still-untranslated (unfinished + empty)
        if ttype == 'unfinished' and not any(forms):
            continue
        translated += 1
        s_ph, s_tags, s_amp = tokens(src_text)
        for f in forms:
            t_ph, t_tags, t_amp = tokens(f)
            problems = []
            if s_ph != t_ph:
                problems.append(f"placeholders {s_ph} -> {t_ph}")
                ph_bad += 1
            if s_tags != t_tags:
                problems.append(f"html {s_tags} -> {t_tags}")
                tag_bad += 1
            if s_amp != t_amp:
                problems.append(f"amp {s_amp} -> {t_amp}")
                amp_bad += 1
            if problems:
                issues.append((src_text, f, problems))
    return total, translated, ph_bad, tag_bad, amp_bad, issues


def main():
    show = '--show' in sys.argv
    files = [a for a in sys.argv[1:] if not a.startswith('--')]
    for path in files:
        total, translated, ph_bad, tag_bad, amp_bad, issues = check(path)
        print(f"\n=== {path} ===")
        print(f"  messages={total} translated={translated} "
              f"placeholder_mismatch={ph_bad} html_mismatch={tag_bad} amp_mismatch={amp_bad}")
        if show:
            for src, tr, probs in issues[:60]:
                print(f"  ! {probs}")
                print(f"      SRC: {src!r}")
                print(f"      TR : {tr!r}")
            if len(issues) > 60:
                print(f"  ... {len(issues) - 60} more")


if __name__ == '__main__':
    main()
