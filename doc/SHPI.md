# SHPI / FSH / QFS – Container- & Texturformat (Aufgabe 1.1)

**Status:** erster belastbarer Stand · **Datum:** 2026-10-14
**Quellen:** `fshtool.c`/`fshtool.h` (v1.22), reale Dateien aus `Game CD/GameData/Track/`,
`Game CD/SaveData/pic16.fsh`, `NFS5 Editing Kit/NFSPUTEX/356_*.fsh`.

> Dieses Format ist das **Kernformat** von NFS5: `.fsh`, `.env` und viele andere Assets
> sind SHPI-Container. Es beherrschen zu können schließt die Mehrheit der Assets auf.

---

## 0. Zusammenfassung

Es gibt **zwei** Ausprägungen desselben Containers:

| Typ | Magic | Beschreibung |
|---|---|---|
| **FSH** (unkomprimiert) | `53 48 50 49` = `"SHPI"` | SHPI-Header direkt am Dateianfang |
| **QFS** (komprimiert) | `10 FB …` | SHPI-Daten mit LZ-artigem Packer umwickelt |

`fshtool` erkennt QFS über `(byte0 & 0xFE) == 0x10 && byte1 == 0xFB` und dekomprimiert
im Speicher; danach beginnt die normale SHPI-Struktur.

---

## 1. QFS-Wrapper (nur bei komprimierten Dateien)

QFS-Header am Dateianfang, danach komprimierter Datenstrom:

| Offset | Länge | Bedeutung |
|---|---|---|
| 0 | 1 | `0x10` (Bit0=1 → `0x11`: längerer Header, Datenstart bei Offset 8 statt 5) |
| 1 | 1 | `0xFB` |
| 2 | 3 | **Ausgabe-Länge** (24-bit **big-endian**) = Größe der dekomprimierten SHPI-Daten |
| 5 (bzw. 8) | … | komprimierter Strom (Decode: siehe `uncompress_data()` in `fshtool.c`) |

Beispiel `coastal.fsh`: `10 FB D8 89 40 F3 …` → Ausgabe ~0xD88940 = ~14,2 MB SHPI
(aus 7,7 MB komprimiert).

---

### 1.1 Größengrenze und Lader des Spiels (2026-10-17)

- Die Ausgabe-Länge hat nur 24 Bit, also höchstens 16.777.215 Byte entpackt.
- `Porsche.exe` liest die Länge in `FUN_00571490` mit genau 3 Byte (über `FUN_0056e290(p+2, 3)`). Der Entpacker `FUN_00594760` macht es genauso. Erkannt werden die Kennungen `10/11`, `18`, `30–35`, `46` jeweils mit `FB` oder `32`. Die EA-Groß-Variante `90 FB` mit 4-Byte-Länge ist **nicht** dabei.
- `fshtool` prüft die Grenze nicht. Bei 1024er-Texturen (street-Test: ~80 MB SHPI) schreibt es einen abgeschnittenen Längenwert, und die Datei lässt sich nicht mehr entpacken.
- **Ein QFS über 16 MB ist für dieses Spiel also nicht möglich**, egal welches Werkzeug packt. Dafür müssten Lader und Entpacker in der Exe gepatcht werden.
- Die Lader `FUN_0056e4f0` und `FUN_0056e3e0` verwenden den Dateipuffer direkt, wenn keine QFS-Kennung vorliegt. Eine **unkomprimierte** `.fsh` (`SHPI` am Anfang) wird damit angenommen. ✅ Im Spiel bestätigt (2026-10-17, coastal 91 MB). Der Spitzenbedarf an RAM ist dabei sogar kleiner, weil der gepackte Puffer entfällt.

## 2. SHPI-Header (`FSH_HDR`, 16 Byte)

| Offset | Länge | Struktur-Feld | Bedeutung |
|---|---|---|---|
| 0 | 4 | `SHPI[4]` | `"SHPI"` |
| 4 | 4 | `filesize` | Gesamtgröße der SHPI-Daten (little-endian) |
| 8 | 4 | `nbmp` | Anzahl der Ressourcen/Objekte |
| 12 | 4 | `dirId[4]` | Verzeichnis-Tag, üblicherweise `"GIMX"` |

Beispiel `pic16.fsh` (6960 Byte, 1 Objekt):
```
53 48 50 49  30 1B 00 00  01 00 00 00  47 49 4D 58
"SHPI"       0x1B30=6960   1 Objekt      "GIMX"
```

## 3. Ressourcen-Verzeichnis (`BMPDIR`, je 8 Byte)

Beginnt direkt nach dem Header (Offset 16), `nbmp` Einträge:

| Offset | Länge | Feld | Bedeutung |
|---|---|---|---|
| 0 | 4 | `name[4]` | 4-Zeichen-Ressourcenname (ggf. mit Leerzeichen gepaddet) |
| 4 | 4 | `ofs` | Byte-Offset dieser Ressource in den SHPI-Daten |

