# LICHT.md – Scheinwerfer, Licht und Reflexionen

> Themen-Dokument zu Phase 9. Status und Aufgaben stehen in `PLAN.md`.

## 1. Ausgangslage

- Der Treiber erhält vorbeleuchtete Vertices ohne Normalen (`knowledge.md` §3). Licht kann er nicht selbst neu berechnen.
- Nutzer-Screenshots (Nachtstrecke, dieselbe Szene ohne und mit Scheinwerfern): Mit Licht werden Fassade und Straße großflächig und weich heller, die Farben neutraler; über dem Heck erscheinen zwei helle, halbtransparente Lichtkegel; die Rücklichter leuchten.
- Hypothese vor der Messung: projizierte Lichttextur über die zweite Textur-Stufe (`UV1`).

## 2. Messfahrt 9.2 (2026-10-16)

Build 9.1 (`Measure = 1`), Nachtstrecke. Ablauf laut Nutzer: ~1 min stehen **mit**, dann ~1 min **ohne** Scheinwerfer. Daten: `work/logs/9.2-scheinwerfer/` (`9.2-dx11z.log`, `m3t_*.bmp`, Übersicht `9.2-texturen-uebersicht.png`).

### 2.1 Befund

1. **Keine zweite Textur-Stufe im Rennen.** Alle erfassten Licht-Kandidaten haben `tex1 none`, Stufe 1 ist aus (`op 1`). Eine projizierte Lichttextur über `UV1` gibt es nicht. Die Hypothese aus §1 ist **widerlegt**.
2. **Alle Licht-Kandidaten sind addierte Leuchtsprites** (`src 5 dst 2` = SrcAlpha/One, meist ohne Z-Write, je 2 Dreiecke, Koordinaten 0…1):

   | Textur | Größe | Aussehen | Deutung |
   |---|---|---|---|
   | `E1FD7F61` | 64² | weicher runder Schein | Lampen-Halo |
   | `10D45A01` | 64² | Strahlenstern | Lens-Flare / Glanzpunkt |
   | `97A478A7` | 64² | Stern mit Hof | Flare |
   | `96D7B290` | 64² | weicher Schein, weiß | Halo (nur in einem Teil der Fahrt) |
   | untexturiert | – | – | 106 Batches zwischen Flip 4059 und 7200, Kandidat für die Lichtkegel |

   Im Frontend zusätzlich `4A0B6BAD` (weiß, Alpha-Verlauf von oben nach unten: **Lichtkegel-Textur**) und `793ED57D` (runder Schein), beide addiert.
3. **Die großflächige Aufhellung ist kein eigener Pass.** Kein multiplizierender Blend, keine Überlagerung der Welt. Sie muss daher in den **Vertexfarben** stecken, die die Engine pro Bild auf der CPU berechnet. Gestützt durch die mittlere Vertexfarbe der deckenden Welt, die sich zwischen den Berichtsabschnitten deutlich ändert (z. B. 110/87/64 → 121/138/115, Specular immer 0).

### 2.2 Grenzen der Messung

- Das Log hatte keine Zeitangaben und berichtete alle 3600 Flips. Die Abschnitte lassen sich darum nicht sicher „mit/ohne Licht“ zuordnen. Die Fahrt enthielt auch Bewegung (Anfahrt, Menü), was die Vertexfarbe ebenfalls ändert.
- Pass-Schlüssel enthielten den Texturzeiger, deshalb neue Pass-Nummern nach jedem Textur-Reset (behoben in 9.2b).

## 3. Folgen für eine Verbesserung (vorläufig)

- **Lichttextur tauschen** bringt für die Aufhellung nichts, weil es keine gibt. Nur die Sprites (Halos, Flares, Kegel) lassen sich per Textur verschönern.
- **Weicher, realistischer Lichtabfall** braucht Licht pro Pixel. Der Treiber kann den Scheinwerfer-Anteil nicht aus den Vertexfarben herauslösen. Weg: die ASI meldet dem Treiber „Scheinwerfer an“ und die Lage des Spielerautos, der Treiber rechnet den Lichtkegel pro Pixel aus dem Tiefenpuffer. Die CPU-Aufhellung der Engine müsste dafür abgeschaltet oder abgeschwächt werden (Engine-Hook).
- Grobe Fassaden-Dreiecke erklären die weiche, aber unpräzise Aufhellung: Licht wird nur an den Ecken berechnet und dazwischen interpoliert.

## 4. Messfahrt 9.2b (2026-10-16)

Build 9.2b: Bericht alle **10 s** mit Sekunden seit Start, Pass-Schlüssel nach Textur-Inhalt. Ablauf laut Nutzer: an der Startlinie stehen, Licht an → aus → an; der erste Abschnitt ist durch Intro und Countdown länger. Daten: `work/logs/9.2b-scheinwerfer/`.

Rennen ab ~31 s (Flip 2510), Ende 130,6 s. Je 10-s-Abschnitt (Sprites pro Bild = Batches / Flips im Abschnitt):

| Abschnitt (s) | Halo `E1FD7F61` / Bild | Vertexfarbe (Mittel über Vertices) |
|---|---|---|
| 41–51 | 27,6 | 119 / 102 / 80 |
| 51–61 | 28,8 | 113 / 108 / 93 |
| 61–71 | 24,5 | 111 / 112 / 95 |
| 71–81 | 16,0 | 113 / 114 / 98 |
| 81–91 | 15,2 | 115 / 112 / 95 |
| 91–101 | 25,5 | 115 / 112 / 96 |
| 101–111 | 26,3 | 115 / 111 / 95 |
| 111–121 | 16,1 | 116 / 111 / 95 |
| 121–131 | 10,4 | 114 / 108 / 94 (inkl. Beenden) |

1. **Der Halo-Sprite `E1FD7F61` folgt dem Licht:** ~25–29 pro Bild gegenüber ~15–16. Das Muster hoch → niedrig → hoch → niedrig passt zu an → aus → an, wenn das Licht beim Start schon an war und am Ende noch einmal ausging (offen, 👤 fragen). Flares `97A478A7`/`96D7B290` bleiben konstant (Streckenlampen).
2. **Die über alle Vertices gemittelte Vertexfarbe ändert sich mit dem Licht nicht** (ab 61 s ±2). Das widerlegt die Vertex-Beleuchtung **nicht**: Das Mittel wird von Zehntausenden entfernter Vertices bestimmt, die angestrahlte Fassade besteht aus wenigen großen Dreiecken.
3. Der untexturierte additive Pass (Lichtkegel-Kandidat) erscheint nur zwischen 42 und 51 s.

## 5. Messfahrt 9.2c (2026-10-16) – Vertex-Beleuchtung belegt

Build 9.2c: **F11** zeichnet das nächste Bild Draw für Draw auf (`M3T snapshot #n`): je Textur und Blend-Art Batches, Vertices, abgedeckte Bildfläche und die **nach Bildfläche gewichtete** mittlere Vertexfarbe, die 80 größten Gruppen; Kopfzeile mit dem Mittel aller deckenden Draws.

Ablauf: an der Startlinie stehen bleiben, Kamera nicht bewegen. Licht aus → F11, Licht an → F11, Licht aus → F11. Steigt die gewichtete Farbe derselben Fassaden-/Straßentexturen mit dem Licht, ist die Vertex-Beleuchtung belegt; sonst gibt es einen weiteren, noch nicht erfassten Mechanismus.

### 5.1 Ergebnis

Daten: `work/logs/9.2c-snapshot/9.2c-dx11z.log`. Drei Aufnahmen an derselben Stelle, Kamera fest: #1 Licht aus (613,6 s), #2 an (628,3 s), #3 aus (640,8 s).

**Mittel aller deckenden Draws, nach Bildfläche gewichtet:** 71,5 / 67,1 / 56,6 → **88,3 / 83,9 / 73,5** → 71,5 / 67,1 / 56,6 (exakt zurück).

Je Textur (Vertexfarbe R/G/B, aus → an):

| Textur | Fläche (px) | aus | an | Δ |
|---|---|---|---|---|
| `C4B0AEFA` 128² (größte deckende Fläche, vermutlich Straße) | 4,10 Mio. | 78,5 / 73,5 / 65,8 | 115,9 / 110,9 / 103,3 | +37 je Kanal |
| `E99B6163` 128² (25 Batches) | 0,36 Mio. | 65,7 / 61,3 / 55,0 | 109,8 / 105,5 / 99,1 | +44 |
| `B0C375E6` 64², Alpha-Blend | 0,22 Mio. | 98,6 / 81,8 / 68,6 | 154,5 / 138,1 / 124,9 | +56 |
| `D48C3567` 64² | 0,17 Mio. | 98,4 / 83,0 / 60,2 | 117,3 / 101,9 / 79,1 | +19 |
| `5CC21D88` 128² | 0,08 Mio. | 137,8 / 95,9 / 61,3 | 159,2 / 117,8 / 83,1 | +21 |
| übrige Welt (entfernt) | – | unverändert | unverändert | 0 |

Nur in #2: `40EB6B06` 64², normaler Alpha-Blend (SrcAlpha/InvSrcAlpha), 60 Vertices, Farbe 156 / 154 / 149 → vermutlich die **Lichtkegel** über dem Heck. Die Flares `10D45A01` haben 38 statt 30 Batches.

### 5.2 Schluss

1. Die Scheinwerfer beleuchten die Welt, indem die Engine auf der CPU einen **neutral-weißen Anteil zu den Vertexfarben addiert**, abhängig von der Entfernung (+19 … +56). Es gibt keine Lichttextur und keinen eigenen Pass.
2. Weil nur an den Ecken gerechnet und dazwischen interpoliert wird, wirkt das Licht auf großen Dreiecken (Fassade, Straße) weich, aber ohne Kegelform und mit groben Übergängen.
3. Die Lichtkegel sind eine eigene, halbtransparente Fläche mit Textur `40EB6B06`; die Halos und Flares sind addierte Sprites (§2).

### 5.3 Ansatz für echtes Scheinwerferlicht (Vorschlag, nicht begonnen)

- **Im Pixelshader statt als Zusatz-Pass:** Die Vertices tragen `rhw`, der Pixelshader kennt also die Tiefe pro Pixel. Mit Bildmitte und Brennweite (die ASI kennt das Sichtfeld, Hor+) lässt sich die Position jedes Pixels im Kameraraum rekonstruieren, die Flächen-Normale aus `ddx/ddy`. Ein Spotlicht-Kegel mit weichem Rand und Abfall über die Entfernung wird dann zur Vertexfarbe addiert, bevor sie mit der Textur multipliziert wird, wie es die Engine auch tut.
- **Nötig aus der Engine** (Hook über die ASI): ob die Scheinwerfer an sind sowie Lage und Richtung der Scheinwerfer im Kameraraum. Außerdem muss der Vertex-Anteil der Engine abgeschaltet werden, sonst wird doppelt beleuchtet. Dafür muss die Funktion gefunden werden, die den Anteil berechnet.
- **Stand der Suche:** Im Ghidra-Export gibt es keinen Funktionsnamen und keinen String mit „headlight“. `FUN_0047dcf0` (`porsche.exe`) liest Szenenobjekte mit `dynamicLight` und `radius` (vermutlich Streckenlampen aus `.scn`), `FUN_004409d0` liest `lightdecal`. Der Weg zur Scheinwerfer-Funktion führt eher über die Texturen `40EB6B06`/`E1FD7F61` (wer zeichnet sie?) oder über einen Schreibzugriff auf die Vertexfarben.
- **Schneller Teilgewinn ohne Engine-Hook:** Lichtkegel (`40EB6B06`), Halos und Flares als HD-Texturen per FSH-Repack verschönern.

## 6. Engine-Suche, Runde 1 (2026-10-16)

### 6.1 Statische Spuren (Ghidra, `porsche.exe`)

| Spur | Fund | Bewertung |
|---|---|---|
| `Controller.Lights` | nur in `FUN_0051d1d0`/`FUN_0051dcc0`/`FUN_0051e030`: Frontend-Tastenbelegung (`FEControllerInputAccessor`) | Sackgasse für die Beleuchtung; die Taste wird im Rennen über eine Aktionsnummer ausgewertet, nicht über den String |
| Flare-Baustein `FUN_00451710` (`FLARE_WIDTH_SCALE` 0x00451824 aus `addresses.h`) | Aufrufer `FUN_00460290`, `FUN_00460460`: Sonnen-Flare mit Sichtprüfung per Bild-Rücklesen (vgl. `FixScreenReadback`) | Sonnen-Flare, nicht die Scheinwerfer |
| `dynamicLight` / `radius` in `FUN_0047dcf0` | Parser für Szenenobjekte (`.scn`) | vermutlich Streckenlampen; für Scheinwerfer zweitrangig |
| Hubschrauber-Suchlicht (`CAR_LIST_POLICE`, `addresses.h`) | zweites dynamisches Licht der Engine | möglicher Vergleich, sobald die Scheinwerfer-Funktion gefunden ist |

### 6.2 Messbuild 9.4: Aufrufer-Adressen in der F11-Aufnahme

Alle Dreieck-, Quad- und Sprite-Exporte von `dx11z` merken sich ihre Rücksprungadresse (`_ReturnAddress`) und weitere Rahmen über die EBP-Kette (`RtlCaptureStackBackTrace`, ab dem zweiten Rahmen nur plausibel). Die F11-Aufnahme schreibt zusätzlich je Aufrufkette und Textur `M3T   caller #n tex … : N calls, from porsche.exe:XXXXXXXX < … `, Adressen wie in Ghidra (`porsche.exe` ab 0x400000, `gimme.dll` ab 0x10000000), die 120 häufigsten.

**Testfahrt:** wie 9.2c (stehen, Kamera fest, Licht aus → F11, an → F11, aus → F11). Gesucht: Aufrufer von `40EB6B06` (Lichtkegel), `E1FD7F61` (Halo) und `C4B0AEFA` (aufgehellte Straße). Von dort führt der Weg in Ghidra zur Funktion, die die Vertexfarben berechnet.

### 6.3 Ergebnis Messfahrt 9.4

Daten: `work/logs/9.4-aufrufer/`. Aufnahmen #1 aus (36,2 s), #2 an (44,8 s, gewichtetes Mittel 91/87/77), #3 aus (54,2 s); Licht-Effekt wie in 9.2c reproduziert.

| Draws | direkter Aufrufer | Funktion (Ghidra) |
|---|---|---|
| Straße `C4B0AEFA`, Fassade `E99B6163`, `B0C375E6` (deckend und Alpha) | `porsche.exe:00544A00` | `FUN_00543ae0` (3895 B) |
| Lichtkegel `40EB6B06`, Halo `E1FD7F61`, Flares `10D45A01` | `porsche.exe:00546E1D` | `FUN_00545ad0` (4987 B) |

