# FSH-Repack – funktionale Round-Trip-Verifikation (Aufgabe 1.6)

**Status:** ✅ bestanden · **Datum:** 2026-10-14 · **Gate für Phase 2 freigegeben**

## 1. Zweck

Nachweis, dass `fshtool` eine NFS5-FSH/QFS-Datei **funktional** round-trip kann:
entpacken → bearbeiten → repacken → **vom Spiel korrekt geladen und dargestellt** – und dabei
**pixel-treu** ist (nur die tatsächlich bearbeiteten Texturen ändern sich).

## 2. Werkzeug

| | |
|---|---|
| Tool | `fshtool` (Denis Auroux), v1.22 |
| Binary | `NFS5 Editing Kit/FSHTool/fshtool.exe` |
| Quelle | `OpenNFS-0.3-sourcecode/tools/fshtool.c` + `fshtool.h` |
| NFS5-Support | Palette-Typ `0x29`, Bitmap-Typ `0x6D`, komprimierte + Multiscale-Bitmaps |

## 3. Befehlszeile

```
# Entpacken  (erzeugt <dir>/NNNN-S.BMP (+ NNNN-Sa.BMP für Alpha) und <dir>/INDEX.FSH)
fshtool.exe <datei>.fsh [<verzeichnis>]

# Repacken  (liest INDEX.FSH, schreibt/erzeugt die FSH-Datei)
fshtool.exe <verzeichnis>/INDEX.FSH [<zieldatei>.fsh]
```

## 4. Testdatei

| Feld | Wert |
|---|---|
| Datei | `Game CD/GameData/Track/coastal.fsh` (Original-Backup: `coastal - Kopie.fsh`) |
| Container | **QFS-komprimiert** (Header `10 FB …`, s. `SHPI.md` §2) |
| Größe Original | 7.725.040 Byte |
| MD5 Original | `e52b513c57e6459bc64c9c0c37debb48` |
| Inhalt | 2.854 Textur-BMPs (Track „coastal“, inkl. Alpha-Kanälen + Multiscale-Mips) |

## 5. Durchgeführte Edits

| Datei | Auflösung | BPP | Änderung |
|---|---|---|---|
| `0043-8.BMP` | 256×256 | 24 | grüne Striche |
| `0001-6.BMP` | 64×64 | 24 | Farbfeld (olivgrün) |

> Beide Dateien sind die **Basis-/Vollauflösungsstufe** ihrer Multiscale-Kette
> (`S=max`; zur Namenslogik siehe `SHPI.md` §5).

## 6. Ergebnis

| Feld | Wert |
|---|---|
| Repack | `Game CD/GameData/Track/coastal.fsh` |
| Größe Repack | 8.023.051 Byte |
| MD5 Repack | `54fc4baefad77666e1cf0b6953fe5cc6` |
| Spiel | lädt die Datei, **beide** Edits sichtbar und farbrichtig dargestellt |

## 7. Verifikation auf Pixelebene

Methode: Original-Backup **und** Repack jeweils frisch mit `fshtool` extrahiert,
dann alle 2.854 BMPs byte-weise verglichen.

| Vergleichsgruppe | Ergebnis |
|---|---|
| Bilddaten (Pixel) | identisch **außer** `0043-8.BMP` + `0001-6.BMP` (= die 2 Edits) |
| Alpha-Kanäle (`-a.BMP`) | **byte-identisch** |
| Palette `0x29` | **byte-identisch** |
| Multiscale-Mips | **byte-identisch** |
| `INDEX.FSH` | differiert → nur Metadaten (`BUFSZ`), keine Bilddaten |

## 8. Wichtige Erkenntnisse / Grenzen

1. **QFS ist im Repack *nicht* byte-stabil.** `fshtool` rekomprimiert mit eigenem
   Kompressor → Repack war ~3,9 % größer (8,02 MB vs. 7,73 MB). Das ist erwartet und
   **kein** Fehler. Abnahmekriterium für QFS ist daher:
   *„dekomprimierte Payload identisch (modulo Edits)“*, **nicht** *„Datei byte-identisch“*.