Beispiel: `pic16.fsh` → `4D 4F 33 20` (`"MO3 "`) + `20 00 00 00` (Offset 32).
`356_100.fsh` → `64 65 63 31` (`"dec1"`) + `C0 00 00 00` (Offset 192).

---

## 4. Bitmap-Eintrag (`ENTRYHDR`, 16 Byte) + Anhänge

An jeder `ofs`-Position liegt ein Entry-Header, gefolgt von Palette und Pixeldaten:

| Offset | Länge | Feld | Bedeutung |
|---|---|---|---|
| 0 | 4 | `code` | Bitmap-Typ-Code (s. §6); Bit `0x80` = komprimiert |
| 4 | 2 | `width` | Breite |
| 6 | 2 | `height` | Höhe |
| 8 | 8 | `misc[4]` | 4× short = Hilfs-Positionsdaten (Display-Offsets) |

Danach, je nach Typ:
- bei 8-Bit (`0x7B`): **Palette** (siehe §7) + indizierte Pixeldaten
- bei TrueColor (`0x6D/0x78/0x7D/0x7E/0x7F`): direkte Pixeldaten
- optional weitere Anhänge: Text (`0x69/0x70`), lokale Palette, Binärdaten

---

## 5. Multiscale (Mipmap-Ketten)

NFS5-Track-Texturen liegen als **Multiscale** vor: eine Ressource enthält die Basis-Textur
(Breite/Höhe = 2^n) plus alle kleineren Mip-Stufen (2^(n-1), …, 2^0 = 1×1).

**Namenskonvention beim fshtool-Export:** `NNNN-S.BMP`
- `NNNN` = Ressourcen-Index
- `S` = Stufe, wobei **`S=0` die *kleinste* Stufe (1×1) und `S=max` die *Basis*/Vollauflösung** ist
- `NNNN-Sa.BMP` = Alpha-Kanal der jeweiligen Stufe (falls vorhanden)

Verifiziert an `coastal.fsh`: Objekt 0043 hat Stufen `0…8`, Größen 58 B → 196.662 B,
`0043-8.BMP` = 256×256 (Basis). Objekt 0183: „[0 to 7] up to 128x128“ = Basis 128×128.

---

## 6. Bitmap-Typ-Codes

| Code | Format | Alpha |
|---|---|---|
| `0x6D` | 16-bit 4:4:4:4 | ja |
| `0x78` | 16-bit 0:5:6:5 | nein |
| `0x7B` | 8-bit (indiziert, mit Palette) | (getrennt) |
| `0x7D` | 32-bit 8:8:8:8 | ja |
| `0x7E` | 16-bit 1:5:5:5 | ja |
| `0x7F` | 24-bit 0:8:8:8 | nein |
| `0x60`, `0x61` | NFS6-Demo-Zusätze | – |
| `+0x80` | Bit gesetzt = Bitmap **komprimiert** | – |

## 7. Paletten-Typ-Codes

| Code | Format |
|---|---|
| `0x22` | 24-bit (DOS) |
| `0x24` | 24-bit |
| `0x29` | **16-bit NFS5** |
| `0x2A` | 32-bit |
| `0x2D` | 16-bit |

---

## 8. Alpha-Kanal

- Bei Typen mit Alpha (`0x6D/0x7D/0x7E`) wird die Alpha-Ebene direkt `fshtool`-seitig in ein
  separates **8-Bit-Graustufen-BMP** (`NNNN-Sa.BMP`) exportiert.
- Bei 8-Bit-Bitmaps (`0x7B`) legt `fshtool` eine `ALPHA8.DAT`-Textdatei an (256er-Alpha je Stufe);
  ohne diese Daten wird Alpha als weiß angenommen.
- **Grauwert-Semantik (in-game verifiziert 2026-10-14):**
  
  | Grauwert | Wirkung |
  |---|---|
  | weiß (255) | **deckend/opak** |
  | schwarz (0) | **transparent** |
  
  Beleg: Im Alpha von Objekt `sgn2` (coastal) ist der Schildkörper weiß (deckend) und die
  Fläche außerhalb des Kreises schwarz (transparent). `fshtool` übernimmt beim Lesen die
  „blaue Komponente“ der Graustufen-Palette als Alpha-Wert.

---

## 9. Offene Punkte / nächste Schritte

- [ ] `filesize`-Semantik bei gepackten Ressourcen (Entry-`code` Bit `0x80`) genauer vermessen.
- [ ] `misc[4]`-Bedeutung (Display-Offsets) pro Typ-Code bestimmen.
- [x] `.env`-Dateien verifiziert: sind ebenfalls SHPI-Container (z. B. `coastal.env` =
  `SHPI` + Größe + `nbmp=4`).
- [ ] QFS-Kompressionsstrom (Encode/Decode) vollständig dokumentieren (für eigenen Packer).
