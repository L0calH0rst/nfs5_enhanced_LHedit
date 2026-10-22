# knowledge.md – Faktenblatt NFS5

> **Zweck:** kurze, belegte Fakten zum Nachschlagen, eine Zeile pro Fakt, immer mit Quelle.
> Ausführliche Herleitungen stehen in den Themen-Dokumenten (`SHPI.md`, `FSH-REPACK.md`, `BUGS.md`, `MOD.md`, `GHIDRA.md`, `TOOLS.md` …), Status und Aufgaben in `PLAN.md`.
> **Pflege:** Ändert sich ein Fakt, zuerst das Themen-Dokument anpassen, dann die Zeile hier.
> **Status:** ✅ = im Projekt gemessen oder belegt · 📖 = Nutzer- oder Community-Wissen, noch nicht selbst geprüft.

## 0. Ablage im Projekt (Stand 2026-10-22)

| Was | Wo | Git |
|---|---|---|
| Doku (dieser Ordner) | `G:\NFSP\nfs5_enhanced_LHedit\doc\` (bis 2026-10-22 `G:\NFSP\doc\`, dort gelöscht) | LHedit, `master` |
| Mod-Code: ASI, dx11z, LFSP | `G:\NFSP\nfs5_enhanced_LHedit\` (`src`, `drivers/dx11`, `lfsp`) | LHedit (github L0calH0rst/nfs5_enhanced_LHedit) |
| Standard-INIs (`lfsp.ini`, `nfs5_enhanced.ini`) | `nfs5_enhanced_LHedit\config\` | LHedit |
| Build | `nfs5_enhanced_LHedit\build-asi\` (`lfsp\lfsp.asi`, `drivers\dx11\dx11z.dll`) | nein |
| Testinstallation | `G:\NFSP\Game CD\` (ASI, `lfsp.ini`, `drivers\dx11z.dll`, Sicherungen in `drivers\_backup\`) | nein |
| Werkzeuge, Messungen, Logs, Strecken/HD-Arbeit | `G:\NFSP\work\` | nein (NFSP-M wird nicht mehr committet) |
| Upstream-PRs | Branch je Fix ab `upstream/master` (iArtorias/nfs5_enhanced), Push nach `origin`, PR im Browser (kein `gh`); nach Abschluss Branch auf GitHub und lokal löschen. Gelöscht 2026-10-22: `fix/wheel-suspension` (#9), `fix/menu-scroll` (#6), `fix/lamp-glow-depth` | LHedit |
| Ghidra-Projekt und Exporte | `G:\NFSP\GhidraNFSP\` | nein |

## 1. Texturen – Alpha-Werte als Maske (Fahrzeugtexturen)

Bei Fahrzeugtexturen ist Alpha **nicht nur Transparenz**, sondern eine Material-Maske (📖 Nutzer):

| Nr. | Alpha-Wert | Bedeutung |
|---|---|---|
| 2 | `#ffffff` | Farbe bleibt unverändert |
| 3 | `#cccccc` | Wagenfarbe (wird mit der gewählten Lackfarbe eingefärbt) |
| 4 | `#000000` | transparent |
| 5 | `#323232` | Innenraum |

**Folge für HD-Texturen:** Die Alpha-Ebene von Fahrzeugtexturen darf beim Hochskalieren **nicht interpoliert** werden. Sonst entstehen Zwischenwerte, die keiner Maske entsprechen. Stattdessen Nearest-Neighbor oder eine Rück-Quantisierung auf genau diese Werte. *(Ableitung, nicht getestet.)*

## 2. Texturen – Größen und Formate

