"""Erzeugt carlimit_table.h: alle Code-Stellen, die eine verlegte Tabelle pro Auto ansprechen (VERKEHR.md §6.6).

Eingaben (aus Ghidra, siehe VERKEHR.md §6.5):
  globalrefs.csv  DumpGlobalRefs.java über 0x5D0000..0x6E3000 (Befehlsadresse, Funktion, Wert, Operand, Text)
  listing.tsv     DumpListing.java (Befehlsadresse, Funktion, Länge, Text)
  Porsche.exe     um die Byte-Position des Adress-Operanden im Befehl zu finden

Je Verweis wird der 4-Byte-Wert im Befehl gesucht; er muss genau einmal vorkommen. Zusätzlich prüft eine
Rohsuche über .text, dass kein Vorkommen eines Bereichs außerhalb der erfassten Stellen liegt.
Aufruf: python -I gen_carlimit_table.py <globalrefs.csv[,weitere.csv]> <listing.tsv> <Porsche.exe> <carlimit_table.h>
"""
import bisect
import csv
import re
import struct
import sys

# Name, Anfang, Ende (exklusiv), Elementgröße, Faktor (neue Anzahl = Faktor x MaxCars), Schleifenende als
# Adresse (CMP reg,Ende) wird mit verlegt. Reihenfolge = Bereichsnummer in LFSP.
REGIONS = [
    ("LIST_ALL", 0x5E8B34, 0x5E8B58, 4, 1, False),    # alle Autos
    ("LIST_ALL2", 0x5E8B88, 0x5E8BAC, 4, 1, False),   # Kopie (FUN_004110A0)
    ("LIST_ALL3", 0x5E8BAC, 0x5E8BD0, 4, 1, False),   # Kopie
    ("LIST_AI", 0x5E8AEC, 0x5E8B10, 4, 1, False),     # KI-gesteuert (Gegner, Verkehr, Polizei)
    ("LIST_TRAFFIC", 0x5E8B60, 0x5E8B84, 4, 1, False),
    ("AI_OBJECTS", 0x5E6774, 0x5E6798, 4, 1, False),
    ("SOUND", 0x6563C8, 0x6563E8, 4, 1, False),       # 8 Plätze
    ("DESC", 0x65808C, 0x65B20C, 0x580, 1, False),    # 9 x 0x580
    ("AI_STATES", 0x5E6E38, 0x5E7478, 0xA0, 2, True), # Pool der KI-Zustände (FUN_0040B530), 10 Plätze
    ("AI_STATE_USED", 0x5E7484, 0x5E74AC, 4, 2, True),  # Belegt-Flags dazu
    ("RESULTS", 0x5E75A0, 0x5E8AB8, 0x258, 1, False),     # Ergebnis je Auto (FUN_004B68F0 bis NUMCARS)
    ("BLINK_TIME", 0x60FDA8, 0x60FDCC, 4, 1, False),      # Kartenpunkt-Blinken je Autoplatz (FUN_00433110)
    ("BLINK_COLOR", 0x5CD158, 0x5CD17C, 4, 1, False),     # dazu Farbe (vorbelegte Daten)
]

# Gefaltete Basen: der Compiler hat liste[i - k] zu [i*4 + liste - 4k] zusammengezogen; der Wert fällt dabei in
# einen anderen Bereich. Operand-Adresse -> Bereich, zu dem die Stelle wirklich gehört.
FOLDED = {
    0x4A9593: "LIST_ALL",  # FUN_004A93C0 (Ton): [ESI*4 + 0x5E8AEC], ESI = 0x12/0x13 -> Liste aller Autos [ESI - 0x12]
    0x4A9743: "LIST_ALL",
    0x4A9813: "LIST_ALL",
}

# Stellen, die zwar den Anfangswert tragen, aber das Ende der Tabelle davor meinen (bleiben unverändert).
def excluded(name, value, text):
    lo = [r[1] for r in REGIONS if r[0] == name][0]
    if value == lo and re.match(r"CMP E[A-Z]{2},0x%x$" % lo, text):
        return True  # Anfang dieses Bereichs = Ende der Tabelle davor (z. B. 0x5E75A0: Kehrwerte ab 0x5E7554)
    if name == "SOUND" and re.match(r"CMP E[A-Z]{2},0x6563c[8c]$", text):
        return True  # Schleifenende der Tabelle 0x656180 (Abstand 8, ab 0x656180 bzw. 0x656184; FUN_004A8660 u. a.)
    return False


def sections(exe):
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    count = struct.unpack_from("<H", exe, pe + 6)[0]
    opt = struct.unpack_from("<H", exe, pe + 20)[0]
    out = []
    for i in range(count):
        o = pe + 24 + opt + 40 * i
        name = exe[o:o + 8].rstrip(b"\0").decode()
        vsize, va, rsize, raw = struct.unpack_from("<IIII", exe, o + 8)
        out.append((name, 0x400000 + va, raw, rsize))
    return out


