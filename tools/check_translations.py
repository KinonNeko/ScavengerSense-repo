#!/usr/bin/env python3
"""Check the Chinese menu translation against the strings the code uses.

Collects every English string the menu can show - T("...") and Help("...")
calls, including multi-line concatenated literals, plus the combo-box string
arrays fed through Translated() - and compares them with the keys in
mod/SKSE/Plugins/ScavengerSense_chinese.ini.

The INI is keyed by the exact English text with \\n kept as two characters
and = escaped as \\=, which is exactly what this script emits for missing
entries - paste, translate the right-hand side, done.

Exit code 1 when anything is missing, so it can gate a release.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "plugin", "src")
INI = os.path.join(ROOT, "mod", "SKSE", "Plugins", "ScavengerSense_chinese.ini")

# One C string literal, possibly split over lines: "a" "b" "c"
LITERAL = r'"(?:[^"\\]|\\.)*"'
CONCAT = rf"{LITERAL}(?:\s*{LITERAL})*"


def unescape(a_raw: str) -> str:
    out, i = [], 0
    while i < len(a_raw):
        c = a_raw[i]
        if c == "\\" and i + 1 < len(a_raw):
            n = a_raw[i + 1]
            out.append({"n": "\n", "t": "\t", '"': '"', "=": "=", "\\": "\\"}.get(n, "\\" + n))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def joined(a_concat: str) -> str:
    return unescape("".join(re.findall(r'"((?:[^"\\]|\\.)*)"', a_concat)))


def collect() -> set:
    wanted = set()
    for name in sorted(os.listdir(SRC)):
        if not name.endswith((".cpp", ".h")):
            continue
        text = open(os.path.join(SRC, name), encoding="utf-8", errors="replace").read()

        for call in ("T", "Help"):
            for m in re.finditer(rf"\b{call}\(\s*({CONCAT})", text):
                wanted.add(joined(m.group(1)))

        # Combo entries: const char* arrays later passed through Translated().
        # Menu.cpp only - the arrays in Config.cpp are INI vocabulary, never
        # shown to the player. Two menu arrays stay English by policy: key cap
        # names (the cap itself is printed with them) and the INI language
        # codes (must stay ASCII to name the translation file).
        if name == "Menu.cpp":
            for m in re.finditer(r"const char\*(?: const)?\s+(k\w+)\[\]\s*=\s*\{([^;]*?)\};", text, re.S):
                if m.group(1) in ("kGamepadNames", "kKeyNames", "kLanguages"):
                    continue
                for lit in re.findall(LITERAL, m.group(2)):
                    wanted.add(joined(lit))

    # Housekeeping: IDs and formats are not player-facing text.
    return {w for w in wanted if w and not w.startswith("##") and "%" not in w}


def ini_keys() -> set:
    keys = set()
    if not os.path.exists(INI):
        return keys
    for line in open(INI, encoding="utf-8-sig"):
        line = line.rstrip("\n")
        if not line or line.lstrip().startswith((";", "#", "[")):
            continue
        eq, i = -1, 0
        while i < len(line):
            if line[i] == "\\":
                i += 2
                continue
            if line[i] == "=":
                eq = i
                break
            i += 1
        if eq > 0:
            keys.add(unescape(line[:eq].strip()))
    return keys


def as_key(a_text: str) -> str:
    return a_text.replace("\\", "\\\\").replace("\n", "\\n").replace("=", "\\=")


if __name__ == "__main__":
    wanted = collect()
    have = ini_keys()

    missing = sorted(wanted - have)
    stale = sorted(have - wanted)

    print(f"code wants {len(wanted)} strings, ini has {len(have)}")
    if missing:
        print(f"\nMISSING ({len(missing)}) - paste and translate:")
        for text in missing:
            print(f"{as_key(text)}=")
    if stale:
        print(f"\nstale ({len(stale)}) - in the ini, no longer in the code:")
        for text in stale:
            preview = text.replace("\n", "\\n")
            print(f"  {preview[:76]}")

    sys.exit(1 if missing else 0)
