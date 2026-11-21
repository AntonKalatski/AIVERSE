#!/usr/bin/env python3
"""No genre in engine code (CTest genre_free_engine; owner rule NO GENRE SPLIT).

Code under engine/ (comments and string literals stripped) must not use genre-host names: identifiers starting with
Rts / Fps, the namespaces rts:: / fps::, isFps / isRts. Known debt is listed below per file with its count; a file
not listed, or a count above its limit, fails. When debt shrinks, lower the limit in the same commit (a limit above
the real count also fails, so the list only shrinks).

Usage: check_engine_genre_free.py [REPO_ROOT]"""
import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PATTERN = re.compile(r"\b(Rts[A-Z]\w*|Fps[A-Z]\w*|isFps|isRts|fps::\w+|rts::\w+)\b")
# Debt removed by no-genre stages C/D (engine/net still runs on the legacy hosts).
DEBT = {
    "engine/net/authority/Session.cpp": 27,
    "engine/net/authority/Session.h": 15,
    "engine/net/city/CityNet.cpp": 5,
    "engine/net/city/CityMirror.cpp": 3,
    "engine/net/city/CityNet.h": 2,
}


def strip(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', text)


errors = []
seen = {}
for base, _, files in os.walk(os.path.join(ROOT, "engine")):
    for name in files:
        if not name.endswith((".h", ".hpp", ".cpp", ".mm", ".inc")):
            continue
        path = os.path.join(base, name)
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        with open(path, encoding="utf-8", errors="replace") as f:
            found = PATTERN.findall(strip(f.read()))
        if not found:
            continue
        seen[rel] = len(found)
        limit = DEBT.get(rel)
        if limit is None:
            errors.append(f"{rel}: genre names in engine code: {sorted(set(found))[:6]}")
        elif len(found) > limit:
            errors.append(f"{rel}: {len(found)} genre names, debt limit {limit}")
for rel, limit in DEBT.items():
    if seen.get(rel, 0) < limit:
        errors.append(f"{rel}: debt shrank to {seen.get(rel, 0)} (limit {limit}): lower the limit in check_engine_genre_free.py")
for e in errors:
    print("FAIL:", e)
print(f"genre-free engine: {len(seen)} files with debt, {sum(seen.values())} names")
sys.exit(1 if errors else 0)