def main():
    refs_path, listing_path, exe_path, out_path = sys.argv[1:5]
    exe = open(exe_path, "rb").read()
    secs = sections(exe)

    def file_offset(va):
        for name, start, raw, size in secs:
            if start <= va < start + size:
                return va - start + raw
        raise ValueError(hex(va))

    lengths = {}
    texts = {}
    for line in open(listing_path, encoding="utf-8"):
        addr, _func, length, text = line.rstrip("\n").split("\t", 3)
        lengths[int(addr, 16)] = int(length)
        texts[int(addr, 16)] = text
    starts = sorted(lengths)

    def containing(va):
        i = bisect.bisect_right(starts, va) - 1
        if i >= 0 and starts[i] <= va < starts[i] + lengths[starts[i]]:
            return starts[i]
        return None

    sites = {}   # Operand-Adresse -> (Wert, Bereich, Text)
    skipped = []
    rows = []
    for path in refs_path.split(","):
        rows += list(csv.reader(open(path, encoding="utf-8")))
    code_ranges = [(start, start + size) for name, start, raw, size in secs if name in (".text", "PACODE")]
    for addr, func, value, _op, text in rows:
        ins, val = int(addr, 16), int(value, 16)
        # nur echter Code; Sprung-/Aufrufziele sind keine Adressoperanden
        if not any(lo <= ins < hi for lo, hi in code_ranges) or re.match(r"(CALL|J[A-Z]+) 0x", text):
            continue
        for idx, (name, lo, hi, _elem, _factor, cmp_end) in enumerate(REGIONS):
            # Schleifenende als Adresse: nur CMP reg,Ende (der Wert ist sonst die nächste Variable)
            is_end = cmp_end and val == hi and re.match(r"CMP E[A-Z]{2},0x%x$" % hi, text) is not None
            if not (lo <= val < hi or is_end):
                continue
            if excluded(name, val, text):
                skipped.append((ins, name, text, val))
                continue
            off = file_offset(ins)
            body = exe[off:off + lengths[ins]]
            needle = struct.pack("<I", val)
            pos = [i for i in range(len(body) - 3) if body[i:i + 4] == needle]
            if len(pos) != 1:
                sys.exit("Stelle %08X (%s): Wert %08X %d-mal im Befehl" % (ins, text, val, len(pos)))
            site = ins + pos[0]
            if site in FOLDED:
                idx = [r[0] for r in REGIONS].index(FOLDED[site])
            if site in sites and sites[site][0] != val:
                sys.exit("Stelle %08X doppelt" % site)
            sites[site] = (val, idx, text, is_end)

    # Rohsuche: jedes Vorkommen in .text muss erfasst oder bewusst ausgelassen sein.
    skipped_sites = set()
    for ins, name, text, val in skipped:
        off = file_offset(ins)
        body = exe[off:off + lengths[ins]]
        for i in range(len(body) - 3):
            if struct.unpack_from("<I", body, i)[0] == val:
                skipped_sites.add(ins + i)
    for name, start, raw, size in secs:
        if name not in (".text",):
            continue
        for off in range(raw, raw + size - 3):
            v = struct.unpack_from("<I", exe, off)[0]
            for rname, lo, hi, _e, _f, _c in REGIONS:
                if lo <= v < hi:
                    va = start + off - raw
                    if va in sites or va in skipped_sites:
                        continue
                    ins = containing(va)
                    # Zufallstreffer: Bytes eines relativen Sprungs/Aufrufs (Opcode + Distanz)
                    if ins is not None and re.match(r"(CALL|J[A-Z]+) 0x", texts[ins]):
                        continue
                    sys.exit("Rohsuche: %08X enthält %08X (%s), nicht erfasst; Befehl %s" % (
                        va, v, rname, "%08X %s" % (ins, texts[ins]) if ins is not None else "keiner"))

    counts = [0] * len(REGIONS)
    for val, idx, _t, _end in sites.values():
        counts[idx] += 1
    with open(out_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("// Erzeugt von gen_carlimit_table.py, nicht von Hand ändern (VERKEHR.md §6.6).\n")
        out.write("// Stellen je Bereich: %s; ausgelassen (Ende der Tabelle davor): %d\n" % (
            ", ".join("%s %d" % (REGIONS[i][0], c) for i, c in enumerate(counts)), len(skipped)))
        out.write("#pragma once\n\n")
        out.write("struct CarLimitSite\n{\n    std::uint32_t site;   // Adresse des 4-Byte-Operanden\n"
                  "    std::uint32_t value;  // erwarteter alter Wert\n    std::uint8_t region;  // Index in kCarLimitRegion*\n"
                  "    std::uint8_t end;     // 1 = Schleifenende (alter Wert = Ende des Bereichs)\n};\n\n")
        out.write("constexpr std::uint32_t kCarLimitRegionCount = %d;\n" % len(REGIONS))
        out.write("constexpr std::uint32_t kCarLimitRegionStart[] = { %s };\n" % ", ".join("0x%06X" % r[1] for r in REGIONS))
        out.write("constexpr std::uint32_t kCarLimitRegionEnd[] = { %s };\n" % ", ".join("0x%06X" % r[2] for r in REGIONS))
        out.write("constexpr std::uint32_t kCarLimitRegionElem[] = { %s };\n" % ", ".join("0x%X" % r[3] for r in REGIONS))
        out.write("constexpr std::uint32_t kCarLimitRegionFactor[] = { %s }; // neue Anzahl = Faktor x MaxCars\n\n"
                  % ", ".join(str(r[4]) for r in REGIONS))
        out.write("const CarLimitSite kCarLimitSites[] = {\n")
        for site in sorted(sites):
            val, idx, text, is_end = sites[site]
            out.write("    { 0x%06X, 0x%06X, %d, %d }, // %s\n" % (site, val, idx, int(is_end), text.replace("\\", "")))
        out.write("};\n")
    print("Stellen:", len(sites), dict((REGIONS[i][0], c) for i, c in enumerate(counts)), "ausgelassen:", len(skipped))


if __name__ == "__main__":
    main()
