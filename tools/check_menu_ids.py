#!/usr/bin/env python3
"""Find ImGui widgets that share a label inside one menu function.

ImGui keys a widget by its label within the current ID stack, so two widgets
with the same label in the same window are the same widget: they fight over one
piece of state and the second one usually cannot be clicked at all. It has
happened twice - "Show it" on the vitals page, then "Corner" when the ammo
readout gained one - and neither showed up as a compiler error or a bad string.

Widgets wrapped in igPushID/igPopID are scoped and counted separately, which is
how the tracking page gets away with three "Gesture" combos.
"""
import io
import os
import re
import sys

MENU = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "plugin", "src", "Menu.cpp")

# widgets that take a visible label and therefore an ID derived from it
LABELLED = (
    "igCombo_Str_arr", "igCheckbox", "igSliderFloat", "igSliderInt",
    "igDragFloat", "igDragInt", "igButton", "igSmallButton", "igInputText",
    "igColorEdit3", "ColourPicker", "igSelectable_Bool", "igRadioButton_Bool",
)
CALL = re.compile(r"\b(" + "|".join(LABELLED) + r")\s*\(\s*(T\(\s*)?\"((?:[^\"\\]|\\.)*)\"")
FUNC = re.compile(r"^\t\tvoid (Draw\w+)\s*\(")


def main() -> int:
    lines = io.open(MENU, encoding="utf-8", newline="").read().replace("\r\n", "\n").split("\n")

    func = "(file scope)"
    depth = 0            # igPushID depth
    seen = {}            # (func, depth-path, label) -> first line
    clashes = []

    path = []
    for n, line in enumerate(lines, 1):
        m = FUNC.match(line)
        if m:
            func = m.group(1)
            path = []
            seen = {k: v for k, v in seen.items() if k[0] != func}

        if "igPushID" in line:
            path.append(n)
        if "igPopID" in line and path:
            path.pop()

        for hit in CALL.finditer(line):
            label = hit.group(3)
            if not label or label.startswith("##"):
                continue          # hidden labels carry no ID of their own
            key = (func, tuple(path), label)
            if key in seen:
                clashes.append((func, label, seen[key], n))
            else:
                seen[key] = n

    if not clashes:
        print("no duplicate widget labels")
        return 0

    print("DUPLICATE WIDGET LABELS - the second of each cannot be clicked:\n")
    for func, label, first, second in clashes:
        print('  %-18s "%s"  lines %d and %d' % (func, label, first, second))
    return 1


if __name__ == "__main__":
    sys.exit(main())