- Beide Funktionen haben in Ghidra **keine Aufrufer** und rufen dieselben Hilfsfunktionen (`FUN_00536240`, `FUN_00549630`, `FUN_00549380`, `FUN_0054c030`): Sie sind Methoden einer Render-Klasse und werden über eine Tabelle aufgerufen.
- `FUN_00543ae0` übernimmt **fertige** Positionen, Werte und Farben (Parameter 1–3, Farben als 2 Dwords je Vertex = Diffuse + Specular), clippt (`FUN_005498b0`/`FUN_0054c030`), projiziert und zeichnet. **Die Scheinwerfer-Beleuchtung wird also vorher berechnet**, beim Aufrufer.
- Die EBP-Kette war leer (die Engine nutzt keine Rahmenzeiger); die Rahmen 2–4 fehlen deshalb.
- Die Aufrufer-Liste war auf 120 Einträge je Aufnahme begrenzt; die Lichtkegel fehlen deshalb in #2 (nicht aussagekräftig).

### 6.4 Messbuild 9.4b: Stack-Scan

Statt der EBP-Kette durchsucht `NoteCaller` den Stack nach Werten, die in den Code von `porsche.exe`/`gimme.dll` zeigen und direkt hinter einem `call` liegen (`E8`, `FF 15`, `FF 5x`, `FF Dx` …). Bis zu 5 weitere Aufrufer je Draw, 400 Zeilen je Aufnahme. Weiter oben in der Kette sind Fehltreffer möglich (alte Werte auf dem Stack); Ketten, die in allen Aufnahmen gleich auftreten, sind belastbar.

**Testfahrt:** wie 9.2c/9.4. Gesucht: die Funktion, die für `C4B0AEFA` mit und ohne Licht verschiedene Farben an `FUN_00543ae0` übergibt.

### 6.5 Ergebnis Messfahrt 9.4b: Aufrufketten

Daten: `work/logs/9.4b-stackscan/`. In allen drei Aufnahmen gleich (belastbar):

- **Welt** (`C4B0AEFA`, `E99B6163`): `00544A00` (in `FUN_00543ae0`) < `0053F714` (in `FUN_0053f620`) < `0053F223` (in `FUN_0053f1b0`) < `00450CFE` (in `FUN_00450ca0`) < `004668E6` (`FUN_004668d0`) < `00466699` (`FUN_00466660`).
- **Licht-Sprites** (`E1FD7F61`, `40EB6B06`): `00546E1D` (in `FUN_00545ad0`) < [`005B0822`/`005387B7`] < `0053F734` (in `FUN_0053f620`) < `0053F223` < `0059F20D` (`FUN_0059f1d0`) < `0043D470` (`FUN_0043cdd0`, Halos) bzw. `00423A98` (Kegel).

Mehrere dieser Funktionen fehlten im Ghidra-Export (nur über Tabellen erreichbar). Anfänge aus den `call`-Zielen und Füllbytes ermittelt, dekompiliert mit `GhidraNFSP/DecompileRebuild.java` (headless, `-readOnly`), Ausgaben in `work/re/light1–4.c` (lokal).

### 6.6 Render-Pipeline (Ghidra)

- `FUN_00466660`: Leert die Render-Warteschlangen (deckend, Alpha, …) und ruft je Eintrag den Zeichen-Rückruf.
- `FUN_00450ca0` (Tabelle `005B3CF8`): Rückruf für Strecken-Meshes. `FUN_0044a250` verknüpft die Datenströme des Mesh (Positionen, Farben, …), danach geht es in die Verarbeitungskette.
- `FUN_0053f1b0`: läuft eine **Kette von Verarbeitungsknoten** ab (`puVar1 = node->vtbl[0](…)`). 14 Knotenklassen mit gemeinsamer Basis (Slots 1/2 = `0053FDB0`/`0053F490`), Tabellen `005BBC3C` … `005BBE10`, Stride 0x24.
- Knoten `005BBC60` → `FUN_0053f620`: **Zeichnen**, verteilt je Modus auf `FUN_00540ce0`/`005483c0`/`00543ae0` (Welt)/`00545ad0` (Sprites).
- Knoten `005BBD80` → `FUN_005749c0`: **Transformation** (Matrix × Position, je Vertex).
- Knoten `005BBD5C` → **`FUN_00574440`: Beleuchtung je Vertex.** Liest eine Lichterliste (Strom 10: Anzahl, je Licht Vektor `+1`, Typ `+0x21`, Farbe `+0x2A`), Normalen (Strom 1/6), Positionen (1/7), Materialfarbe (5) und je Vertex Gewichte (6/9). Je Licht: Typ-abhängig `dot(Normale, Richtung)` × Farbe (diffus), Gewicht × Farbe (Typ 2) oder Spiegelanteil (Typ 3); Summe × Materialfarbe, auf 1 begrenzt, als ARGB in den Farbstrom. Keine Entfernungsdämpfung und kein Kegel im Code sichtbar.

**Offene Frage:** Ob die Scheinwerfer als Eintrag in dieser Lichterliste ankommen (und als welcher Typ), oder ob die Streckenfarben woanders aufgehellt werden.

### 6.7 Messbuild 9.4c: Hook auf den Lichtknoten

Bei `Measure ≥ 1` ersetzt `dx11z` den Tabelleneintrag `005BBD5C` durch einen Wrapper (nur wenn das Image bei 0x400000 liegt und der Eintrag `00574440` enthält; Log `M3T light hook installed`). Der Wrapper ruft die Originalfunktion unverändert auf. In der F11-Aufnahme:
- `M3T   light node: N calls` und je unterschiedlicher Lichterliste `M3T   lights #n: k lights` mit Typ, Vektor und Farbe je Licht.
- Aufrufer-Zeilen mit `lit 1`, wenn vor dem Draw der Lichtknoten lief.

**Testfahrt:** wie 9.4b (stehen, Licht aus → F11, an → F11, aus → F11). Erwartung: Mit Licht erscheint ein zusätzliches Licht in der Liste, und `C4B0AEFA` hat `lit 1`, oder die Straße läuft gar nicht durch den Lichtknoten (dann sitzt die Aufhellung woanders).

### 6.8 Ergebnis Messfahrt 9.4c

Daten: `work/logs/9.4c-lichtknoten/`. Hook aktiv (`M3T light hook installed at 005BBD5C`), Licht-Effekt wie zuvor (gewichtetes Mittel 71 → 88 → 71).

- **Der Lichtknoten `FUN_00574440` läuft im Rennen nie** (0 Aufrufe in allen drei Aufnahmen, alle Draws `lit 0`). Er dient vermutlich Showroom/Garage. **Die Scheinwerfer-Aufhellung der Strecke entsteht nicht in der Knotenkette**, sondern vorher: Die Farben kommen schon aufgehellt als Datenstrom des Mesh an (`FUN_0044a250` verknüpft sie nur).
- Geprüft und verworfen (Ghidra): `FUN_0053f530` (kopiert Vertexdaten, richtet über `FUN_00540820`/`FUN_005408f0` Sichtfeld, Nebel und Clipping ein), `FUN_005406a0` (Sprungtabelle nach Clipmodus).

### 6.9 Nächster Ansatz (Vorschlag)

**Hardware-Datenhaltepunkt auf den Farbstrom der Straße:** `dx11z` merkt sich bei F11 den Farbzeiger, den `FUN_00543ae0` für `C4B0AEFA` bekommt (Parameter 3), setzt über einen Hilfsthread ein Debug-Register (DR0, „bei Schreiben“) auf diese Adresse im Render-Thread und fängt den Treffer mit einem Vectored Exception Handler ab. Geloggt werden die schreibende Instruktion (EIP) und ein Stack-Scan. Das zeigt direkt die Funktion, die die Scheinwerfer-Farbe einrechnet. Risiko: Absturz bei Fehlern im Handler (nur im Messbuild, `Measure ≥ 1`); Aufwand 1 Build + 1 Fahrt.

Alternativen: (a) Hook auf `FUN_00543ae0` und Vergleich von Farbzeiger und -inhalt mit/ohne Licht (zeigt, ob derselbe Puffer überschrieben oder ein anderer gewählt wird, aber nicht wer); (b) Abbruch der Engine-Suche, nur HD-Texturen für Kegel/Halos.

### 6.10 Messbuild 9.4d: Schreib-Haltepunkt auf die Straßenfarbe

Nur bei `Measure ≥ 1`, nur auf `porsche.exe` v3.5 (Prüfung der Bytes vor dem Patchen):
- Der Aufruf `call FUN_00543ae0` bei `0053F70F` (im Zeichenknoten `FUN_0053f620`) wird auf einen Wrapper umgebogen, der die Originalfunktion unverändert aufruft (Log `M3T road draw hook installed`).
- Im F11-Bild sucht der Wrapper beim ersten Draw der Straße (`C4B0AEFA`) die zuletzt übergebene Vertexfarbe in den drei Eingangsströmen (Log `M3T watch: diffuse found in stream n at +X`, dazu die ersten 8 Dwords jedes Stroms). Ohne Treffer wird Strom 3 beobachtet.
- Auf diese Adresse setzt ein Hilfsthread DR0 („Schreiben, 4 Byte“) im Render-Thread. Ein Vectored Exception Handler zählt 3 s lang jede Schreibstelle (EIP nach dem Schreibbefehl) mit Stack-Scan und schaltet sich dann ab. Beim nächsten Flip: `M3T watch report` und je Schreibstelle `M3T   write after porsche.exe:XXXXXXXX …`.
- Grenzen: nur Schreibzugriffe aus dem Render-Thread; schreibt ein anderer Thread, bleibt der Bericht leer.

**Testfahrt:** wie 9.4c (stehen, Licht aus → F11, an → F11, aus → F11, zwischen den Tasten mindestens 4 s warten).

### 6.11 Ergebnis Messfahrt 9.4d

Daten: `work/logs/9.4d-watch/`. Beide Hooks aktiv.

- Die Eingangsströme von `FUN_00543ae0` sind andersherum als vermutet: **Strom 1 = Vertexfarben** (ARGB, Straße `FF4D4841`), Strom 2 = leer, **Strom 3 = Texturkoordinaten** (Floats 0 / 0,5 / 10 / 11).
- Licht aus: Die gezeichnete Farbe `FF4D4841` steht unverändert in Strom 1. **Licht an:** gezeichnet wird `FF757069` (+40 je Kanal), Strom 1 enthält weiter `FF4D4841`.
- Schreib-Haltepunkt auf Strom 1 bzw. 3: **0 Schreibzugriffe** in allen Aufnahmen. Die Quellfarben sind statisch; die Scheinwerfer-Farbe wird nicht in die Mesh-Daten geschrieben.
- `FUN_00549630`, `FUN_00549380` (Ghidra): Tiefensortierung der Dreiecke, kein Licht.

**Schluss:** `FUN_00543ae0` kopiert die Farben in sein eigenes Vertex-Array (`this+0x434`, 32 Byte je Vertex = TL-Vertex des Treibers; Diffuse bei `+0x444`) und übergibt dieses Array über `DAT_006bd9ac` an den Treiber. Mit Licht verändert also jemand **dieses Array** zwischen Kopie und Übergabe.

### 6.12 Messbuild 9.4e: Haltepunkt auf das Vertex-Array des Renderers

(Ab 9.4e2 wird die Aufnahme mit **Rollen** statt F11 ausgelöst, §6.13.)

Wie 9.4d, aber DR0 liegt auf `renderer+0x444` (Diffuse von Vertex 0); Fenster 1 s statt 3 s, weil jeder Draw dorthin schreibt. Zusätzlich im Log: die Farbe von Vertex 0 im Array direkt nach dem Draw. Erwartung: mit Licht eine zusätzliche Schreibstelle außerhalb der Kopierschleife von `FUN_00543ae0`.

### 6.13 Nebenbeobachtung: F11 ist eine Spieltaste (Szenerie-Detail in Stufen)

👤 (9.4d, Gegenprobe mit `Measure = 0`): F11 schaltet zyklisch: 1. Druck keine Änderung, 2. entfernte Gebäude verschwinden, 3. teilweise zurück, 4. alle zurück (`doc/screenshots/9.4d/`). Also eine mehrstufige Sichtweiten-/Detailtaste.