2. `fshtool` akzeptiert nur **unkomprimierte** BMPs (8-Bit mit Palette / 24-Bit TrueColor) –
   kein RLE, kein PNG.
3. Multiscale-Regeln sind strikt (Fehlermeldung „Compressed multiscale bitmaps not allowed“
   etc., siehe `OpenNFS-…/tools/readme.txt`).
4. `INDEX.FSH` ist menschenlesbar editierbar, aber **whitespace-sensitiv** (keine Leerzeilen,
   keine zusätzlichen Leerzeichen).
5. **⚠️ fshtool trunkiert die Zieldatei sofort.** Beim Repack wird die Ausgabedatei mit
   `"wb"` geöffnet (Trunkieren) *bevor* die BMPs verarbeitet werden. Bricht der Lauf ab
   (z. B. ungültiges Alpha-BMP), bleibt eine **leere Zieldatei** zurück. → **Immer vorher
   Backup anlegen** und nach einem Fehlschlag das Ziel prüfen.
6. **⚠️ Alpha-BMPs müssen 8-Bit-Graustufen sein.** Eine 24-Bit-RGB-Datei als Alpha führt zum
   Abbruch `Alpha channel is not a 8-bit BMP !`. Transparenz editieren = in der `-a.BMP`
   **Graustufen** malen (weiß = deckend, schwarz = transparent, s. `SHPI.md` §8).

## 9. Offener/fachlicher Hinweis für Phase 2

Die zwei editierten Texturen sind 24-Bit (Typ `0x7F`) und tragen **keinen Alpha-Kanal**.
Für HD-Texturen mit Transparenz (Typ `0x6D`/`0x7D`/`0x7E`) muss zusätzlich der
Alpha-Round-Trip (`-a.BMP`) an einem passenden Beispiel verifiziert werden – hier bereits
*byte-identisch* bestätigt, aber noch nicht *inhaltlich verändert* getestet.

## 10. Strategie-Entscheidung: FSH-Repack statt Laufzeit-Ersatz (3T.0, 2026-10-16)

👤 hat für HD-Texturen den Weg **F (FSH-Repack)** gewählt. Die Alternative war **R (Laufzeit-Ersatz in dx11z)**.

| | **R – Laufzeit-Ersatz in dx11z** | **F – FSH repacken** ✅ gewählt |
|---|---|---|
| Prinzip | `TextureUpdate` bildet einen Hash über die Originaldaten; liegt eine HD-Datei unter diesem Hash, wird sie stattdessen als ARGB8888 mit eigenen Mips hochgeladen | HD-Bitmaps per `fshtool` in die Original-`.fsh` zurückpacken |
| Alpha-Qualität | volle 8 Bit | Strecke als `7D` → ARGB8888, volle 8 Bit; palettierte Fahrzeugtexturen (`7B`) → ARGB4444, 4 Bit (§11.3) |
| Größengrenzen | nur D3D11 | im Rennen bisher max. 256×256 beobachtet; `fshtool` kann 512² packen (§11), ob die Engine sie nutzt, ist offen |
| Spieldateien | unverändert | werden geändert (Repack ist geprüft, §6/§7) |
| Risiken | dynamisch beschriebene Texturen (Video, Render-to-Texture) dürfen nicht ersetzt werden; Hash-Aufwand pro Upload | Engine verwirft große Bitmaps oder stürzt ab |

**Offen für Phase 2:** veränderter Alpha-Inhalt (§9) und Größen über 256×256 (`PLAN.md` 2.0).

## 11. Texturen über 256×256 (Aufgabe 2.0, 2026-10-16)

### 11.1 Ursache des fehlgeschlagenen 512er-Repacks

Ein Repack mit einer 512×512-Textur bricht in `fshtool` ab mit:

```
FSH data buffer full. Increase BUFSZ parameter in INDEX.FSH
```

`fshtool` schreibt beim Entpacken die **exakte** Größe der unkomprimierten FSH-Daten als `BUFSZ` in `INDEX.FSH` (coastal: `14690912`). Jede vergrößerte Textur sprengt diesen Puffer. Die Zieldatei ist danach leer (§8, Punkt 5). Mit einem Fehler im Spiel hat das nichts zu tun. **Abhilfe:** `BUFSZ` großzügig erhöhen (hier `20000000`).

