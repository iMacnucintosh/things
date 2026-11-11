#!/usr/bin/env python3
"""Check that i18n/<lang>.json covers every string of the code (and has nothing left over).

  scripts/check-i18n.py [lang ...]     default: every i18n/*.json

The code is written in English; qsTr("text") / qsTr("text", "context") / tr("text") /
QCoreApplication::translate("Backend", "text") are looked up in i18n/<lang>.json by "text" or
"context\u0004text". Exit 1 if a string is missing or a catalogue entry is unused.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CALL = re.compile(r'\b(?:qsTr|tr)\(\s*"((?:[^"\\]|\\.)*)"(?:\s*,\s*"((?:[^"\\]|\\.)*)")?')
TRANSLATE = re.compile(r'translate\(\s*"[^"]*"\s*,\s*([a-zA-Z_.]*"?)')
# Texts passed as data (the labels of Backend's pacman field table go through translate(..., d.label))
TABLE = re.compile(r'\{"[A-Za-z ]+", "([^"]+)", "(?:text|list|link)"\}')
EXTRA = re.compile(r'translate\("Backend",\s*"((?:[^"\\]|\\.)*)"')


def unescape(s: str) -> str:
    return json.loads(f'"{s}"')


def used() -> set[str]:
    keys: set[str] = set()
    files = list((ROOT / "qml").rglob("*.qml")) + list((ROOT / "src").glob("*.cpp")) + list((ROOT / "src").glob("*.h"))
    for p in files:
        text = p.read_text()
        for m in CALL.finditer(text):
            src, ctx = unescape(m.group(1)), m.group(2)
            keys.add(f"{unescape(ctx)}\x04{src}" if ctx else src)
        for m in EXTRA.finditer(text):
            keys.add(unescape(m.group(1)))
        for m in TABLE.finditer(text):
            keys.add(m.group(1))
    return keys


def main() -> int:
    langs = sys.argv[1:] or [p.stem for p in sorted((ROOT / "i18n").glob("*.json"))]
    need = used()
    bad = 0
    for lang in langs:
        cat = json.loads((ROOT / "i18n" / f"{lang}.json").read_text())
        missing = sorted(need - set(cat))
        unused = sorted(set(cat) - need)
        for k in missing:
            print(f"{lang}: missing: {k!r}")
        for k in unused:
            print(f"{lang}: unused:  {k!r}")
        bad += len(missing) + len(unused)
        print(f"{lang}: {len(cat)} entries, {len(need)} strings in the code")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