| Fakt | Status | Quelle |
|---|---|---|
| Im Rennen lädt das Spiel keine Textur größer als **256×256** | ✅ (eine Sitzung, Strecke coastal) / 📖 Nutzer: „nur bis 256×256 unterstützt“ | `BUGS.md` §8.5, `work/logs/3T1-dx11z.log` |
| 256² ist **keine** harte Grenze: eine 512×512-Stufe (Typ `0x7D`, 10 Mip-Stufen) wird im Rennen geladen und dargestellt | ✅ | `FSH-REPACK.md` §11.5 |
| Das Frontend lädt ARGB8888 bis 1024×1024 (ohne Mips) | ✅ | `BUGS.md` §8.5 |
| Upload-Formate des Spiels: ARGB4444 (häufigstes), ARGB8888, ARGB1555, RGB565; keine palettierten Formate | ✅ | `BUGS.md` §7.1, §8.5 |
| Strecken-Texturen kommen mit vollständiger Mip-Kette bis 1×1 | ✅ | `BUGS.md` §8.5 |
| FSH-Bitmap-Typen: `0x7D` = 8888, `0x6D` = 4444, `0x7E` = 1555, `0x78` = 565, `0x7B` = 8 Bit + Palette, `0x7F` = 24 Bit | ✅ | `SHPI.md` |
| Upload-Format folgt dem FSH-Typ (`7D` → 8888, `7E` → 1555, `78` → 565, `6D` → 4444); palettierte `7B` (vor allem Fahrzeuge) werden zu ARGB4444 | gestützt (Größen-Zuordnung), nicht je Textur gemessen | `FSH-REPACK.md` §11.3 |
| Strecken-`.fsh` enthalten nur `7D`/`7E`; `CarModel` fast nur `7B` (2211 von 2300) | ✅ | `FSH-REPACK.md` §11.3 |
| `RaceColorDepth = 32` ändert die Texturformate nicht | ✅ | `FSH-REPACK.md` §11.6 |
| `fshtool` scheitert bei vergrößerten Texturen an `BUFSZ` in `INDEX.FSH` (exakte Originalgröße); erhöhen löst es. 512² packt und entpackt pixelgenau | ✅ | `FSH-REPACK.md` §11 |
| QFS speichert die entpackte Größe in 24 Bit (max. 16 MB). Der Entpacker des Spiels liest genau diese 3 Byte; die Groß-Variante mit 4-Byte-Größe (`0x90 FB`) kennt er nicht. `fshtool` schreibt über 16 MB einen falschen Kopf, die Datei lässt sich nicht mehr entpacken | ✅ (Code `FUN_00571490`, `FUN_00594760`; fshtool getestet) | `SHPI.md` §1.1 |
| Der Lader nimmt auch unkomprimierte `.fsh` (`SHPI` am Anfang): Ohne QFS-Magic wird der Dateipuffer direkt verwendet | ✅ Code (`FUN_0056e4f0`, `FUN_0056e3e0`) + Spiel 2026-10-17 (coastal, 91 MB unkomprimiert) | `SHPI.md` §1.1 |
| Texturen liegen im Spiel in einem eigenen Pool „DTEX“ (`FUN_00553fc0`, `VirtualAlloc`): 12 MB Vorgabe, im Rennen **24 MB** (`push 0x1800000` bei `0x4679DA` und `0x467F45`). Läuft er über, gibt `FUN_00554b20` NULL zurück und `FUN_00554ad0` stürzt ab (`0x554AE6`). Eine 1024²-Textur 8888 mit Mips braucht 5,6 MB | ✅ Absturz 2026-10-17 + Code; mit `nfsp_texpool.asi` (256 MB) lädt und fährt coastal mit 14 Texturen in 1024² | `work/hd/SPEC.md` §7 |
| Alpha-BMPs für `fshtool` müssen 8-Bit-Graustufen sein (weiß = deckend) | ✅ | `FSH-REPACK.md` §8 |
| `fshtool` leert die Zieldatei vor dem Repack → bei Abbruch ist sie leer, also vorher sichern | ✅ | `FSH-REPACK.md` §8 |

## 3. Rendering

