"""Erzeugt windows_table.h aus dem Patch "Window Opener" (owindow = offen, cwindow = Original).

Je Fahrzeug und Style wird der letzte Wert von geometry 35/47 genommen (die Engine überschreibt
frühere Einträge, FENSTER-VERDECK.md §2). Eingetragen wird nur, was sich gegenüber dem Original ändert.
Aufruf: python -I gen_windows_table.py <Mod-Window Opener/GameData> <windows_table.h>
"""
import re
import sys
from pathlib import Path

PARTS = ("35", "47")


def window_values(text):
    styles = {}
    for sec in re.split(r"\n(?=\[)", text.replace("\r\n", "\n")):
        m = re.match(r"\[style(\d+)\]", sec)
        if not m:
            continue
        geo = dict(re.findall(r"^geometry(\d+)=(\d+)", sec, re.M))
        typ = dict(re.findall(r"^type(\d+)=(\d+)", sec, re.M))
        name = re.search(r"^name=(.*)$", sec, re.M)
        vals = {}
        for idx in sorted(geo, key=int):
            if geo[idx] in PARTS:
                vals[geo[idx]] = int(typ.get(idx, 0))
        styles[int(m.group(1))] = (name.group(1).strip() if name else "", vals)
    return styles


def main(src, dst):
    src = Path(src)
    lines = []
    for f in sorted((src / "owindow").glob("*.tpg")):
        car = f.stem.lower()
        opened = window_values(f.read_text(encoding="latin-1"))
        closed = window_values((src / "cwindow" / f.name).read_text(encoding="latin-1"))
        for style in sorted(opened):
            name, ov = opened[style]
            cv = closed.get(style, ("", {}))[1]
            if all(ov.get(p, 0) == cv.get(p, 0) for p in PARTS):
                continue
            outer, inner = (ov.get(p, cv.get(p, 0)) for p in PARTS)
            key = f'"{car}.{style}",'
            lines.append(f"    {{ {key:<14} {outer}, {inner} }}, // {name}")

    head = """// Erzeugt mit gen_windows_table.py aus "Mod-Window Opener" (owindow vs. cwindow), nicht von Hand ändern.
// Mesh-Variante der Seitenscheiben im offenen Zustand: Fahrzeug.N = außen (Teil 35), innen (Teil 47),
// N wie [styleN] in der .tpg; 0 = Teil nicht ändern. lfsp.ini [Windows] kann einzelne Einträge ersetzen.
#pragma once

struct WindowTableEntry
{
    const char * key;
    int outer;
    int inner;
};

constexpr WindowTableEntry kWindowTable[] = {
"""
    Path(dst).write_text(head + "\n".join(lines) + "\n};\n", encoding="utf-8")
    print(f"{len(lines)} Eintraege -> {dst}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