### 11.2 Durchgeführter Test

| Schritt | Ergebnis |
|---|---|
| Basis | Original `coastal - Kopie.fsh` (MD5 `e52b513c…`), entpackt nach `work/extract/coastal-orig/` (`work/extract/` ist per `.gitignore` ausgeschlossen) |
| Vergrößern | Objekt `0043` (`sgn2`, Schild, Typ `0x7D` mit Alpha) mit `work/hd/upscale_mbp.py coastal-512 43 --mark`: alle Stufen per Nearest-Neighbor verdoppelt, neue Kette `x9 512 512` (Stufen 0–9), oberste Stufe mit **magentafarbenen Diagonalstreifen** markiert, Alpha nur verdoppelt (Inhalt unverändert) |
| Repack | mit `BUFSZ 20000000` erfolgreich: 15.239.488 Byte FSH-Daten, QFS 8.064.027 Byte |
| Rück-Extraktion | `fshtool` erkennt `'sgn2' [7D] -> multiscale '0043-[0 to 9].BMP' (up to 512x512)`; **Pixel und Alpha aller Stufen identisch** (nur BMP-Header-Felder weichen ab) |
| Installiert | `Game CD/GameData/Track/coastal.fsh` (MD5 `e281f772c37b5d01b16049194449af26`). Der vorherige Stand des Nutzers ist gesichert als `coastal.fsh.usertry` |
| Im Spiel | ✅ 2026-10-16: Schild zeigt die Magenta-Streifen der 512er-Stufe (`doc/screenshots/2.0/2.0-A_sign_512.png`). Die Engine nutzt also Stufen über 256×256. Ein Log dieser Fahrt wurde nicht gesichert |

### 11.3 Upload-Format und FSH-Typ (korrigiert 2026-10-16)

*Die erste Fassung dieses Abschnitts schloss aus „coastal ist komplett `0x7D`, hochgeladen wird überwiegend ARGB4444“, dass die Engine alle Texturen umwandelt. Das war voreilig: Die 4444-Texturen stammen aus anderen Dateien.*

**Typen je Ordner** (alle `.fsh` in `Game CD/GameData` mit `fshtool` gezählt):

| Ordner | Dateien | Typen |
|---|---|---|
| `Track` | 16 | `7D` 1897, `7E` 1428 (meist 128² und 64², max. 256²) |
| `Track/Sky` | 17 | `7D` 127, `7E` 9, `78` 1 |
| `CarModel` | 84 | **`7B` 2211** (8 Bit + Palette, kleine Größen 18–80 px), `7D` 89 |
| `DashHud` | 27 | `6D` 128, `78` 7, `7F` 1 |
| `Render` | 2 | `6D` 18, `7D` 15, `78` 2 |

**Im Rennen hochgeladen** (Messlog, Formate je Größe): ARGB8888 16²–512², ARGB4444 8²–64², ARGB1555 16²/64², RGB565 64². Das **512er-Test-Schild (`7D`) kam als ARGB8888** an.

**Bestes Modell:**
- `7D` → ARGB8888, `7E` → ARGB1555, `78` → RGB565, `6D` → ARGB4444 (Upload folgt dem Typ).
- `7B` (palettiert): Der Treiber bietet keine Palettenformate an. Die Engine wandelt sie um, nach Größen und Menge passend zu den vielen kleinen ARGB4444-Texturen. Das sind vor allem **Fahrzeugtexturen**.

**Folge für HD:**
- **Strecken-Texturen als `7D` behalten volles 8-Bit-Alpha.** Für Strecken ist das Risiko damit erledigt.
- **Fahrzeugtexturen (`7B`) landen bei 4 Bit Alpha.** Die Masken-Werte aus `knowledge.md` §1 (`FF`, `CC`, `32`, `00`) liegen fast genau auf 4-Bit-Stufen (`CC` = 12·17, `33` = 3·17). Das passt zur Vermutung, dass sie dafür gewählt wurden. Für HD-Fahrzeugtexturen wäre `7D` statt `7B` zu prüfen (`CarModel` enthält bereits 89 `7D`-Objekte).
- Das Modell „Upload folgt dem Typ“ ist durch Zuordnung von Größen gestützt, nicht durch eine Messung je Textur.