| Fakt | Status | Quelle |
|---|---|---|
| Der Großteil der Welt wird mit Alpha-Blending **und** Z-Write gezeichnet, Alpha-Test `GREATER` mit Referenz 10/255 | ✅ | `BUGS.md` §8.5 |
| Heller Saum an Alpha-Kanten (B4) ist ein Fehler des Originalspiels, kein Mod-Fehler | ✅ Nutzer + Messung | `BUGS.md` §8 |
| `ForceTwoSided = 1` (früher 2) behebt fehlende Rückseiten (Fensterläden) ohne Flackern doppelseitiger Schilder | ✅ | `BUGS.md` §9.4 |
| Die Himmels-Löcher am Bildrand (B5) entstehen durch Hor+: Das Himmels-Culling (`0x45AAF0`) nutzt das 4:3-Halb-FOV aus `view+0x130`; jede Himmelsschicht hat 9 Teilflächen à 10×10 | ✅ | `BUGS.md` §4.1, §4.2 |
| Engine-Culling mit dem 4:3-Halb-FOV (`view+0x130`): Himmel `0x45AAF0`, Strecken-Chunks `0x47F550`; der Mod verbreitert nur die Projektion | ✅ | `BUGS.md` §4.2, §4.3 |
| Fahrzeug-Rückseiten (Scheiben von innen) erscheinen auch mit abgeschaltetem Culling nicht → die Engine verwirft sie vermutlich selbst | Vermutung | `BUGS.md` §9.4 |
| Das Menü maskiert die Profilliste über die Tiefe mit voll transparenten Flächen ohne Alpha-Test; Eingriffe in Z-Write solcher Draws machen die Liste unsichtbar (B10) | ✅ (Wirkung belegt, Mechanismus abgeleitet) | `BUGS.md` §10 |
| Die Engine übergibt dem Treiber transformierte, vorbeleuchtete Vertices (`X, Y, Z, Rhw, Diffuse, Specular, UV0, UV1`) ohne Normalen; die Transformation läuft in der Engine | ✅ (Quellcode) | `nfs5_enhanced-1.1.0-mod-sourcecode/drivers/dx11/src/driver.h` (`TlVertex`) |
| `0110` shrb ist ein Atlas (links 4 Büsche à 64 px, rechts 2 Bäume à 128 px); ein Busch wird mit v 1,0…1,24 adressiert. Wrap liest an Zellgrenzen den Fuß des Nachbar-Sprites → Linien (B7) | ✅ | `BUGS.md` §9.4 |
| Wasserspiegelungen sind Original-Assets: eigene Geometrie mit eigener 64×64-Textur (vorgefertigtes Spiegelbild, Alpha-Verlauf, Vertexfarbe ~halb so hell), gezeichnet aus der ersten Liste der Render-Warteschlange `FUN_00466660` (`this+4`) vor der übrigen Szenerie; die Engine spiegelt nichts. Beispiel Boot `BF250B72` → Spiegelung `41C66523` | ✅ (Messlauf M-B15 + Ghidra) | `BUGS.md` §12 |
| B4-Fix: `[Dx11] AlphaEdgeFix = 1` (früher 2), `AlphaEdgeRef = 128`; die frühere Schwellen-Variante zerstörte Reflexionen und Ufer-Übergänge | ✅ | `BUGS.md` §8.7–8.9 |
| Scheinwerfer bei Nacht: die Engine addiert auf der CPU einen neutral-weißen Anteil zu den Vertexfarben naher Geometrie (+19…+56 je Kanal); keine Lichttextur, keine zweite Textur-Stufe. Lichtkegel = halbtransparente Fläche mit Textur `40EB6B06`, Halos/Flares = addierte Sprites (SrcAlpha/One, 64²) | ✅ | `LICHT.md` §2, §5 |
| Scheinwerfer-Funktion: `porsche.exe` `FUN_00453110` (Strecken-Meshes; `FUN_004535c0` für weitere Szenerie). Licht an = Byte in `[car+0x74C]+0x2D4` (Spielerauto). Faktor je Vertex aus 128×128-Lichtkarte `DAT_0061e1cc` im Scheinwerfer-Raum (`FUN_00452910`, Matrix `DAT_0061bccc`), Farbe `DAT_0061bd4c` additiv, auf 255 begrenzt | ✅ (Haltepunkt + Ghidra) | `LICHT.md` §6.14–6.15 |
| Leuchten-Schaden: `0x4398C6` setzt Bytes der Leuchten-Tabelle auf 0, wenn ein Float am Fahrzeug `[model+0xF4]` die Schwelle `[0x5B2408]` übersteigt: `+0x4E4` → 0/4, `+0x4EC` → 1/5, `+0x4F4` → 3/7/11, `+0x4FC` → 2/6/10, `+0x4F8` → 8, `+0x504` → 12/13. Bei Licht an ohne Bremsen: 2/3/10/11 = 1 mit Stärke `[0x5B35A8]` in `[model+0x2D8]` | ✅ (Disassembly) | `LICHT.md` §9.3, §9.6 |
| Scheinwerfer-Modell nachgerechnet: `licht = (welt, 1) × M[0061BCCC]`; `faktor = (karte[round(z·1,28)·128 + round(x·3,62) + 64] − |round(y·32)|) / 256`; Beitrag `128 × faktor` je Kanal (Farbe `FF808080`). Lichtkarte = weicher ovaler Fleck, ≈ 99 × 34 Einheiten | ✅ | `LICHT.md` §7.2 |
| Scheinwerfer pro Pixel (`[Dx11] Headlights = 1`) funktioniert: Rückrechnung Bild → Clip (`w = 1/rhw`, Viewport `[renderer]+0x2C…0x3C`) → `inv(P′)` (Projektion `kamera+0x148`, Spalten 0/1/3) → `inv(Objekt×Ansicht)` (`kamera+0x188`, Kamera = `[[0x69E0D4]]`) → `L[0061BCCC]`. Die Matrix `[[[[0x69ED0C]]+0x14]+0x60]` ist nur die Objektmatrix | ✅ (👤 Sichtprüfung) | `LICHT.md` §7.4–7.6 |
| Scheinwerfer gibt es nur im Nachtmodus (`DAT_00628a28`, 16 Bit): Lichtkarte aus `GameData/Render/night.psh` (SHPP, Eintrag `nght`, 128×128 8 Bit ab `0x28`), Matrix je Bild aus dem Spielerauto (`FUN_004526c0`), zweites Licht = Blaulicht des nächsten Polizeiautos | ✅ (Ghidra + Dateivergleich) | `LICHT.md` §7.7 |
| Scheinwerfer bei Tag: Farbknoten `FUN_00453110`/`FUN_004535c0` laufen nur in der Nacht-Kette; der Strecken-Rückruf `FUN_00450ca0` (Slot `005B3CF8`) läuft immer. `FUN_00543ae0` rechnet die Kameramatrizen erst im Aufruf neu (`FUN_005393a0`). Tag-Projektion `kamera+0x148` hat Spalte 3 = (0,0,0,1), w = Spalte 2 (`−z`). Licht-Bytes des Autos schalten auch am Tag | ✅ (Log 9.6f–9.6h, 👤 Sichtprüfung) | `LICHT.md` §7.9–7.13 |
| Kamera: Zustand je Kamera ab `0x627E80` (Abstand `0x10C`; `+0x24` Position, `+0x58` halbes FOV, `+0x7C` Drehung, `+0xBC` Typ). Takt `FUN_00463c30` (Position nach Typ), je Bild `FUN_00464f80` (View, `view+0x130` = FOV), Taste C `FUN_00465a60`, setzen `FUN_00465790(Kamera, Typ)`. 22 Typen, z. B. 2–4 TailCam, 12 TVCam | ✅ (Ghidra) | `KAMERA.md` §1–2 |
| Wiederholungs-Kameras: `GameData/Track/<strecke>_cameras.scn` (Kamera- und Trigger-Elemente); Regisseur `FUN_00413b80` setzt bei neuer Zone die Streckenkamera (`FUN_00413960`). Die vier Ansichten der Taste C stehen in `car[N].camera` (`0x658294`), Vorgabe `fe.txt` | ✅ (Ghidra) | `KAMERA.md` §1, §3 |
| F11 ist eine Spieltaste: schaltet die Szenerie-Detailstufe zyklisch (entfernte Gebäude aus / teilweise / alle). Das Spiel übersetzt F1–F12, Navigations- und Ziffernblocktasten selbst (`FUN_0053b530`); der Mod belegt F11 nicht | ✅ (Nutzer + Code) | `LICHT.md` §6.13 |