- **Nicht der Mod:** Weder `nfs5_enhanced` noch `dx11z` werten F11 aus (Quellen durchsucht: nur der Alt+F4-Hook, sonst keine Tastenabfrage); `dx11z` las F11 nur für die Momentaufnahme.
- **Das Spiel selbst:** `FUN_0053b530` (aufgerufen vom `WM_KEYDOWN`-Handler `0x0053B450`, `KEY_DOWN_HANDLER` in `addresses.h`) übersetzt Windows-Tasten in eigene Tastencodes, darunter **alle Funktionstasten F1–F12** (`VK_F11` → `0x8700`), Bild/Pos/Ende/Pfeile/Einfg/Entf und den Ziffernblock. Wer `0x8700` auswertet, ist noch nicht gefunden (kein direkter Vergleich im Export; vermutlich über eine Tabelle).
- **Folge für die Messungen:** In 9.2c–9.4d hat jeder F11-Druck auch die Detailstufe weitergeschaltet. Die Werte je Textur (Straße `C4B0AEFA`, gleiche Fläche in allen Aufnahmen) bleiben gültig; das flächengewichtete Gesamtmittel ist leicht beeinflusst (#3 lag trotzdem wieder bei den Werten von #1).
- **Ab Build 9.4e2:** Momentaufnahme auf **Rollen** (Scroll Lock), die das Spiel nicht übersetzt.
- Möglicher Bezug zu B2 („Map-Teil nicht geladen“, `BUGS.md`): Die Stufen könnten dieselbe Szenerie-Auswahl steuern. Nicht geprüft.

### 6.14 Ergebnis Messfahrt 9.4e: Scheinwerfer-Funktion gefunden

Daten: `work/logs/9.4e-arraywatch/` (Auslöser Rollen). Schreib-Haltepunkt auf die Diffuse von Vertex 0 im Vertex-Array des Renderers (`renderer+0x444`):

| Zustand | Schreibstelle für die Strecken-Meshes | Wert |
|---|---|---|
| Licht aus (#1, #3) | `porsche.exe:0045352C` (≈ 11 000×/s) | Grundfarbe, z. B. `FF3B2D1F` |
| Licht an (#2) | **`porsche.exe:00453373` / `0045337F`** (≈ 10 600×/s) | aufgehellt, z. B. `FFAFA193` |

Beide liegen in **`FUN_00453110`**, einem Knoten der Kette (`0053F223`) für Strecken-Meshes (`00450CFE`). Eine zweite, gleich gebaute Funktion `FUN_004535c0` (Schreibstellen `00453BE1`/`004537F6`) dient anderen Szenerie-Objekten. Die übrigen Schreibstellen (`00446A3A` …, `005443BD`, `005465AE` …) gehören zu anderen Objektarten und ändern sich mit dem Licht nicht.

### 6.15 Der Scheinwerfer-Algorithmus der Engine (Ghidra, `work/re/light7.c`, `light8.c`)

**`FUN_00453110` (Knoten, Strecken-Meshes):**
1. Licht an? `FUN_00443660(car+0x74C, 0)` oder `(…, 1)` für das **Spielerauto** `CAR_LIST_RACERS[DAT_0065741c]` (linker/rechter Scheinwerfer; `FUN_00443660` liest ein Byte aus der Tabelle `[this+0x2D4]`).
2. Wenn an: `FUN_00452910(n, positionen, kameramatrix)` füllt je Vertex einen Lichtfaktor in `DAT_0061bd60[]`.
3. Je Vertex: `farbe = grundfarbe + lichtfarbe × faktor` je Kanal, auf 255 begrenzt, Alpha 255. Lichtfarbe: `DAT_0061bd4c` (ARGB). Faktor ≤ 0 → nur Grundfarbe.
4. Wenn aus: Grundfarbe kopieren.
5. Danach, falls `DAT_0061e180 ≠ 0`: `FUN_00452a50` mit einer zweiten Nachschlagetabelle (Abstand in beide Richtungen) für ein weiteres Licht.

**`FUN_00452910` (Lichtfaktor):**
- Positionen mit Scheinwerfer-Matrix `DAT_0061bccc` (4×4, mit der Kameramatrix verknüpft) in den Lichtraum (`DAT_0061d178`, 16 Byte je Vertex: x, y, z).
- `iz = round(z × s_z)` (0 < iz < 127, vorwärts), `ix = round(x × s_x)` (−63 … 63, seitlich), `iy = |round(y × s_y)|` (Höhe). Skalen: `s_z = DAT_0061bd54`, `s_x = DAT_0061bd58` (beim ersten Aufruf übernommen), `s_y` = Konstante bei `005CEC60`.
- `faktor = (lichtkarte[iz × 128 + ix + 64] − iy) × k` (`k` bei `005CEC62`). **Die Lichtkarte** ist ein **128 × 128-Byte-Feld** (Zeiger `DAT_0061e1cc`), also die Form des Lichtkegels in der Draufsicht.

**Folge:** Das Licht ist eine projizierte Lichtkarte, nur an den Ecken ausgewertet. Grobe Dreiecke ergeben deshalb den weichen, ungenauen Verlauf aus §1.

## 7. Weg zu echtem Scheinwerferlicht pro Pixel (Vorschlag)

Dasselbe Modell der Engine pro Pixel statt pro Vertex:
1. **Daten je Bild aus dem Spiel** (Hook bei `FUN_00453110`/`FUN_00452910`): Licht an/aus, Scheinwerfer-Matrix (Licht ↔ Kamera), Skalen, Lichtfarbe, Zeiger auf die Lichtkarte.
2. **Lichtkarte als Textur** (128 × 128, einmal hochladen; bilinear gefiltert, optional geglättet oder durch eine schönere Karte ersetzt).
3. **Vertex-Anteil abschalten:** `FUN_00452910` liefert 0, dann kopiert `FUN_00453110` nur die Grundfarbe.
4. **Pixelshader:** Position im Kameraraum aus Bildposition und Tiefe (`rhw`) zurückrechnen, mit der Matrix in den Lichtraum, Lichtkarte nachschlagen, `lichtfarbe × faktor` zur Vertexfarbe addieren, dann wie gewohnt mit der Textur multiplizieren. Nur für Draws aus `FUN_00453110`/`FUN_004535c0` (markiert über den Hook).
5. Optional: Kegelrand weicher, Abfall über die Entfernung, Normale aus `ddx/ddy` für Licht nur auf zugewandten Flächen.

Nächster Schritt: Messbuild, der die Lichtkarte als BMP speichert und Matrix, Skalen und Farbe loggt (Grundlage für Schritt 1–2).

### 7.1 Messbuild 9.5: Daten der Scheinwerfer-Berechnung

Nur bei `Measure ≥ 1`. Der Aufruf `call FUN_00452910` bei `0045321A` (in `FUN_00453110`, cdecl `(n, positionen, kameramatrix)`) wird auf einen Wrapper umgebogen, der das Original unverändert aufruft (`M3T headlight hook installed`). Im Rollen-Bild mit Licht an schreibt er einmal:
- Lichtfarbe `DAT_0061bd4c`, Flags `DAT_0061bd5c`, `DAT_0061e180`, Spieler-Index `DAT_0065741c`, Zeiger der Lichtkarte `DAT_0061e1cc`.
- Skalen: `s_x` (`0061D170`, Rohwert `0061BD58`), `s_z` (`0061E178`, Rohwert `0061BD54`), `s_y` = 32 (`005CEC60`), `k` = 1/256 (`005CEC64`); zweites Licht: 3,5 / 16 / 1/512 (`005CEC6C`/`70`/`68`).
- Matrizen: Kamera (Parameter 3), Scheinwerfer `0061BCCC`, zweites Licht `0061E184`.
- Für die ersten 12 Vertices: Eingangsposition, Lichtraum-Koordinaten (`DAT_0061d178`), Faktor (`DAT_0061bd60`).
- Einmal je Lauf die Lichtkarte als `m3t_headlight_map_128x128.bmp` und `.raw` neben dem Log, mit Wertebereich.

Damit lässt sich der Faktor offline nachrechnen (Abnahme 9.5).

### 7.2 Ergebnis Messfahrt 9.5: Modell bestätigt

Daten: `work/logs/9.5-headlight/` (Log, `m3t_headlight_map_128x128.bmp/.raw`, vergrößert `9.5-lichtkarte-x3.png`).

| Größe | Wert |
|---|---|
| Lichtfarbe `DAT_0061bd4c` | `FF808080` (Grau 128, additiv) |
| Flags `DAT_0061bd5c` / zweites Licht `DAT_0061e180` | `3` (beide Skalen übernommen) / `0` (aus) |
| Skalen | `s_x` = 3,62, `s_z` = 1,28, `s_y` = 32, `k` = 1/256 |
| Kameramatrix (Parameter 3) | Einheitsmatrix → Eingangspositionen sind **Weltkoordinaten** |
| Scheinwerfer-Matrix `0061BCCC` | Rotation + Translation, Zeilenvektor-Konvention: `licht = (x, y, z, 1) × M` |

- **Offline nachgerechnet:** Für alle 12 geloggten Vertices stimmen Lichtraum-Koordinaten (±0,02) und Faktor (exakt) mit der Engine überein. Formel: `iz = round(z·1,28)` (0 < iz < 127), `ix = round(x·3,62)` (−63 < ix < 63), `iy = |round(y·32)|`, `faktor = (karte[iz·128 + ix + 64] − iy) / 256`, Beitrag `= 128 × faktor` je Kanal.
- **Lichtkarte:** weicher, ovaler Fleck (Werte 0–255, 13 061 von 16 384 ≠ 0), großflächig auf 255 gesättigt, Rand weich. Reichweite vorwärts ≈ 99 Einheiten (127 / 1,28), seitlich ± 17 Einheiten (63 / 3,62); Höhe löscht das Licht nach 8 Einheiten (255 / 32). **Kein Kegel** und keine zwei Scheinwerfer, sondern ein Fleck vor dem Auto.

**Folge für 9.6:** Das Modell lässt sich 1:1 im Pixelshader nachbilden (Weltposition je Pixel → `× M` → Karte nachschlagen, bilinear). Darüber hinaus kann eine bessere Karte (zwei Kegel, Abfall über die Entfernung, heller Kern) dieselbe Formel nutzen, ohne Engine-Änderung außer dem Abschalten des Vertex-Anteils.

### 7.3 Umsetzung 9.6: Scheinwerfer pro Pixel (`[Dx11] Headlights`)

| Wert | Wirkung |
|---|---|
| `0` | Original: Licht nur an den Vertices (Hooks nur bei `Measure ≥ 1`) |
| `1` | dasselbe Modell **pro Pixel**; der Vertex-Anteil der Engine wird abgeschaltet |
| `2` | beides gleichzeitig (nur zum Vergleich, doppelt hell) |

**Engine-Seite** (nur auf `porsche.exe` v3.5; alle vier Aufrufstellen werden vor dem Patchen geprüft, sonst bleibt `Headlights = 0`):
- `call FUN_00452910` bei `0045321A` (Strecken-Meshes, `FUN_00453110`) und `004536B0` (weitere Szenerie, `FUN_004535c0`): Original läuft, danach baut der Treiber die Konstanten für das Mesh und setzt (Modus 1) die Faktoren `DAT_0061bd60[0…n)` auf 0. Dann kopiert die Engine nur die Grundfarbe.
- `call FUN_00543ae0` (`0053F70F`) und `call FUN_00545ad0` (`0053F72F`): Nach dem Zeichnen endet das Mesh; der Treiber leert den Batch, damit kein anderes Objekt das Licht erbt.

**Konstanten je Mesh:**
- Kameramatrix `MV = [[[[0x69ED0C]]+0x14]+0x60]` (wie im Transformationsknoten `FUN_005749c0` / `FUN_00539f30`), affin invertiert; `ViewToLight = MV⁻¹ × L[0061BCCC]`.
- Projektion des Renderers (`[0x69E0D4]+0x2C/0x30/0x38/0x3C` = fx, fy, cx, cy): `x = xv·fx/zv + cx`.
- Skalen `0061D170`/`0061E178`/`005CEC60`, Farbe `0061BD4C`, Lichtkarte `[0061E1CC]` als R8-Textur 128×128 (t2/s2, bilinear, Clamp), neu hochgeladen bei geändertem Inhalt.

**Shader:** Vertex-Shader: `zv = 1/rhw`, Kameraraum zurückrechnen, `× ViewToLight`, als `TEXCOORD2` ausgeben (affin im Raum, daher pro Pixel exakt interpoliert). Pixel-Shader: gleiche Formel wie die Engine (`iz`, `ix`, `iy`, Grenzen 0,5 < iz < 126,5, |ix| < 62,5), Lichtkarte bilinear statt gerundet, `diffuse += farbe × faktor` vor der Textur-Stufe.

**Prüfung im Log** (`Measure ≥ 1`, Rollen bei Licht an): `Headlights check: v0 … -> light x y z, game x y z` vergleicht die aus dem Treiber-Vertex zurückgerechnete Lichtraum-Lage mit dem Ergebnis der Engine (`DAT_0061d178`). Stimmen beide, sitzt das Licht pro Pixel an derselben Stelle wie das Original.

### 7.4 Testfahrt 9.6 (erster Build): kein Licht in der Umgebung

👤: Lichtkegel-Sprites sichtbar (3rd-Person und Cockpit), aber **kein Licht auf Straße und Fassaden** (`doc/screenshots/9.6/headlights1_kein_licht.webp`). Daten: `work/logs/9.6-pixel1/`.

- Hooks und Lichtkarte aktiv (`Headlight hooks installed`, `light map … uploaded`). Der Vertex-Anteil der Engine war abgeschaltet, der Pixel-Anteil traf daneben.
- Prüf-Log: zurückgerechnete Lichtraum-Lage ≈ (225…247, −39…−62, 331…366) statt Engine-Werte (−37 … 48, −0,6 … 15, −30 … 25), also ≈ Verschiebung der Scheinwerfer-Matrix. **Ursache:** Die gelesene „Kameramatrix“ `[[[[0x69ED0C]]+0x14]+0x60]` ist die **Objektmatrix** (für die Strecke die Einheitsmatrix), nicht die Ansicht. Außerdem fehlte die Projektionsmatrix (die Bildumrechnung `x = xv·fx/zv + cx` war zu einfach).
- Projektion des Renderers (4K): fx = 1920, fy = 1080, cx = 1920,01, cy = 1080,01, d. h. `x_s = x_c/w_c · 1920 + 1920`.

### 7.5 Korrektur 9.6b: Matrizen der EAGL-Kamera

Ghidra (`work/re/light9.c`): Der Renderer hält in `renderer[0]` seine EAGL-Kamera (`EaglCamera`, `structs.h` des Mods). `FUN_005393a0` baut daraus `kamera+0x188 = Objekt × Ansicht` und `kamera+0x108 = (Objekt × Ansicht) × Projektion (+0x148)`; `FUN_005408f0` setzt `+0x108` als aktuelle Matrix (`FUN_00538160`), `FUN_005385b0` rechnet damit die Vertices in den Clip-Raum (Ziel `renderer+0x434`), `FUN_00543ae0` teilt durch `w` und bildet auf den Bildschirm ab.

Neue Rückrechnung (CPU, je Mesh): `(x_c, y_c, w_c, 1)` mit `x_c = (x_s − cx)/fx · w_c`, `y_c = …`, `w_c = 1/rhw` → `× inv(P′)` (Spalten 0, 1, 3 der Projektion) → `× inv(Objekt × Ansicht)` → `× L[0061BCCC]`. Der Shader bleibt unverändert, nur die Matrix ist neu. Das Prüf-Log gibt zusätzlich beide Kameramatrizen aus.

### 7.6 Testfahrt 9.6b: Scheinwerfer pro Pixel funktioniert ✅

👤 (2026-10-16): „Das hat gut funktioniert.“ Screenshots `doc/screenshots/9.6/headlights2_pixel_an.webp` (Licht an, `Headlights = 1`) und `headlights2_aus.webp` (Licht aus), gleiche Stelle:
- Fassade und Straße werden an derselben Stelle wie im Original hell, mit **weichem, rundem Verlauf ohne Dreieckskanten**; die Farben werden neutraler (Tür türkis, Wand rosa) wie beim Original-Licht.
- Kein doppeltes Licht, keine Fehlfarben, keine sichtbaren Nähte zwischen Meshes.
- Das gesicherte Log (`work/logs/9.6b-pixel/`) stammt von einem späteren Start ohne Rennen (keine Prüfzeilen); die Abnahme beruht auf der Sichtprüfung.

**Stand:** Das Original-Modell läuft 1:1 pro Pixel. Die Form bleibt der ovale Fleck der Lichtkarte (§7.2).

**Mögliche nächste Schritte (Phase 9, offen):**
1. Eigene Lichtkarte: zwei Scheinwerferkegel, heller Kern, Abfall über die Entfernung (gleiche Formel, nur die Textur ändert sich; optional höhere Auflösung).
2. Feinheiten: Licht nur auf zugewandten Flächen (Normale aus `ddx/ddy`), Höhe weicher.
3. Gleiches Vorgehen für die zweite Lichtquelle (`FUN_00452a50`, aktiv wenn `DAT_0061e180 ≠ 0`) und die Szenerie über `FUN_004535c0` (schon gehookt, Sichtprüfung offen).
4. Live-Reflexionen am Auto (Weg A) und Rückspiegel (Phase 9 ursprünglich).

### 7.7 Scheinwerfer bei Tag (9.6c, `[Dx11] HeadlightsDay`)

Bei Tag hat das Spiel keine Scheinwerfer: Alles hängt am **Nachtmerker `DAT_00628a28`** (16 Bit, 16 Code-Stellen). Nur nachts …
- lädt `FUN_00452590` die Lichtkarte aus `GameData/Render/night.psh` (`DAT_0061e1cc`),
- setzt `FUN_00452680(spielerauto)` Lichtfarbe `FF808080`, Skalen (z 1,28 = `3FA3D70A`, x 3,62 = `4067AE14`) und `DAT_0061bcc8` = Auto,
- rechnet `FUN_004526c0` jedes Bild die Scheinwerfer-Matrix `0061BCCC` aus Position (`car+0x330`) und Drehung (`car+0x364`) des Autos und wählt das nächste **Polizeiauto mit Blaulicht** (`DAT_0061e180`, Farben `FFFF0000`/`FF0000FF`, zweites Licht über `FUN_00452a50`).

**Umsetzung im Treiber:**
- `night.psh` = Container `SHPP` mit einem 8-Bit-Eintrag `nght` 128 × 128, Pixel ab `0x28`; **identisch** mit der nachts gemessenen Lichtkarte (§7.2). Bei Tag lädt der Treiber sie selbst.
- Tabelleneinträge der Farbknoten `005B3B70`/`005B3B9C` (`FUN_00453110`) und `005B3B44` (`FUN_004535c0`, beide thiscall, `ret 8`) zeigen auf Wrapper. Bei Tag und Licht an: einmal je Bild `DAT_0061bcc8` = Spielerauto, `0061BD50` = 0, Nachtmerker für den Aufruf von `FUN_004526c0` kurz auf 1 und zurück, danach `DAT_0061e180` = 0 (kein Blaulicht am Tag). Dann dieselbe Pixel-Beleuchtung wie nachts mit den Nachtwerten für Farbe und Skalen.
- `HeadlightsDay`: 0 aus, 1 wenn der Lichtschalter des Autos an ist (gleiche Abfrage wie `FUN_00453110`), 2 immer. Ob das Spiel den Lichtschalter am Tag überhaupt umschaltet, ist ungeprüft; falls nicht, wirkt nur 2.

**Status:** gebaut und installiert, im Spiel ungetestet.

### 7.8 Test 9.6c bei Tag: kein Licht; Diagnose und Tag-Schalter (9.6d)

👤: Bei Tag wirft der Scheinwerfer kein Licht. Log (`work/logs/9.6c-tag1/`): Tag-Hooks installiert (`scenery nodes hooked`), aber **keine Lichtkarte hochgeladen**. Der Tag-Pfad ist also vor `PrepareHeadMesh` ausgestiegen. Mögliche Gründe: Lichtschalter-Byte bleibt am Tag 0 (`HeadlightsDay = 1`), die Farbknoten `FUN_00453110`/`FUN_004535c0` laufen am Tag nicht (andere Knotenkette ohne Licht), oder Spieler/Auto ungültig.

Build 9.6d:
- **Diagnose** (`Measure ≥ 1`, alle 10 s): `M3T   day lights: N node calls (night, no car, switch off, prepared, failed), M mesh draws (lit), night flag, player, light bytes`.
- **Tag-Schalter** in `[Dx11]` (nur Mod, keine eigene Kartendatei, 👤-Entscheidung): `HeadlightsDayStrength` (Anteil der Nachthelligkeit, 0–4, Standard 1), `HeadlightsDayColour` (RRGGBB, Standard `808080` wie das Spiel), `HeadlightsDayRange` (Reichweite, 0,25–4, Standard 1; teilt die Skalen x/z).

### 7.9 Ergebnis 9.6d und Korrektur 9.6e: Tag-Hook auf den Strecken-Rückruf

Daten: `work/logs/9.6d-tag2/`. Diagnose eindeutig: **0 Aufrufe der Farbknoten** `FUN_00453110`/`FUN_004535c0` am Tag bei bis zu 1,3 Mio. Mesh-Draws je 10 s. Die Farbknoten mit dem Scheinwerfer-Code gibt es nur in der Nacht-Knotenkette.

Neuer Tag-Hook (9.6e): **`FUN_00450ca0`**, Eintrag 2 der Tabelle `005B3CF0` (`005B3CF8`, thiscall, `ret 8`), zeichnet die Strecken-Abschnitte (Straße, Gebäude) bei Tag und Nacht. Nachbarn: `FUN_00450de0` (Warteschlange, LOD/Cull), `FUN_00450d10` (Sonderobjekte Typ 2/3/4); weitere Szenerie über `FUN_00450380` (noch nicht einbezogen).
- Bei Tag (Nachtmerker 0) markiert der Treiber alle Draws innerhalb des Rückrufs; Ende des Rückrufs = Batch leeren, Licht aus.
- Die Matrizen werden zusätzlich direkt vor `call FUN_00543ae0` neu gelesen, weil `kamera+0x188` erst in der Knotenkette (`FUN_005393a0`) auf den Abschnitt gesetzt wird.
- Die Licht-Bytes des Autos werden in jedem Modus gelesen und in der Diagnose ausgegeben; für den Test `HeadlightsDay = 2` (immer an), damit sichtbar wird, ob das Spiel den Schalter am Tag umschaltet.

### 7.10 Test 9.6e: kein Licht; Korrektur 9.6f

Daten: `work/logs/9.6e-tag3/` (Fahrt mit `Measure = 0`, daher ohne Bericht). Der Log zeigt keine Zeile „light map … uploaded“: Die Lichtkarte wurde nie hochgeladen, der Tag-Pfad kam also nie bis zur Licht-Vorbereitung durch.

Wahrscheinliche Ursache: 9.6e bereitete das Licht schon beim Eintritt in `FUN_00450ca0` vor. Dort ist `kamera+0x188` für den Abschnitt noch nicht gesetzt (siehe §7.9), die Umkehrung bzw. Prüfung scheitert, und `m_DayMesh` blieb für den ganzen Abschnitt aus. Danach wurden auch die Vorbereitungen vor `FUN_00543ae0` nie mehr versucht.

Korrektur 9.6f:
- Eintritt in den Rückruf schaltet nur noch scharf (`m_DayMesh`); die Vorbereitung läuft vor **jedem** `FUN_00543ae0` innerhalb des Abschnitts, ein Fehlschlag betrifft nur diesen Draw.
- Die Lichtkarte wird bei Tag unabhängig von den Matrizen geladen.
- Einmalige Log-Zeilen auch ohne `Measure` („Headlights by day: first track chunk draw / player car / first lit track draw / track draw not lit, step N failed“; Schritt 1 Kamera, 2 Umkehrung, 3 Viewport, 4 Karte).

### 7.11 Test 9.6f: Hook läuft, Matrizen nicht umkehrbar; Korrektur 9.6g

Daten: `work/logs/9.6f-tag4/` (mit `Measure = 1`).
- Der Strecken-Rückruf läuft am Tag (~120 000–170 000 Abschnitte je 10 s), die Lichtkarte aus `night.psh` wird geladen.
- Die **Licht-Bytes schalten am Tag um** (`light bytes 1 1` nach Betätigen des Schalters) → Stufe 1 (`HeadlightsDay = 1`) ist möglich.
- Jede Vorbereitung vor `FUN_00543ae0` scheitert an Schritt 2 (Umkehrung, Determinante ≈ 0).

Deutung: `FUN_00543ae0` aktualisiert die Kameramatrizen selbst (lazy über `FUN_005393a0`: `+0x188`/`+0x148`/`+0x108` werden erst neu gerechnet, wenn sich Kamera oder Objekt geändert haben). Vor dem Aufruf stehen dort am Tag noch keine gültigen Werte.

Korrektur 9.6g: Vor `FUN_00543ae0` wird das Licht nur angemeldet; vorbereitet wird es beim ersten Treiber-Draw innerhalb des Aufrufs (`BeginBatch`/`DrawRaw`), nach dem Aufruf wieder abgeschaltet (Sprites bleiben unbeleuchtet). Scheitert die Vorbereitung, loggt der Treiber bis zu dreimal Kamera, Viewport und beide Matrizen.

### 7.12 Test 9.6g: Projektion bei Tag ohne w-Spalte; Korrektur 9.6h

Daten: `work/logs/9.6g-tag5/`. Die Vorbereitung im ersten Treiber-Draw scheitert weiter an Schritt 2, der Matrix-Dump zeigt warum:
- `kamera+0x188` (Objekt × Ansicht) ist gültig (Drehung + Verschiebung).
- `kamera+0x148` (Projektion) ist am Tag `diag(0,9774; −1,7376; −1; 1)`, Spalte 3 = `(0, 0, 0, 1)`. Das w der Clip-Koordinaten ist dort konstant 1. Die Tiefe zum Teilen liefert Spalte 2 (`z_c = −z_Ansicht`).
- Die Rückrechnung aus §7.5 nimmt fest die Spalten 0, 1 und 3 → singulär.

Korrektur 9.6h: Sind die Einträge 3, 7, 11 der Projektion ≈ 0, nimmt der Treiber Spalte 2 als w. Einmalig geloggt werden die Projektion (Tag/Nacht, gewählte Spalte) und nach dem ersten beleuchteten Draw die Lichtraum-Position von vier Vertices („Headlights by day check“) zur Plausibilitätsprüfung.

### 7.13 Test 9.6h: Scheinwerfer bei Tag funktionieren ✅

Daten: `work/logs/9.6h-tag6/`, Screenshot `doc/screenshots/9.6h/tag_licht_felsen.webp` (Lichtfleck auf Straße und Felswand vor dem Auto, 👤 „Es hat funktioniert!“).
- Erkannt: Tag-Projektion mit w aus Spalte 2; alle Vorbereitungen gelingen (bis ~510 000 beleuchtete Draws je 10 s, 0 Fehlschläge).
- Lichtschalter am Tag: `light bytes 1 1` nach dem Einschalten → `HeadlightsDay = 1` (nur bei eingeschaltetem Licht) ist nutzbar.
- Die Vertex-Prüfzeilen („Headlights by day check: v…“) blieben leer: Der Vertex-Puffer bei `renderer+0x434` enthält am Tag nach dem Draw keine Werte mit `rhw > 0`; ohne Einfluss auf das Licht.
- Noch nicht einbezogen: weitere Szenerie über `FUN_00450380` (falls einzelne Objekte unbeleuchtet bleiben).

## 8. Plan: eigene Lichtkarte und Lichtfarbe nach Baujahr (9.7–9.9)

### 8.1 Eigene Lichtkarte nur im Treiber (9.7)

Die Form des Lichts ist die 128×128-Helligkeitskarte aus `GameData/Render/night.psh` (§7.7). Zeilen = Entfernung (`z · 1,28`), Spalten = seitlich (`x · 3,62 + 64`), Wert − `|y · 32|` = Faktor. Die Farbe kommt getrennt dazu (§7.2).

Vorschlag: neuer INI-Schalter `[Dx11] HeadlightsMap = <datei>.bmp` (Graustufen, 128² oder größer, z. B. 512²). Der Treiber lädt die Datei statt der Karte des Spiels, bei Tag und Nacht (Modus `Headlights = 1`); die Spieldateien bleiben unverändert. Bei größeren Karten werden die Skalen so angepasst, dass dieselbe Fläche abgedeckt bleibt. Dazu eine mitgelieferte Karte mit zwei Kegeln, hellem Kern und Abfall über die Entfernung.

Nicht betroffen: Lichtkegel- und Halo-Sprites am Auto (Textur `40EB6B06`, §5); Einfärben wäre ein eigener Schritt.

### 8.2 Baujahr live aus dem Spiel (9.8)

Ziel: das Baujahr des Spielerautos zur Laufzeit lesen statt aus einer festen Tabelle, damit auch Zusatz- und Mod-Autos ohne Pflege funktionieren.

**Befund (2026-10-17):**
1. **Quelle:** Jede Fahrzeug-Datei `GameData/Simulation/CarData/<Auto>.sim` beginnt mit dem Modellnamen, die ersten vier Zeichen sind das Baujahr: `1956 356 A coupe 1.6L`, `1989 964 Carrera 3.6L`, `2000 996 Turbo coupe 3.6L`, `1997 Boxster 2.5L`. Das gilt für alle 87 Dateien, auch für `default.sim` (`1998 996 Carrera 3.4L`) und `1951356 coupe1.3L` (ohne Leerzeichen). Die Frontend-Texte `FEData/Locale/<Jahr><Modell>.loc` tragen das Jahr ebenfalls, aber nur im Dateinamen.
2. **Im Speicher:** `FUN_00410d60` legt für jedes Rennauto aus der Tabelle `0x5E8B34` einen Block `carSpecs` (0x1C4 Byte) an und speichert den Zeiger in `car + 0x760`. `FUN_0049c6c0` kopiert die `.sim` dorthin (`%s%s.sim`, ersatzweise `default.sim`; im Netzspiel aus den Netzdaten, gleiches Format). Danach rechnet es nur Zahlenfelder ab `+0x44` um, der Name ab `+0` bleibt unverändert.
3. **Zugriff:** `car` ist das Fahrzeug-Objekt, das LFSP als `model + 0xF4` kennt (gleiche Offsets `0x52C` Flags, `0x558` Konfiguration). Baujahr = `atoi` der ersten vier Zeichen von `*(char **)(car + 0x760)`, nur gültig im Rennen.

Damit funktionieren auch Zusatz- und Mod-Autos ohne Tabelle, solange ihre `.sim` dem Namensschema folgt. Ein Rückfall (Tabelle in der INI) ist erst nötig, wenn ein Auto ohne Jahr im Namen auftaucht; dann liefert der Wert 0 und das Licht bleibt bei der bisherigen Farbe.

**Umsetzung 9.8a (LFSP v6a, damals `nfsp_carparts.asi`):** Im Modell-Update liest LFSP für jedes Rennauto einmal den Namen aus `carSpecs` und schreibt `Baujahr: Modell … 'Auto' Spieler/Gegner: carSpecs '<Name>' -> <Jahr>` ins Log. Das Jahr des Spielerautos stellt die ASI über den Export `int LfspPlayerCarYear()` (bis v6a `NfspPlayerCarYear`) bereit (0 = unbekannt oder Menü). Lesen mit `__try`, ein ungültiger Zeiger ergibt 0.

**Test 9.8a (2026-10-17, 👤, Log `work/logs/9.8a-lfsp/nfsp_carparts_v6a.log`):** drei Rennen. `1960 356 B cabrio S-90 1.6L` → 1960, `1987 959 coupe 2.9L` → 1987, `1998 Porsche GT3 3.6L` → 1998; jeweils Spieler und alle 7 Gegner richtig.

Rückfall, falls es kein Jahresfeld gibt: Jahr aus dem Menütext des Modells parsen; als letzte Stufe Tabelle nach Modellname mit Überschreibung in der INI.

### 8.2a Aufteilung Licht: dx11z und LFSP (Entscheidung 2026-10-17)

Wunsch 👤: Das Licht soll in die eigene Mod (damals „Carparts“, seit 2026-10-17 **LFSP – Love for Speed Porsche**, `lfsp.asi`), weil beide die Fahrzeuge erweitern. Technische Grenze: Das Licht pro Pixel entsteht im Shader von `dx11z.dll`. Nur der Renderer hat die Draws, Konstanten und Texturen, und die Hooks an `0x45321A`/`0x4536B0`, Straße, Sprites und Strecke markieren die Draws im selben Bild. Diese Teile bleiben in dx11z.

| Teil | Ort |
|---|---|
| Shader, Lichtkarte als Textur, Matrizen und Draw-Hooks (9.6, 9.7) | `dx11z.dll` |
| Fahrzeug-Seite: Baujahr (9.8), Lichtfarbe nach Baujahr (9.9), Fernlicht-Taste (9.10), später Rücklichter (11.5) und Instrumente nach Lichtschalter (11.3) | `lfsp.asi` |
| Schnittstelle | Exporte der ASI (`LfspPlayerCarYear`, später z. B. `LfspHeadlightParams` mit Farbe, Stärke, Karte, Fernlicht). dx11z holt sie über `GetModuleHandle("lfsp.asi")` + `GetProcAddress`. Fehlt die ASI, verhält sich dx11z wie bisher |

**Alle Licht-Schalter** liegen ab 9.11b in `lfsp.ini` Abschnitt `[Lights]` (Wunsch 👤: die Schalter der Mod gehören in die Mod-INI), auch die, die dx11z auswertet (`Headlights`, `HeadlightsDay…`, `HeadlightsMap`, `HeadlightsOpponents…`). dx11z liest `lfsp.ini` neben der `Porsche.exe`; fehlt dort ein Schlüssel, gilt noch `[Dx11]` in `nfs5_enhanced.ini` (ältere Installationen). In `nfs5_enhanced.ini` bleiben nur die Renderer-Fehlerkorrekturen (`AlphaEdgeFix`, `ForceTwoSided`, `Measure` …).

### 8.3 Lichtfarbe nach Baujahr (9.9)

Schalter `[Dx11] HeadlightsColourByYear = 1` (0 = bisherige Farbe). Farbtemperatur aus dem Jahr, fließend überblendet:

| Baujahr | Technik | Farbtemperatur | Farbe etwa |
|---|---|---|---|
| bis ~1965 | Glühlampe, 6 V | ~2400 K | `FFA54F` |
| ~1965–1985 | Halogen H4 | ~3000–3200 K | `FFC184` |
| ~1985–1997 | Halogen, verbessert | ~3400 K | `FFCC99` |
| ab ~1998 | Xenon (Litronic) | ~4300–5000 K | `FFF0E0` |

Optional Helligkeit mitkoppeln (6-V-Anlagen deutlich schwächer). Gilt für Tag und Nacht; nachts ersetzt die Farbe den Spielwert `808080` nur im Pixel-Modus. Stützpunkte per INI überschreibbar.

### 8.3a Umsetzung 9.9a (LFSP v6c, dx11z 9.9a)

- **LFSP** (`lfsp.ini [Lights]`): `HeadlightsColourByYear = 1` und Stützpunkte `YearN = Jahr, RRGGBB, Helligkeit` (Standard: 1955 `FFA54F` 0,75 · 1970 `FFC184` 1,0 · 1990 `FFCC99` 1,0 · 1998 `FFF0E0` 1,15). Zwischen den Punkten wird linear überblendet, davor und danach gilt der Randpunkt. Die Farbe wird auf Luminanz 1 normiert (Rec. 601) und mit der Helligkeit multipliziert. So ändert sich nur der Farbton, die Helligkeit nur über den eigenen Faktor. Export `int LfspHeadlightTint(float rgb[3])` liefert die Faktoren und das Jahr (0 = aus, Menü oder Jahr unbekannt).
- **dx11z** (`Driver::ApplyLfspTint`, am Ende der Licht-Vorbereitung je Mesh): sucht einmalig `lfsp.asi` und den Export und multipliziert die bisherige Lichtfarbe (nachts `0x61BD4C` = `808080`, tags `HeadlightsDayColour` × `HeadlightsDayStrength`) mit den Faktoren. Ohne LFSP bleibt alles wie in 9.7a. Log: `Headlights: LFSP tint for model year …` bei jedem Wechsel.
- Ergebnis bei grauem Spiellicht `808080` (Faktoren → Farbe): 1950 1,05/0,68/0,33 → `86572A`; 1960 → `8F6236`; 1975 → `9E7955`; 1987 → `9A7A5A`; 1994 → `9A8672`; ab 1998 1,21/1,14/1,06 → `9B9288`.

**Test 9.9a (2026-10-17, 👤, `doc/screenshots/9.9a/`):** Farbe nach Baujahr wirkt (356 gelb, 901 warm, 996 fast weiß). Befund: Der Lichtfleck mischt sich nicht mit farbigem Umgebungslicht. Unter orangem Straßenlicht wirkt er gelbgrün statt heller orange, unter grünem Licht gelblich-weiß.

**Ursache und Fix 9.9b (dx11z):** Der Shader rechnete `saturate(Vertexfarbe + Scheinwerfer)` **vor** der Textur (wie D3D7, das Vertexfarben auf 0–1 begrenzt). Bei orangem Umgebungslicht liegt Rot schon nahe 1 und wird gekappt, nur Grün und Blau wachsen. Der Fleck kippt deshalb zur Farbe des Scheinwerfers bzw. zu Weiß. Ab 9.9b bleibt die Summe ungekappt, die Textur-Stufe (`RunStage`) kappt erst nach dem Multiplizieren: Ergebnis = Textur × (Umgebung + Scheinwerfer). Shader mit `fxc` (ps_4_0/vs_4_0) offline geprüft.

**Test 9.9b (👤, `doc/screenshots/9.9a/9.9b_gruene_umgebung.webp`):** Unter grünem Umgebungslicht mischt sich das Licht jetzt, „sieht realistisch aus“.

### 8.4 Fernlicht (9.10)

Wunsch 👤: Solange `L` gedrückt ist, leuchtet das Fernlicht. Mit der eigenen Karte aus 9.7 ist das einfach: Der Treiber wählt pro Bild zwischen zwei Karten, nur im Pixel-Modus und nur bei eingeschaltetem Licht.
- `[Lights] HeadlightsMapHigh = <datei>.bmp` (in `lfsp.ini`) als zweite Karte (mitgeliefert: schmaler, weit reichend). Ohne Datei wird die Abblendkarte genutzt und in der Reichweite gestreckt (Skala `z` × `HeadlightsHighRange`, Standard 2) und angehoben (`HeadlightsHighStrength`). „Nach oben schieben“ entspricht hier der längeren Reichweite, denn die Karte liegt in der Fahrbahnebene; zusätzlich kann der Höhenabzug (`|y · 32|`) für das Fernlicht verringert werden, damit Wände und Bäume höher hinauf beleuchtet werden.
- Grenze der Karte: Der Lichtraum reicht beim Original bis ~99 Einheiten (`z · 1,28 < 126,5`). Für größere Reichweite muss die Skala kleiner werden (die Karte deckt dann mehr Fläche ab), nicht die Karte länger.
- Taste: `[Lights] HeadlightsHighKey = L` (in `lfsp.ini`) (virtueller Tastencode, nur bei aktivem Spielfenster, `GetAsyncKeyState`). Vorher prüfen, ob das Spiel `L` selbst belegt (Tastenbelegung im Optionsmenü, Übersetzung `FUN_0053b530`, vgl. F11 in §6.13).
- Optional: Flares/Halos am Auto bei Fernlicht heller (Sprite-Textur `40EB6B06`), eigener Schritt.

### 8.4b Umsetzung 9.10a: Fernlicht (dx11z 9.10a, LFSP v6e)

**Taste:** Die Belegung steht in den Spielständen `SaveData/*.sav` als Einträge `Controller.<Aktion>` mit DirectInput-Scancode (Byte nach `00 03` + 7 Nullbytes) und Zeichen. In allen drei Profilen (`XXDefXX`, `2Sam`, `7testo`) gilt: **`Lights` = `0x26` = L** (Lichtschalter des Spiels), `Hazards` = T, `LeftTurnSignal` = W, `RightTurnSignal` = E, `Horn` = H, `LookBehind` = B, `CameraView` = C, `ResetCar` = R, `Pause` = P, `ShiftUp`/`ShiftDown` = A/Z, Look-Tasten Y/U/I, Handbremse Leertaste. L scheidet fürs Fernlicht also aus. Standard ist **K** (gedrückt halten), einstellbar.

**Umsetzung:**
- **LFSP** (`lfsp.ini [Lights]`): `HeadlightsHighKey` (Standard K), `HeadlightsHighFade` (Überblenden, 120 ms). Export `float LfspHighBeam()`: 0 = Abblendlicht, 1 = Fernlicht, nur im Rennen und bei aktivem Spielfenster.
- **dx11z** (`Driver::ApplyHighBeam`, je Bild einmal abgefragt, nur für das eigene Licht): Reichweite × `HeadlightsHighRange` (z-Skala geteilt), Breite × `HeadlightsHighWidth` (x-Skala geteilt), Helligkeit × `HeadlightsHighStrength`, Höhenabzug × `HeadlightsHighHeight` (kleiner = das Licht reicht an Wänden und Bäumen höher hinauf). Standard 2,0 / 0,8 / 1,3 / 0,5. Alle Werte werden mit dem Überblendanteil gemischt. Konstante `g_HeadHigh` (Offset 864) mit den Faktoren für die z-, x- und y-Skala.
- Wirkt nur, solange das eigene Licht an ist (Fernlicht ohne Abblendlicht gibt es nicht). Die Gegner bleiben auf Abblendlicht.
- Grenze: Mit Reichweite 2 deckt die Karte ~200 Einheiten ab, der Verlauf wird entsprechend gröber (Karte 512² reicht dafür). Eine eigene Fernlicht-Karte (`HeadlightsMapHigh`) ist noch nicht umgesetzt.

**Test 9.10a (2026-10-17, 👤, `doc/screenshots/9.10a/`):** Fernlicht wirkt, ist aber zu schmal (unrealistisch); gewünscht ist nur „nach oben strecken“. Außerdem ließ es sich laut 👤 ohne eingeschaltetes Abblendlicht aktivieren. Das Log zeigt nur `high beam on/off`, keine Licht-Bytes. dx11z prüfte den Lichtschalter fürs Fernlicht nicht selbst.

**9.10b (dx11z):** Standard jetzt Höhenabzug 0,4 (das Licht reicht an Wänden 2,5-mal so hoch), Helligkeit 1,2, Reichweite 1,0 und Breite 1,0 (unverändert gegenüber dem Abblendlicht; beides bleibt einstellbar). Das Fernlicht wirkt nur, wenn die Licht-Bytes des Spielerautos an sind. Log beim Drücken und Loslassen: `high beam key down/up, light bytes a b -> high beam on/off`.

**9.10g (2026-10-22, 👤 Wunsch):** `HeadlightsHighHeight` gibt jetzt direkt die Höhe an Wänden an (1 = wie Abblendlicht, 2 = doppelt so hoch, 0,5–10, Standard 2,5). dx11z nimmt intern den Kehrwert als Faktor auf den Höhenabfall (vorher stand der Faktor selbst in der INI: alter Wert 0,4 = neu 2,5, 0,2 = 5).

**Klarstellung 👤 zu 9.10a:** Das Fernlicht *soll* auch ohne eingeschaltetes Abblendlicht gehen (wie eine Lichthupe), tat es aber nicht. 9.10b hatte es irrtümlich zusätzlich an den Lichtschalter gekoppelt. Log 9.10b: Die Taste wurde nur bei eingeschaltetem Licht gedrückt (`light bytes 1 1`).

**9.10c (dx11z):** Bei gedrückter Fernlicht-Taste gilt das eigene Licht als an, auch bei ausgeschaltetem Schalter. Nachts läuft das dann über den Strecken-Pfad `FUN_00450ca0` (die Spielmatrix `0x61BCCC` baut das Spiel nachts in jedem Bild, auch bei ausgeschaltetem Licht), mit der Spielfarbe `0x61BD4C`. Bei Tag gilt dasselbe, unabhängig von `HeadlightsDay`. Grenze: Der Strecken-Pfad erfasst die Streckenteile; ob weitere Szenerie (`FUN_004535c0`) mitbeleuchtet wird, ist ungeprüft.

**Test 9.10c (2026-10-17, 👤, Log `work/logs/9.10c/`):** ✅ Fernlicht funktioniert auch ohne Abblendlicht (23 Tastendrücke bei `light bytes 0 0`).

**Nebenbefund für 11.5:** Das Spiel kennt Blinker und Warnblinker (`LeftTurnSignal`, `RightTurnSignal`, `Hazards`, Strings in `FUN_0051d1d0`). Deren Zustand am Auto ist noch nicht gesucht.

### 8.4a Scheinwerfer der Gegner (9.11, Machbarkeit)

Wunsch 👤: Gegner, die nachts mit Licht fahren, sollen die Strecke ebenfalls beleuchten. Das Spiel kennt nur **ein** Scheinwerferlicht (Spielerauto, Matrix `0x61BCCC`) und ein zweites Licht für das Blaulicht der Polizei (`0x61E180`/`0x61E184`, §7.13).

Befund aus `FUN_004526c0`: Die Licht-Matrix entsteht nur aus Position `car + 0x330` und Drehung `car + 0x364` des Autos (`FUN_004661e0`/`FUN_00466220`/`FUN_00466280`, dann `FUN_00539cd0`). Dasselbe lässt sich für jedes Auto aus der Tabelle `0x5E8B34` rechnen. Ob ein Auto Licht an hat, steht wie beim Spieler in den Licht-Bytes `[[car + 0x74C] + 0x2D4]`.

Weg (alles in dx11z, Fahrzeug-Seite optional in LFSP):
1. Je Bild bis zu 8 Lichter sammeln: Spieler plus Gegner mit eingeschaltetem Licht, nach Abstand zur Kamera sortiert (ab ~100 Einheiten unwirksam, Lichtraum §8.4).
2. Shader: Der Vertex-Shader gibt die Kameraposition statt der Lichtposition weiter. Der Pixel-Shader rechnet je Licht `view × ViewToLight[i]` und summiert die Beiträge (gleiche Karte, Farbe je Licht, z. B. nach Baujahr des Gegners).
3. Offen: Die Pixel-Beleuchtung wirkt heute nur auf Meshes, für die das Spiel seine Lichtfunktion aufruft (nachts Hook auf `FUN_00452910`). Liegen Meshes nur im Lichtbereich eines Gegners, nicht im Bereich des Spielers, ruft das Spiel sie eventuell nicht auf. Dann den Strecken-Rückruf `FUN_00450ca0` wie bei Tag nutzen (§7.8).
4. Gegner-Autos selbst anstrahlen (11.4) ist ein eigener Schritt: Fahrzeug-Meshes laufen nicht durch die Strecken-Lichtfunktion.

Aufwand: mittel (Matrizen-Array im Konstantenpuffer, Schleife im Shader, Messlauf zu Punkt 3). Leistung: bis 8 Kartenzugriffe je Pixel, unkritisch.

**Umsetzung 9.11a (dx11z 9.11a, LFSP v6d):**
- **Matrix je Auto:** `FUN_004526c0` baut die Spieler-Matrix über `FUN_00466220`/`FUN_00466280` (Kamera-Objekt: Drehung transponiert, Translation `−pos` gedreht) und `FUN_00539cd0` (3×3 + Translationszeile → 4×4). Das ist die Inverse der Welt-Matrix des Autos. dx11z rechnet sie selbst (`CarObjectToLight`: Zeilen aus `car + 0x364`, Position `car + 0x330`, `InvertAffine`, plus Versatz `0x61BD50` auf Element 14), ohne globale Werte des Spiels anzufassen. Ob die Drehung zeilen- oder spaltenweise gespeichert ist, prüft dx11z einmal nachts gegen die Spielmatrix `0x61BCCC` des Spielers (Log `car matrix check … rows/transposed`). Passt keine Variante (Abweichung ≥ 0,05), bleiben die Gegner aus.
- **Lichter:** einmal je Bild alle Rennautos aus `0x5E8B34` (Anzahl `0x658080`, höchstens 9) außer dem Spieler, deren Licht-Bytes `[[car + 0x74C] + 0x2D4]` nicht beide 0 sind. Defekte oder ausgeschaltete Scheinwerfer strahlen also nicht, wie beim Spieler im Spiel. Höchstens 7 Gegner-Lichter.
- **Farbe:** nachts die Spielfarbe `0x61BD4C`, tags `HeadlightsDayColour` × `HeadlightsDayStrength`, × `HeadlightsOpponentsStrength`, × Farbton nach Baujahr des Gegners aus LFSP (`LfspHeadlightTintForCar(car, rgb)`, über das Fahrzeug-Objekt, das LFSP beim Baujahr-Lesen merkt).
- **Shader:** Der Vertex-Shader gibt zusätzlich die Kameraposition (`m_View`) weiter. Der Pixel-Shader rechnet je Gegner `view × ViewToLight[i]` mit derselben Karte und denselben Skalen und addiert die Beiträge zur Umgebung (wie 9.9b, ungekappt vor der Textur). Konstantenpuffer: `g_Extra` (Anzahl) bei 288, `g_ExtraColour[7]` bei 304, `g_ExtraViewToLight[7]` bei 416, geprüft mit dem `fxc`-Listing.
- **Welche Meshes:** Ist das Spielerlicht nachts an, ruft das Spiel `FUN_00452910` für jedes Strecken-Mesh auf, und dx11z hängt die Gegner dort an. Ist es aus (oder kaputt) bzw. bei Tag ohne Spielerlicht, nimmt dx11z den Strecken-Rückruf `FUN_00450ca0` wie bei Tag (§7.8) und zeichnet nur die Gegner-Lichter (Spielerfarbe 0). Bei Nacht gilt dort keine `HeadlightsDayRange`.
- **Schalter** (`lfsp.ini [Lights]`, ab 9.11b): `HeadlightsOpponents` (1 an, 0 aus), `HeadlightsOpponentsStrength` (0–4, Standard 1,0).
- **Nicht enthalten:** Gegner-Autos selbst werden nicht angestrahlt (11.4); ein einzelner defekter Scheinwerfer halbiert das Licht nicht (die Karte hat beide Kegel, Licht an, solange ein Byte ≠ 0).
- **Log:** `Headlights opponents: N of M racers with light (night/day, mask …)` bei jeder Änderung.

**Test 9.11b (2026-10-17, 👤):** „Die Funktionen sind solide, keine Probleme.“ Das danach gesicherte `dx11z.log` stammt aus einem Tagrennen (`night flag 0`, Spielerlicht aus). Dort fehlen `car matrix check` und `Headlights opponents`: Die Prüfung der Matrix-Anordnung lief nur nachts, bei Tag blieben die Gegner deshalb aus.

**Befund im Log zu 9.10a (Nacht):** `car matrix check … difference rows 47.09, transposed 587.5 -> no match, opponents off`. Die Gegner-Scheinwerfer waren also **auch nachts nie aktiv**, das „solide“ aus dem 9.11b-Test betraf nur das eigene Licht. Die Drehung stimmt (`FUN_00532a00` ist eine Transposition, bestätigt), die Translation nicht: `FUN_00532a40` dreht `−pos` über den Matrix-Stapel der Engine (`FUN_00559a40`/`FUN_0055a930`), Zeilen- oder Spaltenform unbekannt. Meine Nachbildung nahm die Zeilenform an.

**9.11d (dx11z):** `CarObjectToLight` baut die Matrix jetzt direkt wie `FUN_004526c0` (3×3 transponiert oder wie gespeichert, Translation `−pos` in Zeilen- oder Spaltenform, vier Varianten) und prüft alle vier gegen `0x61BCCC`. Passt keine, schreibt das Log Position, Drehung und die Spielmatrix für die Auswertung.

**Test 9.11d (2026-10-17, 👤, Log `work/logs/9.11d/`, `doc/screenshots/9.11d/`):** ✅ Gegner werfen nachts Licht, unabhängig vom eigenen. Log: `transposed/row 0` (exakt), `stored/row 617.6`, `transposed/column 617.6`, `stored/column 1.97` → Variante 1, `7 of 8 racers with light`. Variante 1 ist mathematisch dieselbe wie die erste Nachbildung (Inverse). Die frühere Abweichung von 47 kam daher vermutlich von einer veralteten `0x61BCCC` im Prüfmoment (Bild davor). **9.10c:** Die Prüfung wird deshalb bis zu 20-mal wiederholt, statt nach dem ersten Fehlschlag aufzugeben.

**Fix 9.11c (dx11z 9.10a):** Bei Tag baut der Strecken-Pfad die Spielermatrix über `FUN_004526c0` jetzt auch dann einmal, wenn das eigene Licht aus ist und die Anordnung noch nicht geprüft wurde. Die Prüfung läuft dann im selben Bild (`m_GameMatrixFlip`).

### 8.5 Umsetzung 9.7a: `[Dx11] HeadlightsMap`

- Der Treiber lädt das BMP einmal (8 Bit mit Palette, 24 oder 32 Bit, unkomprimiert; Graustufe = Luminanz) als eigene Textur in voller Größe. Es ersetzt die Karte des Spiels bei Tag und Nacht. Fehlt die Datei, nimmt er die Spielkarte und loggt das.
- Der Shader tastet unverändert mit normierten Koordinaten ab (`(ix + 64,5)/128`, `(iz + 0,5)/128`), größere Karten ergeben also automatisch feinere Verläufe auf derselben Fläche; Grenzen wie beim Original (`0,5 < iz < 126,5`, `|ix| < 62,5`).
- Ausrichtung: unterste Bildzeile = Kartenzeile 0 (direkt vor dem Auto), oberste = fern (~99 Einheiten), linke Spalte = `ix = −64` = links vom Auto (in Fahrtrichtung). Geprüft 2026-10-17 (👤): Schriftzug „RECHTS“ in der rechten Kartenhälfte erscheint rechts vor dem Auto und lesbar, die Karte wird also weder gedreht noch gespiegelt (`doc/screenshots/9.7a/`).
- Mitgeliefert (`gen_headlight_map.py`): `headlights_low.bmp` 512² mit zwei Lampen bei `x = ±0,8`, breitem Kegel (σ ≈ 24°) und hellem Kern (σ ≈ 6°), Abschattung durch die Haube bis ~12 Einheiten, Abfall `1/(1 + (z/38)²)`, weich auslaufend vor den Kartenrändern; `headlights_original.bmp` = Spielkarte als Vorlage.
- Test 9.7a (2026-10-17, 👤): eigene Karte wirkt bei Tag und Nacht (Boxster am Tag vor der Felswand, 911 nachts), Ausrichtung wie oben.
- Nachts wirkt die eigene Karte nur im Pixel-Modus (`Headlights = 1`); mit `Headlights = 2` rechnet das Spiel zusätzlich mit seiner Karte an den Vertices.

## 9. Rücklichter, Bremslicht, Blinker (11.5)

Wunsch 👤 (2026-10-17): Rücklichter mit leichtem rotem Schein, immer sichtbar (Tag und Nacht), beim Bremsen etwas stärker; Blinker als optionales Ziel; eine im Spiel beschädigte Leuchte strahlt nicht.

### 9.1 Leuchten-Tabelle am Modell (Ghidra und Disassembly, `work/re/11.5_lamps*.c`)

- `FUN_00443660(model, n)` liefert Byte `n` der Tabelle `[model + 0x2D4]`. `car + 0x74C` zeigt auf dieses Modell (dasselbe Objekt wie die Licht-Bytes der Scheinwerfer, Byte 0 und 1).
- Die Leuchten-Schicht des Automodells (Code ab `0x43D840`, von Ghidra nicht als Funktion erkannt, mit capstone disassembliert) geht die Listeneinträge durch. Bei Einträgen vom Typ 5 liest sie die Lampennummer aus dem Kopf (`[eintrag + 0x44] + 0xC`) und zeichnet die Leuchte nur, wenn das Tabellen-Byte ≠ 0 ist (`0x43D8C4`). Bei gesetztem Flag `0x08` an `+0x48` folgt eine gespiegelte Hälfte (x-Werte negiert) mit eigener Lampennummer an `+0x4E` (`0x43DAC9`).
- Folge: Jede Leuchte des Modells (Scheinwerfer, Rücklicht, Bremslicht, Blinker …) hat eine Nummer in dieser Tabelle. Ist das Byte 0 (aus oder kaputt), zeichnet das Spiel sie nicht. Das ist der Mechanismus „defekte Leuchte strahlt nicht“. Die Einträge enthalten auch die Lage der Leuchte am Modell; damit lässt sich der Schein später genau an den Rücklichtern ansetzen.
- Das Spiel kennt die Aktionen `Lights` (L), `Hazards` (T), `LeftTurnSignal` (W), `RightTurnSignal` (E) (`FUN_0051d1d0`, Belegung §8.4b).

### 9.2 Messlauf 11.5a (dx11z)

dx11z loggt in jedem zweiten Bild die ersten 16 Bytes der Tabelle des Spielerautos, wenn sie sich ändern (`Lamps: player car … table …: xx …`, höchstens 400 Zeilen). Ablauf 👤: Nachtrennen, nacheinander Licht an/aus (L), Bremsen, Blinker links (W), rechts (E), Warnblinker (T), Rückwärtsgang, dann Heck anstoßen (Rücklicht beschädigen). Daraus ergibt sich, welches Byte welche Leuchte ist und ob Bremse und Blinker in der Tabelle stehen.

### 9.3 Leuchten-Logik des Spiels (Disassembly `0x439360`, von Ghidra nicht als Funktion erkannt)

Je Modell-Update setzt das Spiel alle Bytes über `FUN_00443670(model, n, wert)` (schreibt `[model + 0x2D4][n]`) aus dem Zustand des Autos `car = [model + 0xF4]` neu:

| Byte | Quelle | Bedeutung |
|---|---|---|
| 0, 1 | Bit 0 von `car + 0xD87` | Scheinwerfer (Lichtschalter) |
| 2, 3 | `car + 0xD7F` bzw. `+ 0xD80` (je nach `car + 0xD98`) > 0 | Bremslicht |
| 4 / 6 | Bit 3 von `car + 0xD87`, Takt `FUN_0053c2e0 / ([0x5DEB48] / 2)`, 4 und 6 gegenphasig | Blinker links (zwei Leuchten) |
| 5 / 7 | Bit 4 von `car + 0xD87`, wie oben | Blinker rechts |
| — | `car + 0xB50 == 3` | beide Blinker (Warnblinker) |
| 8 | `car + 0xD82 == 0` | Rückfahrlicht |
| 9 | immer 0 | – |
| 10, 11 | Blinker links/rechts, sonst Bremse | kombinierte Brems-/Blinkleuchte |
| 12, 13 | immer 1 | Rücklicht |

Danach (ab `0x4398C6`) setzt das Spiel Bytes beschädigter Leuchten auf 0; der Schaden wirkt also je Leuchte und überschreibt den Schalter. Mit `c = [model + 0xF4]` und der Schwelle `[0x5B2408]` gilt: `c + 0x4E4` > Schwelle → 0 und 4 aus (vorn links), `+ 0x4EC` → 1 und 5, `+ 0x4F4` → 3, 7, 11, `+ 0x4FC` → 2, 6, 10, `+ 0x4F8` → 8, `+ 0x504` → 12 und 13 (Disassembly 11.5i). (`0x4396BF` ist die Klappen-Logik, nicht der Schaden.)

**Klappscheinwerfer:** Gleich danach vergleicht das Spiel Bit 0 (Lichtschalter) mit `model + 0x567`. Ändert es sich, speichert es den neuen Wert und startet die Klappen-Animation über das Objekt `[model + 0x64]` (zeitgesteuerte Kanäle mit Startzeit, Dauer, Von- und Zielwert, vgl. `FUN_00438440`).

**Messlauf 11.5a** (👤, 944 nachts, Log `work/logs/11.5a/`): bestätigt Bremse (2/3, 10/11), Rückfahrlicht (8), Rücklicht (12/13 = 1), Schäden je Leuchte (Byte 0 bleibt nach einem Treffer 0, nach Heck-Treffer links 2 und 10 = 0). Blinkende Bytes kamen im Lauf nicht vor (ob W/E/T gedrückt wurden, ist offen).

### 9.4 Lichthupe öffnet Klappscheinwerfer (9.10d, LFSP v6h)

Wunsch 👤: Beim 944 öffnet L die Klappscheinwerfer, K (Fernlicht der Mod) nicht. Umsetzung in LFSP (`UpdateFlasher`, je Modell-Update des Spielerautos): Solange die Fernlicht-Taste gedrückt ist und der Lichtschalter aus ist, setzt LFSP Bit 0 von `car + 0xD87`. Die Leuchten-Logik des Spiels schaltet dann im selben Ablauf die Scheinwerfer-Bytes ein und öffnet die Klappen wie mit L, und auch das eigene Licht des Spiels (und damit das Pixel-Licht über den normalen Weg) ist an. Beim Loslassen nimmt LFSP das Bit zurück; hat der Spieler währenddessen selbst mit L geschaltet, bleibt es, wie er es gesetzt hat. Schalter `[Lights] HeadlightsFlashSwitch` (1 an, 0 nur Fernlicht der Mod). Grenze: Die Klappen brauchen ihre Öffnungszeit, ein sehr kurzes Antippen zeigt das Licht vor dem vollständigen Öffnen.

**Test 9.10d (👤, Log `work/logs/9.10d/`):** Die Klappen öffnen mit K, aber (1) die Heckleuchten gehen mit an, weil Bit 0 der vollständige Lichtschalter ist; (2) nach kurzem Antippen bleibt das Licht an. LFSP setzte und löschte das Bit sauber (29 × gesetzt, 29 × zurückgenommen).

**Klappen-Logik im Detail (`0x43950E`–`0x439627`):** Bei einem Wechsel des Schalters gegenüber `model + 0x567` speichert das Spiel den neuen Wert und startet den Kanal `[model + 0x64] + 0x110` (Startzeit, Dauer, Von, Ziel; FUN_00438440 macht daraus Kanal `0x11`): Ziel = Öffnungswinkel `model + 0x70` (an) bzw. 0 (aus), Dauer 1000 ms, Zeit `FUN_0053bfd0`. **Läuft die Animation noch, überspringt es den Start, merkt sich den Schalter aber trotzdem.** Bei kurzem Antippen bleiben die Klappen dadurch offen; derselbe Fehler tritt im Original bei schnellem Doppeldruck auf L auf.

**9.10e (LFSP v6i):** Die Lichthupe ändert den Lichtschalter nicht mehr. Bei ausgeschaltetem Licht fährt LFSP nur den Klappen-Kanal selbst: beim Drücken auf `model + 0x70`, beim Loslassen auf 0, jeweils von der aktuellen Stellung aus, mit anteiliger Dauer (ganzer Weg 1000 ms). Das Spiel greift nicht ein, solange Schalter und `model + 0x567` gleich bleiben. Ist das Licht mit L an, gehören die Klappen dem Spiel. Schalter `[Lights] HeadlightsFlashPopUps` (ersetzt `HeadlightsFlashSwitch`). Heckleuchten und Scheinwerfer-Leuchten des Modells bleiben bei der Lichthupe aus; die Straße beleuchtet das Fernlicht von dx11z.

**Nachtrag (2026-10-22, 👤 Bug):** Bei Fernlicht ohne Lichtschalter zeigten die Scheinwerfer keinen Glow. Der Leuchten-Umweg (§9.6, `LampTest`) meldet jetzt am Spielerauto die Leuchten 0/1 als an (Stärke in `[model + 0x2D8]` auf 1), solange `g_highBeam > 0` ist und die Seite heil (`0x4E4`/`0x4EC` ≤ `[0x5B2408]`). Heckleuchten bleiben aus. 👤 Test offen.

**Test 9.10e (2026-10-17, 👤):** ✅ Klappen öffnen mit K, Heckleuchten bleiben aus, kurzes Antippen schließt wieder.

### 9.5 Umsetzung 11.5b: Rücklicht-Schein (dx11z 11.5b)

- **Lichtraum am Heck:** für jedes Rennauto (auch das eigene, höchstens 8) die Matrix der Gegner-Scheinwerfer (§8.4a), x- und z-Spalte negiert: z > 0 hinter der Wagenmitte, x > 0 links am Auto (gespiegelt).
- **Form** (Shader, keine Lichtkarte): je Seite eine Leuchte bei x = ±`TailGlowWidth`, Abstand hinter der Mitte `TailGlowOffset`. Abfall nach hinten `exp(−d / TailGlowReach)`, kurz weich eingeblendet ab 0,3 vor den Leuchten, seitlich gaußförmig (`TailGlowSpread`, wird mit dem Abstand breiter), in der Höhe `exp(−|y| / 1,2)`. Wie die Scheinwerfer zur Umgebung addiert, vor der Textur (9.9b).
- **Stärke je Seite** aus der Leuchten-Tabelle (§9.3): Rücklicht 12/13 × `TailGlowStrength`, Bremslicht 2/3 × `TailGlowBrake` (zusätzlich), Blinker 4|6 bzw. 5|7 bernsteinfarben × `TailGlowSignals`. Ist eine Leuchte beschädigt (Byte 0), fällt ihr Anteil weg. Annahme: gerade Bytes = linke Seite, noch nicht geprüft.
- **Wann:** Tag und Nacht. Nachts mit eigenem Licht über den Lichtweg des Spiels, sonst über den Strecken-Rückruf (§7.8), auch wenn sonst kein Licht aktiv ist. Die Matrix-Prüfung läuft nachts jetzt auch bei ausgeschaltetem Licht (`FUN_004526c0` baut die Spielmatrix in jedem Nachtbild).
- **Schalter** (`lfsp.ini [Lights]`): `TailGlow`, `TailGlowColour` (Standard `FF1A0A`), `TailGlowStrength` 0,06, `TailGlowBrake` 0,41 (ab 11.5c, vorher 0,12/0,35), `TailGlowSignals` 0,3, `TailGlowOffset` 2,1, `TailGlowReach` 1,5, `TailGlowWidth` 0,6, `TailGlowSpread` 0,5.
- **Grenzen:** Lage der Leuchten für alle Autos gleich (Werte aus der INI); die Leuchten-Einträge des Modells kennen die echte Lage (§9.1), das wäre ein späterer Schritt. Der Schein wirkt nur auf Strecken-Meshes, nicht auf Autos.
- Konstantenpuffer: `g_Rear` 880, `g_RearShape` 896, `g_RearLeft[8]` 912, `g_RearRight[8]` 1040, `g_RearViewToLight[8]` 1168 (fxc-Listing geprüft).

**Test 11.5b (2026-10-17, 👤):** „Sieht gut aus.“ Wunsch: Das Bremslicht soll so hell bleiben, das normale Rücklicht schwächer sein. **11.5c:** Rücklicht 0,06 (halbiert), Bremse 0,41, Bremslicht damit unverändert 0,47 (Rücklicht + Bremse). Nur Standardwerte und INI geändert.

### 9.6 Blinker-Schein vorn und hinten in der Farbe der Leuchte (11.5d, dx11z 11.5d, LFSP v6v)

**Befund 👤 (Test 11.5c):** (1) Der Blinker-Schein leuchtet dauerhaft und blinkt nicht. (2) Vorn gibt es keinen Blinker-Schein. (3) Die Farbe ist fest Bernstein, manche Autos haben rote, weiße oder orange Blinker.

**Ursache (1):** 11.5b nahm je Seite `4 | 6` bzw. `5 | 7`. Die beiden Bytes einer Seite blinken gegenphasig (§9.3), eines davon ist also immer an.

**Leuchten am Modell (Disassembly `0x43D800`–`0x43DE72`, `0x43CDD0`):** Die Leuchten-Schicht zeigt beim Test `0x43D8C4` mit `EBP` auf die Leuchte (Teil vom Typ 5, Daten ab `+8`). Lage am Modell steht bei `+0` (x, y, z; `0x43CDD0` addiert sie zur Lage des Scheins). Zwei Farben stehen bei `+0x40`/`+0x44` (`0x43D3E0`: RGB davon, Alpha aus der Helligkeit). `+0x4C` ist der Typ des Scheins (Tabelle `0x5CD738`, je 0x18 Bytes mit Textur-Kennungen `CARG`, `SPIK`, `GLNT`, `FEGL`). Bei `+0x48` stehen Flags, bei `+0x4E` die Nummer der gespiegelten Hälfte, bei `+0x4F` der Nacht-Schein. Die gespiegelte Hälfte (`0x43DAC9`) negiert x.

**Umsetzung:**
- **LFSP:** Beide Aufrufe von `FUN_00443660` (`0x43D8C4`, `0x43DAC9`) laufen über einen Umweg. Er merkt sich je Modell und Lampennummer die Lage und Farbe `+0x40` und ruft dann das Original. Export `LfspModelLamps(model, out, max)`. Bekannt ist eine Leuchte, sobald das Spiel das Auto einmal gezeichnet hat. Das Log (`Measure = 1`) zeigt jede Leuchte einmal: `Leuchten: Modell … Nr … bei x y z, Farbe …`.
- **dx11z:** Jede Blinker-Leuchte zählt einzeln mit ihrem eigenen Byte, dadurch blinkt der Schein im Takt des Spiels. 10/11 zählen nur, solange der Blinker der Seite gesetzt ist (Bits 3/4 von `car + 0xD87`, Warnblinker `car + 0xB50 == 3`), sonst sind sie Bremslicht. Leuchten mit z > 0 (vor der Wagenmitte) speisen einen zweiten Schein vor dem Auto. Dessen Lichtraum ist nicht gedreht, der Abstand ist die Lage der Leuchte plus der Versatz `0x61BD50` (außerhalb 0,3–5 gilt `TailGlowOffset`). Die Farbe kommt aus der Leuchte, auf den hellsten Kanal 1 normiert, Stärke `TailGlowSignals`; eine schwarze Leuchte wird Bernstein. Ohne LFSP-Daten gibt es keinen Blinker-Schein. Das Log zeigt je Rennauto einmal die Leuchten: `Tail glow: racer … lamp …`.
- Bis zu 16 Scheine (hinten je Auto, vorn je Auto mit Blinker an). Der Abstand je Schein steht in `g_RearLeft[r].w` (0 = `TailGlowOffset`). Konstantenpuffer: `g_RearLeft[16]` 912, `g_RearRight[16]` 1168, `g_RearViewToLight[16]` 1424, zusammen 2448 Bytes (fxc-Listing geprüft).
**Test 11.5d (2026-10-17, 👤, Log `work/logs/11.5d/`):** (1) Vorn und hinten blinken abwechselnd, bei Blinker und Warnblinker. (2) Beim 356 ist der Schein hinten weiß, obwohl die Leuchte rot sein sollte. (3) Das Bremslicht ist nicht heller als das Rücklicht.

**Befund aus Log und Disassembly (`0x4393C8`–`0x4397FB`):**
- **Lage am Modell:** Die Scheinwerfer 0/1 liegen bei z ≈ −1,65, also ist vorn am Modell z < 0. Die Einheit ist Meter, x > 0 ist links (Leuchten 0, 4, 6). Damit sind 4/5 die Blinker vorn (356: `FFFFFFFF` weiß) und 6/7 die hinten (356: `FFFF0000` rot, andere `FFFF8200` orange). 11.5d hatte vorn und hinten vertauscht, das erklärt (2). Die Farbe ist 0xAARRGGBB, wie angenommen.
- **Takt:** P = (`[0x6B7C7C]` (Spielzeit, `FUN_0053c2e0`) / (`[0x5DEB48]` / 2)) & 1. Das Spiel setzt 4/5 und 10/11 auf P, 6/7 auf „nicht P“. Vorn und hinten blinken also schon im Spiel gegenphasig, das erklärt (1).
- **Bremslicht:** Ist der Lichtschalter an und wird nicht gebremst, setzt das Spiel 2/3 (und 10/11 ohne Blinker) trotzdem auf 1, nur mit gedimmter Stärke `[0x5B35A8]` in der Stärke-Tabelle `[model + 0x2D8]` (`FUN_004436a0`). Beim Bremsen ist die Stärke 1,0. 11.5b/d zählte nur das Byte, deshalb war der Brems-Schein bei eingeschaltetem Licht dauernd an, das erklärt (3).

**11.5e (dx11z):** Vorn ist jetzt z < 0, der Abstand des vorderen Scheins ist −z der Leuchte plus Versatz `0x61BD50`. Alle Blinker-Scheine leuchten im Takt P. Eine Leuchte gilt als defekt, wenn sie in ihrer eigenen Phase des Spiels 3 Bilder hintereinander dunkel bleibt; sie wird wieder heil, sobald sie leuchtet. Das Bremslicht zählt nur bei Byte ≠ 0 und Stärke > 0,99.

**Test 11.5e (2026-10-18, 👤):** Farben passen. Wünsche: (1) Auch die Leuchten des Spiels sollen vorn und hinten gleichzeitig blinken, der Gegentakt fällt mit dem sichtbaren Schein auf. (2) Bei eingeschaltetem Abblendlicht ist am Heck kein Schein zu sehen, nur beim Bremsen.

**11.5f (LFSP v6x, dx11z 11.5f):**
- **Blinker im Takt:** LFSP ersetzt `NOT EAX` (`F7 D0`) vor den Bytes 6 und 7 (`0x43943A`, `0x439472`) durch zwei NOP. Damit setzt das Spiel alle Blinker-Leuchten auf P, Schein und Leuchten blinken gleichzeitig. Schalter `[Lights] SignalsInSync` (1 an, 0 wie das Spiel). dx11z liest an `0x43943A`, ob der Patch aktiv ist, und rechnet die eigene Phase der Leuchte für die Schadensprüfung entsprechend.
- **Rücklicht bei Licht an:** Ist der Lichtschalter an (Bit 0 von `car + 0xD87`, die Lichthupe zählt nicht), kommt `TailGlowLights` (0,12) zum Rücklicht dazu. Rücklicht ohne Licht 0,06, mit Licht 0,18; Bremse zusätzlich 0,41. Die gedimmte Stärke des Spiels für 2/3 bei Licht an ist `[0x5B35A8]` = 0,6.

**11.5g (dx11z, 👤 Wunsch): Rückfahrlicht.** Byte 8 ist im Rückwärtsgang an (`car + 0xD82 == 0`) und bei Schaden 0 (§9.3). Leuchte 8 haben nur manche Modelle, im Log 11.5d vier von acht Autos, alle `FFE1EBFF` (kaltweiß), z ≈ 1,9–2,1. Hat das Modell laut LFSP eine Leuchte 8 und ist ihr Byte an, bekommt der Schein hinten auf beiden Seiten die Farbe der Leuchte × `TailGlowReverse` (0,35) dazu. Autos ohne Leuchte 8 bleiben dunkel.

**11.5h (2026-10-18, 👤):** Auch Gegner mit ausgeschaltetem Licht hatten einen Rücklicht-Schein. Das ist kein Fehler bei der Erkennung, sondern der Grundschein `TailGlowStrength`, der für jedes Auto unabhängig vom Lichtschalter gilt. In der installierten `lfsp.ini` fehlte der Schlüssel, deshalb galt die eingebaute Vorgabe von dx11z (0,06). Lösung: `TailGlowStrength = 0` in Vorlage und Installation. Damit gibt es ohne Licht keinen Schein mehr; bei eingeschaltetem Licht gilt `TailGlowLights`, beim Bremsen zusätzlich `TailGlowBrake`. Mit den Werten von 👤 sind das 0,2 bei Licht an und 0,4 beim Bremsen. Die eingebaute Vorgabe in dx11z (`light_value( "TailGlowStrength", … )`, gilt nur bei fehlendem Schlüssel) ist in der Quelle und in `dx11z-nfsp.patch` jetzt ebenfalls 0. Sie kommt mit dem nächsten dx11z-Build dazu; die installierte DLL hat noch 0,06.

**11.5i (LFSP v7h, 2026-10-18, 👤 Befund):** (1) Das Fernlicht (K) leuchtet auch mit kaputten Scheinwerfern. (2) Das 3. Bremslicht leuchtet bei eingeschaltetem Licht dauernd, nicht nur beim Bremsen.
- **Ursache (1):** Das Fernlicht kam ohne Bezug zum Schaden aus `LfspHighBeam`; dx11z zeichnet es bei gedrückter Taste auch mit Licht-Bytes 0 (Lichthupe, 9.10c). Bei ausgeschaltetem Schalter sind die Bytes ohnehin 0, sie unterscheiden „aus“ nicht von „kaputt“.
- **Ursache (2):** Bei Licht an setzt das Spiel 2/3 und 10/11 ohne Bremsen auf 1 mit gedimmter Stärke `[0x5B35A8]` (0,6) (`0x4396FC`–`0x4397BE`), als Rücklicht. Das gilt auch für eine mittige Brems-Leuchte. Welche Nummer das 3. Bremslicht trägt, ist offen (im Log 11.5d nur der 356 ohne 3. Bremslicht); das Verhalten trifft nur 2/3/10/11, denn ohne Licht ist es aus.
- **Umsetzung LFSP:** `UpdateHighBeam` liest wie das Spiel `c + 0x4E4` und `c + 0x4EC` gegen `[0x5B2408]`; sind beide Scheinwerfer kaputt, ist das Fernlicht sofort 0 (Log `Licht: Scheinwerfer des Spielerautos kaputt`). Ist einer heil, bleibt es wie beim Licht des Spiels an. Die Klappen öffnen mit K weiterhin (wie mit L bei kaputten Scheinwerfern). Der Leuchten-Umweg (§9.6) ruft das Original jetzt selbst und meldet eine Leuchte 2/3/10/11 mit |x| < 0,25 als aus, solange ihre Stärke in `[model + 0x2D8]` ≤ 0,99 ist, also außer beim Bremsen (und beim Blinken von 10/11). Nicht im Menü. Log einmal je Leuchte: `Leuchten: 3. Bremslicht …`. Schalter `[Lights] ThirdBrakeLightBrakingOnly` (1 an, 0 wie das Spiel). dx11z unverändert.
**Test 11.5i (2026-10-18, 👤):** ✅ Beides funktioniert: Fernlicht bleibt bei kaputten Scheinwerfern aus, das 3. Bremslicht leuchtet nur beim Bremsen.

### 9.7 Glow der Leuchten: schwarz ab ~92 m (B23) und heller bei Fernlicht (9.10f, LFSP v8e, 2026-10-22)

**Befund 👤:** Ab ~92,4 m zur Kamera werden die Scheinwerfer-Glows teils schwarz, auch im Original. Wunsch: Glow bei Fernlicht heller.

**Glow-Funktion `FUN_0043CDD0(lamp, x, matrix, stärke)`** (Aufruf `0x43DA77`, Stärke aus `FUN_00443690` = `[model + 0x2D8][n]`), Disassembly:
- Helligkeit b = Winkelfaktor × Stärke (`0x43D192`, `[ebp + 0x10]`); unter 0,1 kein Glow. Größe ∝ b (`0x43D26D`), Alpha = `ftol(b × 255) & 0xFF` (`0x43D3BA`, läuft bei b > 1 über), Zusatz-Flare `0x451D60` bei b × … > 0,1.
- Tiefenversatz (Sprite `+0x10` über `0x537A60`, State 0x18 in dx11z): `round(−20000 × b × [[0x628130] + 0xC] / Abstand²)` (`0x43D41C`–`0x43D452`). Der Versatz fällt mit 1/d² und rundet in der Ferne auf 0; dann kämpft der Glow mit dem Scheinwerferglas des Modells und wird teils schwarz. Passt zu B21: Mit `TestNoZBias = 1` wurden die Lichtflecken ebenso teils schwarz.

**Umsetzung LFSP v8e:** Drei Umwege (je 9 Bytes → CALL + NOP): `0x43D44C` Versatz höchstens −1 (`[Lights] LampGlowDepthFix`, alle Autos); `0x43D195` b × Faktor für die Scheinwerfer 0/1 des Spielerautos bei Fernlicht, `1 + (HeadlightsHighGlow − 1) × g_highBeam` (Standard 1,6, gesetzt in `LampTest` direkt vor dem Glow); `0x43D3BA` Alpha auf 255 begrenzt. Heller heißt damit: größer, voll deckend und kräftigerer Zusatz-Flare. ✅ 👤 2026-10-22: funktioniert.

**Upstream (2026-10-22):** Der Tiefenversatz-Fix ist jetzt Teil von nfs5_enhanced (Detail-Subsystem, MidHook `LampGlowDepth` an `0x43D452` nach dem `FISTP`, Versatz höchstens −1, aktiv mit `[Detail] Enabled = 1`; PR aus Branch `fix/lamp-glow-depth`, in LHedit `master` per Cherry-Pick). LFSP v8f hat `LampGlowDepthFix` und den Umweg an `0x43D44C` deshalb entfernt (beide hätten dieselben Bytes geändert). In LFSP bleiben Fernlicht-Glow (`0x43D195`) und Alpha-Grenze (`0x43D3BA`).

## 10. HUD-Instrumente nach Lichtschalter (11.3)

Wunsch 👤 (2026-10-17): Die analogen Instrumente sollen zwischen Tag- und Nachtfassung wechseln, wenn das Licht mit L geschaltet wird, nicht bei Fernlicht/Lichthupe. Bisher hängt das an der Tageszeit des Rennens.

### 10.1 Befund (Disassembly, `work/re/11.3_*.c`)

- Grafiken in `GameData/DashHud/`: Tag `tach`, `needle`, `speedK`, `speedM`, `needle2`; Nacht jeweils mit `N` (`tachN.fsh` …).
- Namenstabellen [Tag, Nacht] in `porsche.exe`: `0x5CCFCC` tach, `0x5CCFD4` needle, `0x5CCEFC`/`0x5CCF04` speedK/speedM, `0x5CCF0C` needle2.
- Tacho `FUN_0042e710` und Drehzahlmesser `FUN_0042f3a0` lesen beim Aufbau des HUD das Nachtflag `0x628A28` (`movsx ecx, word ptr [0x628a28]`) als Index und rufen `FUN_004212a0` (Instrument, vtable `0x5B2AAC`) auf (`0x42E7FB`, `0x42F464`). Das legt über `FUN_00422df0` (Bild-Objekt `0x10C`, Speicher `FUN_0059ef90`) vier Bilder an: Felder `0x31` (`+0xC4`, Zifferblatt), `0x32` (`+0xC8`, Rahmen `circletach`, gleich), `0x33` (`+0xCC`) und `0x34` (`+0xD0`) Zeiger.
- Abbau: `FUN_00421580` (vtable Slot 0, löschend) bzw. `FUN_004215f0` (aus den Destruktoren der beiden Klassen) gibt die vier Bilder über deren vtable Slot 0 mit Argument 1 frei.
- Die Fassung wird also nur beim Aufbau gewählt; ein Wechsel im Rennen braucht beide Bildsätze.
- Fehler in den Originaldaten: das `speedKN.fsh` von der CD hat für einige Autos das falsche km/h-Zifferblatt (z. B. 250 statt 300, Zeiger bleibt auf der Skala der Tagfassung). Laut nfsaddons Combined Patch, `Game Fixes and Extras.txt` Nr. 34, betrifft das Boxster, 911 (996), GT1 und GT3. Der Patch liefert ein korrigiertes `speedKN.fsh` (MD5 `1a785b52…`). Seit 11.3 sieht man den Fehler auch auf Tagstrecken, sobald das Licht an ist. In `Game CD` ist das Patch-Bild eingespielt, das Original liegt als `speedKN.fsh.orig` daneben.

### 10.2 Umsetzung 11.3a (LFSP v6k)

- An beiden Aufrufstellen ruft LFSP statt `FUN_004212a0` einen eigenen Einstieg auf, der das Original aufruft und danach die Bilder der anderen Fassung mit denselben Argumenten anlegt (Zifferblatt `FUN_00422df0(name, a4, a9, a10, a22, a23, 0, 1, 0, 1)`, Zeiger wie im Original). Erkannt wird die Fassung an den Tabellenzeigern der Namen.
- Je Modell-Update des Spielerautos: Fassung = Bit 0 von `car + 0xD87` (Lichtschalter, nur L; die Lichthupe ändert ihn nicht, §9.4). Bei einem Wechsel tauscht LFSP die Zeiger in den Feldern `0x31`, `0x33`, `0x34`.
- Abbau: Hooks auf beide Destruktoren (9 Byte Prolog `56 8B F1 8B 8E C4 00 00 00`) stellen vorher die Bilder des Spiels zurück und geben die eigenen frei. Ohne diese Hooks legt LFSP keine eigenen Bilder an.
- Schalter `[Lights] InstrumentsFollowLights` (1 an, 0 wie das Spiel). Log: `Instrumente: …` bei Aufbau, Wechsel und Abbau.

### 10.3 Absturz v6k und Fix 11.3b (LFSP v6m)

**Absturz** (👤 17:19, Meldung der B19-Sitzung): beim Rennstart Zugriffsfehler `C0000005` bei `0x53879E` (Schreiben auf `0x04F60004`), Stack über `0x41A38C` (HUD). Letzte LFSP-Zeilen: beide Instrumente haben die andere Fassung geladen.

**Ursache:** `FUN_00422df0` gibt jedem Bild eine Nummer `image + 0x100 = [0x60C9A0]++`. Die Zeichenmethode (`0x423ACE`) übergibt sie an `FUN_0041a180`, das die Quads in einen gemeinsamen Puffer schreibt (`0x60AB18`/`0x60AB10`/`0x60AB1C`, fünf Plätze je Nummer, Zähler `0x60AB20[n]`). Die sechs zusätzlichen Bilder verbrauchten sechs Nummern, alle später angelegten HUD-Bilder lagen damit hinter dem Puffer.

**Weitere Grenze:** Der Destruktor eines Bildes (`FUN_00423670` → `FUN_00423690`) leert den ganzen Grafik-Cache `0x60C99C` aller HUD-Bilder. Ein Bild im Rennen freizugeben und neu anzulegen scheidet deshalb aus.

**Fix 11.3b:** Die zusätzlichen Bilder bekommen die Nummer des Bildes, das sie im Feld ersetzen (es zeichnet immer nur eines davon), und `0x60C9A0` wird nach dem Anlegen auf den alten Wert gesetzt. Die Nummernvergabe ist damit wie im Original. `InstrumentsFollowLights` steht bis zum Test auf 0 (INI und Code-Standard).

**Test 11.3b (2026-10-17, 👤, LFSP v6r mit dem Fix aus v6m, Log `work/logs/11.3b/lfsp.log`):** ✅ kein Absturz. Beide Instrumente legen die andere Fassung mit den Nummern der ersetzten Bilder an (Tacho 3/4/4, Drehzahlmesser 5/6/6, nächste Nummer 5 bzw. 7 wie im Original), wechseln mit L zwischen Tag und Nacht (mehrfach im Log) und geben ihre Bilder beim Abbau frei. `InstrumentsFollowLights` ist jetzt standardmäßig 1.

## 11. Scheinwerfer auf Autos, Schildern und Objekten (11.4)

**Ausgangslage:** dx11z gibt das Pixel-Licht nur den Draws, die eine Strecken-Farbknoten-Funktion markiert hat (`FUN_00453110`, `FUN_004535c0`, Tag-Pfad über `FUN_00543ae0`, §7). Fahrzeuge (`LICHT.md` §8.4a: „Fahrzeug-Meshes laufen nicht durch die Strecken-Lichtfunktion“), Schilder und andere Objekte laufen über andere Wege und bleiben dunkel. Vermutlich gehört auch B17 (Felswand halb beleuchtet) dazu.

**Idee:** Der Shader rechnet die Lichtraum-Position aus Bildposition und `rhw` mit `m_HeadViewToLight` (Bildschirm → Kamera → Lichtraum). Diese Matrix hängt nur von der Kamera und den Lichtern des Bildes ab, nicht vom Objekt. Nachdem ein Strecken-Mesh das Licht in einem Bild vorbereitet hat, gilt sie also auch für jeden anderen Draw mit derselben Kamera.

**Test 11.4a (dx11z, `[Lights] HeadlightsObjects`):**
- 1 = jeder Draw mit Tiefentest, der deckend oder normal alpha-geblendet ist (`SRCALPHA/INVSRCALPHA`, `BOTHSRCALPHA`), bekommt das Licht des letzten Strecken-Meshes im Bild (eigene Scheinwerfer, Gegner, Rücklicht-Schein). Additive oder multiplizierende Durchgänge (Leuchtkegel, Schatten, Rauch) und Draws ohne Tiefentest (HUD) bleiben unverändert.
- 2 = wie 1, aber diese Flächen werden 4-fach hell, damit man sieht, welche Draws neu beleuchtet werden.
- Gültig ab dem ersten vorbereiteten Strecken-Mesh eines Bildes bis zum Bildwechsel; schlägt eine Vorbereitung fehl, ist das Licht für Objekte bis zum nächsten Erfolg aus.
- Messlog (`[Dx11] Measure ≥ 1`) alle 10 s: `M3T   object lights: batches track …, objects lit …, not lit: no track light yet …, depth off …, blend …`.
- Risiken: Rückspiegel (eigene Kamera; stimmt, solange die Spiegel-Strecke vor ihren Objekten gezeichnet wird), Objekte, die vor der Strecke gezeichnet werden (dann unbeleuchtet), HUD mit Tiefentest.
- Installiert: dx11z `2c54ad33…` (MOD.md), in `Game CD/lfsp.ini` `HeadlightsObjects = 1`.

**Ergebnis 11.4a (2026-10-18, 👤):** ✅ „Das Licht funktioniert gut.“ Keine Nebenwirkungen gemeldet. `HeadlightsObjects = 1` ist jetzt Standard in der INI-Vorlage. Die eingebaute Vorgabe in dx11z ist in Quelle und Patch ebenfalls 1 (wirksam ab dem nächsten Build).