### 11.4 👤 Testablauf

**A – Größe (Pflicht):**
1. `Measure = 1` ist in `Game CD/nfs5_enhanced.ini` gesetzt. Strecke **coastal** fahren und zu dem Schild fahren, das bei 1.6 die grünen Striche hatte (Objekt `0043`).
2. Erwartung: Das Schild trägt **magentafarbene Diagonalstreifen**. Ein Screenshot aus der Nähe genügt.
3. Spiel beenden und `Game CD/Drivers/dx11z.log` nach `work/logs/2.0-A-dx11z.log` kopieren.

**B – Farbtiefe (optional, gleich im Anschluss):** In der INI `RaceColorDepth = 32` setzen, dieselbe Strecke kurz fahren, Log nach `work/logs/2.0-B-dx11z.log` kopieren. Danach wieder `RaceColorDepth = 0`.

**Auswertung (🤖):**

| Beobachtung | Bedeutung |
|---|---|
| Streifen sichtbar, Log enthält `talloc … 512x512 levels 10` im Rennen | Grenze 256² ist **nicht** hart |
| Kein Streifen, Schild normal, nur 256er-Texturen im Log | Die Engine verwirft oder verkleinert die oberste Stufe |
| Schild fehlt oder Absturz | Grenze ist hart → Hook in `gimme.dll` oder HD auf 256² |
| B: Anteil ARGB4444 sinkt zugunsten ARGB8888 | `RaceColorDepth = 32` liefert volles Alpha für HD |

**Zurücksetzen:** `coastal.fsh` mit `coastal - Kopie.fsh` überschreiben (Original) und `Measure = 0`.

### 11.5 Ergebnis Teil A ✅

Die Grenze 256×256 ist **keine harte Engine-Grenze**: Eine 512×512-Stufe wird geladen und dargestellt. Das Schild wirkt kantig, weil der Inhalt nur per Nearest-Neighbor verdoppelt ist; das ist gewollt. Offen bleiben: Teil B (Farbtiefe), veränderter Alpha-Inhalt, 1024×1024 und der Speicherbedarf eines kompletten HD-Satzes (`PLAN.md` 2.0b).

## 12. Speicherbedarf (Aufgabe 2.0b)

**Frage:** Braucht ein HD-Satz mehr Speicher, als der 32-Bit-Prozess ohne Large-Address-Aware-Flag (2 GB Adressraum) hergibt, und lohnt sich deshalb ein Memory-Patch?

**Messung:** Der dx11z-Build 2.0b schreibt in jeden Bericht des Messlogs (`Measure ≥ 1`, alle 3600 Frames und bei Streckenwechsel) eine Zeile

```
M3T   memory: address space <belegt> of <gesamt> MB, private <MB> (peak), working set <MB> (peak), textures <MB>
```

„textures“ ist die Summe aller hochgeladenen Texturdaten (alle Mip-Stufen) im Grafiktreiber.

**Erster Anhaltspunkt:** Während einer laufenden Sitzung (2026-10-16, `tasklist`) belegte `Porsche.exe` rund **235 MB** Arbeitsspeicher, also weit unter 2 GB.

**Grobe Hochrechnung:** coastal belegt entpackt ~14,7 MB Texturdaten. Bei doppelter Seitenlänge ~59 MB, bei vierfacher ~235 MB je Strecke, zusätzlich Fahrzeuge und Kopien im Treiber.

**Entscheidungsregel:** Ein LAA-Patch ist erst sinnvoll, wenn der belegte Adressraum mit HD-Texturen in die Nähe von ~1,5 GB kommt. Stürzt das Spiel vorher ab oder verwirft es Texturen, obwohl der Adressraum frei ist, liegt die Grenze in einem internen Speicherbudget der Engine. Das LAA-Flag hilft dann nicht.