## 4. Spieldateien und Formate

| Fakt | Status | Quelle |
|---|---|---|
| `Porsche.exe` v3.5 aus dem Mod-Release: 2.129.965 Byte, MD5 `316f33a59db48a7db2a75cc4b3bd03dc` (die ursprüngliche CD-Version war ein 249-KB-Launcher-Stub) | ✅ | `BUGS.md` §7.4 |
| Speicher im Rennen (coastal, 4K, Original-Texturen): max. 817 von 2048 MB Adressraum, Texturdaten im Treiber 30 MB | ✅ | `FSH-REPACK.md` §12 |
| `Porsche.exe` hat **kein** Large-Address-Aware-Flag (Characteristics `0x010E`) → 2 GB Adressraum; der Mod setzt es nicht | ✅ | PE-Header, Mod-Quellen |
| `gimme.dll` (4,6 MB) ist der Engine-Kern, C++/MSVC ~2000; Dev-Pfade `d:\dev\nfs5\game\common\render\…` | ✅ | Strings im Binary |
| `PORSCHE.ICD` (2,1 MB) ist ein PE32-Frontend-Binary | ✅ | PE-Header |
| `.fsh` und `.env` sind SHPI-Container (Magic `SHPI`), oft QFS-komprimiert (Header `10 FB`) | ✅ | `SHPI.md` |
| `.tpg`, `.map`, `.clr`, `.lay`, `.ctr` sind INI-artige Textdateien | ✅ | `INI-formats.md` |
| `.viv` = BIGF-Archiv (Video), `.mad` = MAD-Audio | ✅ | `MEDIA.md` |
| `.sim` enthält lesbare Fahrzeugdaten; `.crp` = Fahrzeug-Container | ✅ (nur gesichtet) | – |
| `.tpg` `[styleN]`: `geometryN`/`typeN` setzen die Mesh-Variante eines Fahrzeugteils (Tabelle mit 120 Einträgen am Fahrzeug `+0x56C`); Teil 35/47 = Seitenscheiben außen/innen | ✅ (Code) | `FENSTER-VERDECK.md` §2 |
| Die Geometrie-Auswahl nach Variante macht `0x43C1B0` (Ghidra-Lücke, je Render-Schicht); `FUN_00447c50` betrifft nur Textur-Teile aus `<Auto>D/L.fsh` | ✅ (Code, Messung 8.3b) | `FENSTER-VERDECK.md` §2 |
| `FUN_0043c830` zeichnet einen Listeneintrag mit den Positionen aus Eintrag `+0x40` (Strom 'vt'), wenn dort ein Puffer steht, sonst aus der `.crp`; `0x43C1B0` legt ihn für animierte Teile und für Teile mit Kopf-Byte 10 Bit 8 bei `dword[0x6197C4] > 0` an (`0x59EFF0`; Scheiben haben ihn), `FUN_0043e0c0` gibt ihn mit `0x59F050` frei | ✅ (Code, Log v4a) | `FENSTER-VERDECK.md` §6.7 |
| Das Auto-Modell hat zeitgesteuerte Animationskanäle (Menü 50, Rennen 24), die `FUN_00438440` jedes Bild aktualisiert, auch im Rennen | ✅ (Code) | `FENSTER-VERDECK.md` §3.1 |
| Verdeck-Animation = Mesh-Animation Slot 1 am Modell (`+0x568`, Setzen mit `FUN_004380d0`); im Rennen setzt die Engine Slot 1 nicht | ✅ (Code, Messung 8.3) | `FENSTER-VERDECK.md` §3.1 |
| Style am Fahrzeug (`+0x568`) zählt ab 0, `.tpg` `[styleN]` ab 1 | ✅ (Messung 8.3) | `FENSTER-VERDECK.md` §6.1 |
| Jede `.sim` in `GameData/Simulation/CarData` (87 Dateien, auch `default.sim`) beginnt mit dem Modellnamen samt Baujahr in den ersten vier Zeichen, z. B. `1956 356 A coupe 1.6L`, `2000 996 Turbo coupe 3.6L` | ✅ | `LICHT.md` §8.2 |
| `FUN_00410d60` legt für jedes Rennauto `carSpecs` (0x1C4 Byte) an, speichert den Zeiger in `car + 0x760` und kopiert die `.sim` dorthin (`FUN_0049c6c0`, `%s%s.sim`, sonst `default.sim`); `car` = Eintrag der Tabelle `0x5E8B34` = `model + 0xF4` in LFSP | ✅ (Ghidra) | `LICHT.md` §8.2 |
| Tastenbelegung steht in `SaveData/*.sav` als `Controller.<Aktion>` + DirectInput-Scancode; in allen Profilen ist **L** (`0x26`) der Lichtschalter, T Warnblinker, W/E Blinker, H Hupe | ✅ | `LICHT.md` §8.4b |
| Menülisten (Autos, Lacke, Strecken) sind `NFS5.ComboBoxSlider` = Klasse `FEComboBox` (vtable `0x5B5C24`), `NFS5.ListBox` = `FEListBox` (`0x5B611C`); Scroll-Accessor `+0x1BC` (aktuell = oberste Zeile), Scrollleiste `+0x1B8` | ✅ (RTTI, Code) | `MENU.md` §1.1, §1.4 |
| Das Frontend erzeugt Mausrad-Ereignisse (Typ 9), aber der Verteiler der Steuerelemente `FUN_00525f80` kennt Typ 9 nicht; der Tasten-Slot der Listen ist leer (`0x4D1B40`) | ✅ (Code) | `MENU.md` §1.3 |
| Verkehrsautos sind normale Auto-Slots (Typ 4 in `FUN_004EEDA0`, Flag `0x10` in `car + 0x52C`, KI `AIHigh_Traffic`); Anzahl im Einzelrennen = Menüwert `Track1.Traffic`, gekürzt auf 9 Autos gesamt; Modell starr `ID[i % 7]` aus Strecken-Datensatz `+0x7C` | ✅ (Ghidra) | `VERKEHR.md` §1–3 |
| Verdeckungswände: Chunk `Vis ` (`0x56697320`) im `Trak`-Container `<strecke>.crp` (QFS), geladen von `FUN_00488a60` in `DAT_00628960`; je Wand 24 Byte = 2 Punkte `(x, y, z)`, `y` = Art 0/1, keine Höhe. Art 1 verdeckt auch ferne Objekte (Flags `0x400020`), Art 0 nur normale | ✅ (Ghidra + Datei, alle Strecken) | `SICHTWEITE.md` §2a |
| Strecken-Geometrie: `<strecke>.crp` = QFS → `Trak`-Container; Objekte `Arti` mit `vt` (xyz1 float), `uv`, `df` (BGRA), `pr` (Primitive: Art 1 Strip / 2 Linien / 3 Liste / 4 Vierecke, belegt `0x540BA4`, Streams nach Slot 0 vt / 2 uv / 3 df (Id meist 11/10/9, nicht verlässlich), Byte-Indizes `Iv`/`Iu`, Hüllkugel `+8`), optional `tr`-Matrix; ohne `tr` Weltkoordinaten | ✅ (alle Strecken, Code) | `SICHTWEITE.md` §2b |
| Ein Primitiv (`pr`) darf höchstens 252 Ecken und 255 Vertices haben: der Renderer nutzt feste Puffer (`FUN_005408f0`, Clip-Codes ab `this+0x44`); mehr überschreibt Speicher (Flackern, fehlende Strecke, Absturz). Original-Maximum je Objekt: 39 Primitive, 737 Vertices | ✅ (Spieltest + Original-Statistik aller Strecken) | `SICHTWEITE.md` §2b |
| Neue Strecken-Objekte gehen: Das Spiel erfasst alle `Arti` beim Laden (`FUN_004879d0`) und ordnet sie nach `Base`-Flags; kopierte Schilder sind eigenständige physische Objekte, ein fremdes Modell übernimmt das Verhalten der Vorlage. `Base`-Bit `0x8000` = Vorlage ohne Position (Hütchen …). Material `mt` +0x24 `sn` + +0x28 Bildname der `.fsh` | ✅ (👤-Tests 2026-10-21) | `SICHTWEITE.md` §2c |
| Fahrbahn der Physik/KI: `SimD` im `Trak` (Lader `FUN_00480080`/`FUN_00480140`), 80 Byte je Wegpunkt (+0x04 Position, +0x10 Normale, +0x1C Fahrtrichtung, +0x28 Seitenvektor, +0x40 4 Breiten), ≈ 6 m Abstand (coastal 1465). Normale immer ≈ senkrecht → **keine Querneigung**, das LOD-0-Mesh ist dagegen oft geneigt (95 % bis 0,4–1,1 m Abweichung quer). Ob die Radkontakte SimD oder das Mesh nutzen, ist offen. `.edg` = Linienzüge (je 28 Byte: 2 Punkte + Wort), vermutlich Randbegrenzung | ✅ (Messung alle Strecken, `simd_vs_mesh.py`) | `SICHTWEITE.md` §2d |
| Fahrbahn-Objekte `RD…` haben 3 LOD-Stufen; LOD 1 weicht von LOD 0 um 95 % ≤ 1–8 cm (max 0,2–0,5 m) ab, LOD 2 um 95 % ≤ 5–16 cm (max bis 1,4 m) | ✅ (Messung alle Strecken) | `SICHTWEITE.md` §2d |
| Reifenspuren: Textur `SKDP` (`common.fsh`), erzeugt in `FUN_004a2880` → `FUN_00478f30` → `FUN_00479190`; Punkt = Ecke des Fahrzeugkastens (`FUN_00496860`, `auto+0x7F8`+i·0xC4), quer waagrecht (beide Ecken gleiche Höhe), neuer Punkt nur jeder 8. (Stufe ≥ 2) bzw. 16. Aufruf (`DAT_006197ec`); Verwaltung `FUN_00479900` (Vtable `0x5B4750`, `DAT_00628b70`) je Streckenabschnitt; gezeichnet über Tabelle `0x5B3D28` Eintrag 2 (`0x5B3D30` = `0x451420` → `FUN_0047a060`) | ✅ (Ghidra) | `BUGS.md` §19.1–19.2 |
| Fahrzeuge aus `nfs5.car`: ein Datensatz-Typ, Art `+0x1F4` (0 Porsche, 1 Verkehr, 2 Polizei); Listen `*0x6919E8` alle, `*0x6919F0` Porsches mit `.crp`, `*0x6919FC` Verkehr, `*0x6919F8` Polizei (`FUN_004F3E40`) | ✅ (Ghidra) | `VERKEHR.md` §4e |
| Verkehrsmodelle je Strecke: `FEData/Data/nfs5.trk` (15 × `0x4A8` Byte, 7 IDs bei `+0x7C`); Modelle mit Motorstimme in `FEData/Data/nfs5.car` (16 Verkehrsmodelle, ID 186–202 ohne 194); Menüstufe Schwer = 7 Verkehrsautos | ✅ (Datei, Messung) | `VERKEHR.md` §3 |