| Lauf | Konfiguration | Adressraum | Private (Peak) | Texturen | Bemerkung |
|---|---|---|---|---|---|
| 1 | Original-Texturen + Test-Schild 512², coastal, 3840×2160 (2026-10-16, `work/logs/3C11-twosided-clamp-dx11z.log`, 20 Berichte) | max. **817 von 2048 MB** | 361 MB | **30 MB** | Menü ~530 MB, Rennen 718–817 MB |
| 1b | wie 1, mit `ForceTwoSided = 2` (2026-10-16, `work/logs/3C12-twosided2-dx11z.log`, 48 Berichte) | max. 839 MB | 371 MB | 30 MB | Rückseiten-Durchgang kostet kaum Speicher |
| 2 | Strecke mit allen Texturen verdoppelt | ⏳ | ⏳ | ⏳ | |

### 11.6 Ergebnis Teil B (`RaceColorDepth = 32`) – keine Wirkung

Log `Game CD/Drivers/dx11z.log` vom 2026-10-16 11:27 (Build 3T.2), direkt ausgewertet. Eine Kopie gibt es nicht, weil der nächste Spielstart die Datei überschrieben hat: Das Rennen meldet weiter `setvideomode 48: 3840x2160 depth 2`, also dieselbe Farbtiefe wie mit `RaceColorDepth = 0`. Die Formatverteilung ist praktisch gleich (ARGB4444 68 % gegenüber 61 % in 3T.1). `RaceColorDepth = 32` ändert die Texturformate nicht. Vermutlich lief das Rennen ohnehin schon in 32 Bit. → Schalter wieder auf `0` setzen. Für HD-Alpha ist der FSH-Typ entscheidend (§11.3), nicht die Farbtiefe.

Nebenbefund: Die Engine meldet bei `talloc` eine Mip-Stufe weniger als die Kette hat (512² → `levels 9`, 256² → `levels 8`). Das ist bei allen Texturen gleich und unkritisch.

**Zwischenstand (nach Lauf 1):** Im Rennen sind rund **1,2 GB Adressraum frei**. Die Texturdaten im Treiber machen nur ~30 MB aus.
- Doppelte Seitenlänge (512²) ergäbe ~4× → ~120 MB.
- Vierfache Seitenlänge (1024²) ergäbe ~16× → ~480 MB.
- Dazu kommen die Kopien des Spiels im eigenen Speicher (FSH-Daten, umgerechnete Texturen) in ähnlicher Größenordnung.

Für einen 512er-Satz ist **kein LAA-Patch nötig**. Für 1024er-Sätze wird es knapp, dann wäre das LAA-Flag eine sinnvolle Vorsorge. Lauf 2 bestätigt oder widerlegt die Hochrechnung.

### 12.1 Lauf 2: kompletter HD-Satz coastal (2026-10-24)

`dx11z.log` vom 2026-10-24 00:24, Rennen auf coastal mit dem kompletten Satz: 178 Texturen mit Faktor 4 (128² → 512², 256² → 1024², `talloc ARGB8888 512x512 levels 9` / `1024x1024 levels 10`), `nfsp_texpool.asi` mit 256 MB.

| Phase | Adressraum | Private Bytes | Arbeitsspeicher | Texturen in dx11z |
|---|---|---|---|---|
| Menü | 1021 MB | 532 MB | 279 MB | 0 |
| Rennen | **1172–1176 MB** von 2048 | 519 MB (Spitze **673** beim Laden) | 495 MB (Spitze 719) | **225 MB** |
| Vergleich: Rennen mit Originaltexturen (anderer Lauf, 2026-10-24) | 1047 MB | 543 MB Spitze | 309 MB Spitze | 26 MB |

**Ergebnis:** Der Satz mit Faktor 4 kostet nur rund **+130 MB Adressraum**. Im Rennen bleiben **~870 MB frei**, ein LAA-Patch ist dafür **nicht nötig**.
- Die 215 MB große `.fsh` wird nur während des Ladens gehalten (Spitze der Private Bytes).
- Der Texturpool ist mit `VirtualAlloc` angelegt und belegt im Adressraum nur, was er reserviert.

**Hochrechnung Faktor 8** (~800 MB Texturdaten): grob +500 bis +600 MB, also rund 1,7–1,8 GB. Das ist zu knapp, dafür vorher das LAA-Flag setzen.