## 5. Mod `nfs5_enhanced`

| Fakt | Status | Quelle |
|---|---|---|
| Der Mod prüft `Porsche.exe` über PE-Header-Felder, nicht über MD5 | ✅ | `MOD.md` §2 |
| Der Mod skaliert die Sichtweite nur an `0x44F1A0` und `0x4677FC`; er hat keinen Code-Hook in `gimme.dll` | ✅ | `MOD.md` §5, `BUGS.md` §2 |
| Ohne `TextureDetail = 3` lädt das Original auf CPUs > 2,1 GHz alle Texturen in 16 Bit und halber Größe | ✅ (Mod-Doku) | `MOD.md` §3 |
| Ghidra-Funktionsgrenzen sind unzuverlässig (`FUN_101054a0`: 358 Byte statt `0x2AA4`) | ✅ | `GHIDRA.md` §3 |

## 6. Umgebung und Werkzeuge

| Fakt | Status | Quelle |
|---|---|---|
| `dx11z.dll` bauen: `work/patches/dx11z/build_dx11z.cmd` (MSVC BuildTools 18, x86) | ✅ | – |
| Die dx11z-Shader kompiliert das Spiel zur Laufzeit; Offline-Prüfung mit `fxc` aus `Windows Kits\10\bin\10.0.26100.0\x64` (ps_4_0/vs_4_0) | ✅ | – |
| Alle dx11z-Schalter (`AlphaEdgeFix`, `Measure`, `ForceTwoSided` …) müssen im Abschnitt `[Dx11]` der INI stehen. Unter einer anderen Überschrift werden sie ignoriert (= 0). Ausnahme ab 9.11b: die Licht-Schalter liest dx11z aus `lfsp.ini [Lights]` (`LICHT.md` §8.2a) | ✅ | `BUGS.md` §9.1 |
| `Game CD/Drivers/dx11z.log` wird bei jedem Spielstart überschrieben → nach dem Test sofort sichern | ✅ | `BUGS.md` §7.1 |
| Mod-Quellcode `nfs5_enhanced-1.1.0-mod-sourcecode/` ist per `.gitignore` ausgeschlossen; Änderungen daran werden als Patch-Kette in `work/patches/dx11z/` versioniert | ✅ | – |
