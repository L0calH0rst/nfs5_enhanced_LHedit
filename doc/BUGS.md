# BUGS.md – Grafische Fehleranalyse NFS5 (Porsche Unleashed)

> Themen-Dokument: Beschreibung, Messungen und Herleitungen zu den Grafikfehlern B1–B9. **Status und Aufgaben** stehen in `PLAN.md` (Phase 3C, Block 3T).
> Umgebung: Mod `nfs5_enhanced` v1.1.0 (dx11z aktiv), Win11, Monitor 2560×1440, gerendert in 3840×2160 (§7.3).

---

## 0. Bug-Katalog

| ID | Beschreibung (Nutzer) | DDS-abhängig? | Analyse | Belege |
|---|---|---|---|---|
| **B1** | Rauch vom schwarzen Auspuff-Turm des Boots erscheint weit entfernt (≈ 500 px Versatz) | ja, bei DDS = 1 maskiert | §2 | `boat smoke wrong location.png`, `boat smoke out of view range.png` (beim Nutzer) |
| **B2** | Ein Teil der Map (Klippe/Straße) wird nicht geladen, ein weiter entfernter Bereich rechts davon schon | ja, bei DDS = 1 maskiert | §2 | `map part not loaded.png`, `map part nothing view range loaded.png` (beim Nutzer) |
| **B3** | Animiertes Leuchtturm-Licht schwebt in der Luft, bevor der Leuchtturm geladen ist | ja, bei DDS = 1 maskiert | §2 | `lighthouse light floating.png` (beim Nutzer) |
| **B4** | Heller Saum an Alpha-Kanten, wenn zwei Alpha-Texturen hintereinander liegen (Busch vor Bäumen) | nein; auch im Originalspiel | §3, §7, §8 | `screenshots/b4/` |
| **B5** | Dreieckige graue Flächen am äußeren Himmelsrand, nur an bestimmten Stellen, kurz | nein; nur mit Mod | §4 | `sky glitching behind trees.png`, `sky glitching.png` (beim Nutzer) |
| **B6** | Beschädigte Fahrzeug-Geometrie flackert bei Kamerabewegung (hinten rechts) | nein | §5 | `car broken geometry back right.png` (beim Nutzer) |
| **B7** | Feiner Rand an der Oberkante von Busch-Billboards (beide gekreuzten Flächen) | nein; vermutlich auch im Original | §9.1 | `screenshots/b7-b9/b7_*` |
| **B8** | Rückseite aufgeklappter Fensterläden unsichtbar | nein; Original-Verhalten | §9.2 | `screenshots/b7-b9/b8_*` |
| **B9** | Textur von kaputtem Glas nur von außen sichtbar, von innen nicht | nein; Original-Verhalten | §9.3 | `screenshots/b7-b9/b9_*` |
| **B10** | Profilliste beim Start leer (Einträge vorhanden, aber unsichtbar) – durch `AlphaEdgeFix = 2` | – | §10 | `work/logs/B10-*.log` |
| **B11** | Mit `ForceTwoSided = 1` flackern doppelseitige Schilder (Vorder- und Rückseite gleichzeitig sichtbar) | – | §9.4 | `screenshots/b7-b9/b11_*` |
| **B12** | Mit `ForceTwoSided` farbiger Rand um Schild-Rückseiten (Rand der Vorderseite schaut heraus) | – | §9.5 | `screenshots/b7-b9/b12_*` |
| **B13** | Map-Chunks am Bildrand zu früh ausgeblendet (abrupte Kante, Himmel dahinter) | nein; Hor+ | §4.3 | `screenshots/b13_*` |
| **B14** | Ein einzelner Balkon: linker Teil mit verschmierter, gestreifter Textur; 5 Balkone in der Nähe korrekt | nein (auch bei DDS = 1) | §11 | `screenshots/b14_balcony_left_smear.png` |
| **B15** | Spiegelung im Wasser ungenau: gespiegelte Objekte (Boot) erscheinen als graue, fast texturlose Silhouette statt in ihren Farben | Ursache geklärt (Original-Asset), Fix-Weg offen | §12 | `screenshots/b15_water_reflection_boat.png` |
| **B16** | Boxster: Überrollbügel von vorn rot mit Netz, von hinten schwarz und ohne Netz; `ForceTwoSided` hilft nicht | nein; auch im Originalspiel | §13 | `screenshots/b16_boxster_rollbar_*.png`, `work/logs/b16-measure2/` |
| **B17** | Scheinwerferlicht beleuchtet nicht jede Geometrie: Felswand vor dem Auto rechts beleuchtet, links daneben nicht (harte Kante) | offen | §14 | `screenshots/b17_headlight_cliff_partial.webp` |
| **B18** | HUD-Schadensanzeige: der Farbverlauf Grün → Rot unter dem Auto-Piktogramm erscheint als mehrere dünne parallele Linien statt als durchgehende Fläche | offen; vermutlich Folge der höheren Auflösung (Nutzer) | §15 | `screenshots/b18_hud_damage_gradient_lines.png` |
| **B19** | Kleine Risse in der Map in mittlerer Entfernung (Straßenrand/Grünstreifen), Himmelsfarbe scheint durch; teils fehlt ein ganzes Dreieck. Beim Näherkommen verschwinden sie | nein (auch bei DDS = 1, FarClip = 1) | §16 | `screenshots/b19/` |
| **B20** | Alpen-Strecke: ein weit entfernter Streckenabschnitt (Straße, Hecke, Gelände) schwebt frei vor dem Bergpanorama | offen; auch ohne LFSP-Strecken-Patches und bei DDS = 1 | §17 | `screenshots/b20_*.webp` |
| **B21** | Loch im Boden nahe vor dem Auto, mittig im Bild (Alpen-Strecke, Wiese hinter dem Zaun) | offen; auch ohne LFSP-Strecken-Patches und bei DDS = 1 | §18 | `screenshots/b21_ground_hole_near.webp` |
| **B22** | Reifenspuren teils unsichtbar: Spur bricht an einer gezackten Kante ab, als läge sie unter der Fahrbahn | nein; auch auf Original-Strecken | §19 | Screenshot 👤 2026-10-22 (Chat) |

**Hypothesen H2a/H2b (B1–B3):** H2a = Der Mod skaliert Sichtweite/Far-Clip, aber Engine-Interna kennen den Faktor nicht → Fix im Mod. H2b = Die Engine unterstützt eine größere Sichtweite grundsätzlich nicht → Fix in `gimme.dll`. Befund: §2.

---

## 1. Baseline-Matrix (3C.0 ✅)

| Test | Konfiguration | B1 Rauch | B2 Map-Lücke | B3 Leuchtturm | B4 Alpha-Ränder | B5 Skybox | B6 Culling |
|---|---|---|---|---|---|---|---|
| **T0** ✅ | Mod 1.1.0, `DrawDistanceScale = 1.0`, `FarClipMultiplier = 1.0` | maskiert (außer Sichtweite) | maskiert | maskiert | **tritt auf** | tritt auf | tritt auf |
| **T1** ✅ (Status quo) | Mod 1.1.0, `DrawDistanceScale = 4.0`, `FarClipMultiplier = 2.0` | sichtbar | sichtbar | sichtbar | sichtbar | sichtbar | sichtbar |
| T2 | Mod 1.0.0 | entfällt – Nutzer: „nicht notwendig" (B4 bereits als Mod-Fehler bestätigt) | | | | | |

**Folgerung (bestätigt durch T0):**
- **B1–B3:** DDS-abhängig → bei Original-Sichtweite maskiert, ab erhöhter Draw-Distance sichtbar. Ursache = Engine-interne Tabellen/Logik, die den Skalierungsfaktor nicht kennen (Details §2).
- **B4–B6:** **nicht** DDS-korreliert → unabhängige Fehler; B4/B5 als Mod-Fehler (dx11z) bestätigt bzw. stark vermutet, B6 offen.

> ⚠️ Screenshot-Belege liegen beim Nutzer (Testfahrer), nicht im Workspace. Pro Bug: vor/nachher je Konfiguration nach 3C.9.

---

## 2. B1–B3: Kernbefund der Mod-Quellen-Analyse (3C.1 ✅)

**Frage (H2a vs. H2b):** Skaliert der Mod Draw-Distance/Far-Clip korrekt, aber die Engine kennt den Faktor nicht (→ billiger Mod-Fix), oder unterstützt die Engine erhöhte Sichtweite schlicht nicht (→ teurer gimme.dll-Fix)?

**Befund: Der Mod skaliert exakt ZWEI Stellen – und nur diese:**

| Hook | Adresse | Funktion |
|---|---|---|
| `DrawDistanceHook` | `0x44F1A0` (porsche.exe) | Skaliert die Draw-Distance (`700 * max(1, DDS)`) |
| Far-Clip-Operand-Patch | `0x4677FC` (Operand 3) + Reset `0x45D501` | Skaliert den Far-Clip (`700 * max(1,DDS) * FarClipMult`, Cap 20'000) |

Belegt in: `src/game/addresses.h` (`namespace detail`: `DRAW_DISTANCE = 0x44F1A0`, `FAR_CLIP = 0x4677FC`) und `src/subsystems/detail/detail.cpp`.

**Entscheidend:** In `addresses.h` existiert **kein einziger Code-Hook**, der auf eines der betroffenen Engine-Systeme zeigt:
- ❌ Zell-Priorität / Streaming-Tabelle (B2)
- ❌ Partikel-Emitter-Reichweite / Ankerung (B1)
- ❌ Light-/Effect-Spawn-Logik (B3)

Der Mod hookt ausschließlich `porsche.exe`; `gimme.dll` wird nur in `silent_compat.cpp` berührt (defensive Guards, keine Skalierung).

**Entscheidung für B1–B3:**
- **H2a als „billiger Mod-Fix" ist NICHT trivial machbar:** Es fehlt jeder Hook auf die betroffenen Systeme. Ein H2a-Fix wäre faktisch gleichwertig mit 3C.2 (neue Hook-Stellen in `gimme.dll` finden, verstehen, skalieren).
- **Empfehlung: Pfad 3C.2** – Startpunkt ist der validierte Ghidra-Export `GhidraNFSP/export/gimme.dll/function_map.csv` (primär CSV; `GHIDRA.md`: 99/110 bekannte Hook-Stellen kartiert, 11 Analyselücken bekannt).
- **Fallback (`PLAN.md` Phase 3C, Gate 3C):** DDS auf höchstem stabilen Wert fixieren + betroffene Zelle in `.scn` manuell priorisieren – Nutzer entscheidet nach 3C.2.

**Pro-Bug-Hypothese (für 3C.2-Analyse):**
- **B1 (Rauch am falschen Ort, ≈500 px Offset):** Emitter-Reichweite/-Ankerung auf eigener Tabelle; bei erhöhtem DDS wird der Emitter aktiv, aber die Ankerposition ist nicht mit der skalierten Sichtweite konsistent. → Partikel-Emitter-Update-Funktion in `gimme.dll` lokalisieren.
- **B2 (Klippe/Straße fehlt, weiter entfernter Bereich WIRD geladen):** Zell-Prioritätstabelle fix dimensioniert für Original-Draw-Distance; bei erhöhtem DDS überschreitet sie die Kapazität → nahe Segmente werden übersprungen. → Zell-Lade-Funktion (Frustum-Cull + Priority-Sort) lokalisieren.
- **B3 (Leuchtturm-Licht ohne Turm):** Licht/Effect spawnen auf eigener Tabelle, unabhängig vom statischen Mesh; bei erhöhtem DDS wird die Lichtposition berechnet, aber die Mesh-Zelle ist noch nicht gestreamt. → Effekt-/Licht-Spawn-Logik lokalisieren.

---

## 3. B4: Alpha-Ränder am Busch (dx11z-Analyse, 3C.6 ✅)

**Symptom:** Obere Bushälfte mit weißen Rändern; dahinterliegende Bäume mit Alpha-Map durchscheinen teilweise korrekt → der Alpha-Discard funktioniert TEILWEISE, Kanten-Pixel werden falsch (weiß statt discarded).

### 3.1 Alpha-Test-Pipeline: Logik korrekt ✅

- `shaders.h` → `AlphaPasses(a)`: switcht über `g_Flags.z` (1 = NEVER … 7 = GEQUAL, Default/8 = ALWAYS), `MainPS` macht `if (!AlphaPasses(current.a)) discard;`.
- `driver.cpp` SetState: case `0x24` (ALPHAREF) → `ALPHAFUNC = stored(0x40)+1 : 5u` (Default GREATESS – korrekt für D3D7-Default); case `0x40` → `ALPHAFUNC = v+1`.
- `ApplyPipeline`: `m_Flags[2] = ALPHATESTENABLE ? ALPHAFUNC : 8`, `m_AlphaRef[0] = (ALPHAREF & 0xFF) / 255.0f`.

→ **B4-Ursache liegt NICHT im Alpha-Discard selbst.**

### 3.2 Prime-Suspect: Alpha-Forcing bei palettierten Texturen (P4/P8)

`formats.cpp` → `ConvertLevel`:

```cpp
case P4:
    out[i] = palette != nullptr ? (palette[index] | 0xFF000000u) : Pack(255, index*17, index*17, index*17);
case P8:
    out[i] = palette != nullptr ? (palette[index] | 0xFF000000u) : Pack(255, index, index, index);
```

**Effekt:** Jeder Paletten-Eintrag wird mit **Alpha = 255 (opak)** erzwungen. Ein Palette-Slot mit A = 0 (transparenter Slot, typischerweise weißes RGB) rendert also **weiß statt transparent** – exakt das B4-Symptom „weiße Ränder".

**Wichtige Nuance:** In echtem D3D7 hat `PALETTEENTRY` kein Alpha-Feld (nur BGR + Flags) – P4/P8 sind dort opake Formate, und das Forcing wäre dann eine *treue* Emulation. Die dtexture-Schicht hier übergibt Paletten aber als `uint32_t[]` (`TextureUpdate(texture, pixels, palette)`), die Alpha tragen KÖNNEN. Ob gimme.dll auf Palette-Alpha verlässt, ist **nicht ohne Laufzeit-Daten entscheidbar**.

### 3.3 Entscheidender Test (👤 Nutzer, erfordert Mod-Rebuild)

Temporäres Log in `Driver::TextureUpdate` (driver.cpp ~Zeile 2057): bei P4/P8-Einträgen Format, Größe und die Alpha-Werte der Paletten loggen. Busch-Szene fahren:
- **Paletten mit A = 0 vorhanden** → H-B4a bestätigt → Fix: Palette-Alpha statt gezwungenem FF verwenden (`out[i] = palette[index];` bzw. `palette[index] | (palette[index] & 0xFF) ? ... : 0xFF000000u`).
- **Alle Paletten-Einträge A = 255** → H-B4a widerlegt → Bush-Textur nutzt ARGB1555/DXT1; Bug liegt dann in Stage-Op/`m_WhiteDiffuse`/Sampler – weitere Analyse nötig.

### 3.4 Klärung: `palette count … not valid`-Logfehler

**Geklärt (2026-10-14):** Der Logstring stammt **NICHT** aus dx11z, sondern aus `src/subsystems/patch_silent_compat/silent_compat.cpp` (Feature `FixPictureConversion = 1`): Der Shape-Parser in `gimme.dll` (RVA `0x1054A0`) liest bei der Konvertierung des Driver-Bildes (`AllDrivr.fsh` → `pic16.fsh`) einen Paletten-Zähler aus einem zu kurzen Block und würde Heap-Garbage lesen. Der Mod lehnt ungültige Zähler ab und loggt `'gimme.dll' shape palette count {} is not valid, palette ignored`.

→ **Unabhängig von B4**, betrifft die Frontend-Bildkonvertierung nach dem Rennen, ist bereits ein aktiver Bug-Fix des Mods. PLAN.md Phase 3C (damals) „Offene Punkte (a)" kann damit geschlossen werden: kein dx11z-Pfad, kein separates Tracking nötig.

---

## 4. B5: Graue Dreiecke am Himmelsrand (dx11z-Analyse, 3C.7 ✅ – Hypothese)

**Symptom:** Dreieckige graue Flächen am äußeren Himmelsrand, nur an bestimmten Map-Stellen, für kurze Momente; **nur mit Mod** (Vanilla-Vergleich bestätigt: ohne Mod kein Bug).

**Befunde:**
- Sky wird als normale Geometrie gerendert (kein separater Skybox-Pfad in dx11z); VS macht `saturate(z) * w` (keine W-Projektion für unendlich ferne Geometrie).
- Depth: `ClearWindow` nutzt `m_ClearDepthValue` (case `0x43`, clamp 0–1); `DEPTH_BIAS_UNIT = 1/32768`; `m_DepthBias` via case `0x18`/`0x66`.
- Rasterizer: `FrontCounterClockwise = FALSE` (D3D7-CW-Front), `DepthClipEnable = TRUE`, CullMode-Mapping `2→FRONT, 3→BACK, sonst NONE`.

**Hypothese (nicht final zugeordnet):** Depth-Precision-Konflikt bei großer Far-Clip-Distanz (T1: `700 * 4 * 2 = 5600`) ODER fehlende Depth-Write-Suppression für die Sky-Geometrie → einzelne Sky-Triangelschnitte gewinnen den Depth-Test gegen bereits gezeichnete Geometrie und erscheinen als graue Dreiecke.

**Nächster Schritt:** Bei Gelegenheit `ClearWindow` (driver.cpp ~Zeile 1516–1600) + Depth-Bias-Handling im Detail lesen; ggf. Log der Sky-Pipeline-State (Z-Func, DepthWrite) in der betroffenen Szene. Fix-Kandidaten: Sky-Z-Func erzwingen / Depth-Write off für Sky-Pässe / Depth-Bias-Anpassung.


### 4.1 Update 2026-10-16: neuer Befund und Hypothese H-B5w

**Beobachtung** (`doc/screenshots/b5_sky_hole_edge.png`):
- Am **linken und rechten Bildrand** fehlt in manchen Winkeln kurz ein **dreieckiges Stück Himmel**. Man sieht stattdessen die graue bzw. lila Löschfarbe.
- Besonders auffällig in Kurven.
- Tritt auch mit `DrawDistanceScale = 1.0` / `FarClipMultiplier = 1.0` auf.
- Ohne Mod nicht vorhanden (§4).

**Hypothese H-B5w (Widescreen):**
- `[Widescreen] HorPlus = 1` öffnet das Sichtfeld seitlich über 4:3 hinaus. Der Mod passt dabei nur den **vertikalen** Culling-Winkel der Kamera an (`widescreen.cpp`, `m_TanHalfVertical`); den Himmel behandelt er nicht gesondert.
- Die Engine verwirft große Himmelsdreiecke nach ihrer 4:3-Logik, typischerweise solche, deren Ecke hinter der Kamera liegt (keine Clip-Behandlung). Im 4:3-Bild lagen diese Bereiche außerhalb des Bildes, mit Hor+ werden sie an den Rändern sichtbar.
- Die frühere Hypothese (Depth-Präzision, §4) passt nicht zur Unabhängigkeit von der Sichtweite und zur Lage an den Bildrändern.

**Test:** `HorPlus = 0` (Bild gestreckt, Sichtbereich wie im Original). INI-Sicherung: `nfs5_enhanced.ini.before-b5test`.
- Löcher weg → H-B5w bestätigt. Fix-Wege: (a) Culling der Engine für den Himmel erweitern (Hook in `gimme.dll`/`porsche.exe`), (b) in dx11z die Löschfarbe an den Horizont angleichen (kaschiert nur), (c) `FovMultiplier < 1` als Kompromiss.
- Löcher bleiben → Ursache im Renderer (dx11z-Depth-Pfad, §4) weiter verfolgen.

**Ergebnis ✅ (2026-10-16):** Mit `HorPlus = 0` sind die Löcher weg → **H-B5w bestätigt**, B5 ist eine Folge des erweiterten Sichtfelds. INI wieder auf `HorPlus = 1`.

**Was der Mod schon richtig macht:** Die Culling-Ebenen der EAGL-Kamera (`m_TanHalfHorizontal`/`m_CosHalfHorizontal` bei +0xF8) berechnet die Originalfunktion `SetPerspective` aus dem neuen, breiteren FOV. Die Haupt-Kamera cullt also korrekt. Der Himmel wird folglich an anderer Stelle aussortiert, mit eigener Logik und vermutlich festem 4:3-Winkel.

**Ansatzpunkte in `porsche.exe`** (Ghidra-Export): `FUN_00459660`/`FUN_00459700` laden die Himmels-Konfiguration (`SkyCnfig`, `Sky\%s.bin`), `FUN_0045dbd0` lädt `Sky\%s.fsh` (aufgerufen von `FUN_0045dd00`, `FUN_00468fb0`). Die Zeichen- und Cull-Funktion des Himmels liegt vermutlich in der Nähe. Der Export enthält keinen dekompilierten Code; Ghidra 12.1.4 und das Projekt `GhidraNFSP/NFSP.gpr` sind vorhanden, eine Headless-Dekompilierung einzelner Funktionen ist möglich.

**Fix-Wege:**
1. **Engine-Patch (sauber):** Sky-Culling finden und auf das echte Hor+-Sichtfeld erweitern (neuer Hook, analog `SetPerspectiveHook`).
2. **Kaschieren in dx11z:** Löschfarbe vor jedem Frame an die Horizontfarbe angleichen. Das Loch wird dann unauffälliger, bleibt aber.
3. **`FovMultiplier < 1`:** weniger Seitenblick, weniger Löcher; nur ein Kompromiss.

### 4.2 Ursache im Code und Fix (2026-10-16, Engine-Weg)

Dekompiliert mit Ghidra headless (`GhidraNFSP/decompile.cmd`; die Ausgaben in `work/re/` bleiben lokal).

**Zeichenpfad:**
- Der Frame `FUN_00468ae0` ruft vor der Welt `FUN_0045d7d0` auf, das wiederum `FUN_0045d030` (Bewegung) und `FUN_0045d4f0` (Zeichnen).
- `FUN_0045d4f0` setzt Near/Far auf 0,15/700, verschiebt die Kamera in den Ursprung und zeichnet die Himmelsschichten über `FUN_0045d470` (virtuell, vtable +4).
- Schicht-Klassen: vtables `0x5B3E8C`, `0x5B3EA0`, `0x5B3EB8`.
- Die Zeichenmethoden `0x45B380` und `0x45BD40` (in Ghidra nicht als Funktion erkannt, Analyselücke) teilen jede Schicht in **9 Teilflächen** zu je 10×10 Vertices. Pro Teilfläche prüft `FUN_0045abb0` vier Ecken und vier Kantenmitten gegen einen Keil.
- Ausnahme: Beim steilen Blick (`view+0x104 ≥ 0,5`, mittlere Fläche ab 0,4/0,35) wird immer gezeichnet.

**Der Keil:** `FUN_0045aaf0` baut zwei waagerechte Ebenen bei ±(90° + Halbwinkel) um die Blickrichtung (`view+0x100/0x108`). Der Halbwinkel kommt aus **`view+0x130` = halbes FOV in Grad**, mindestens 37,5° (`0,104166 × 360`).
- Der Mod verbreitert das Sichtfeld erst in `SetPerspective` (`0x5391B0`). Diesen Wert gibt die Engine selbst an `SetPerspective` weiter (`view+0x130 × 2`), deshalb bleibt er auf 4:3.
- Folge: Im 16:9-Bild (Hor+) werden Teilflächen am linken und rechten Rand weggeschnitten. Weil nur Ecken und Kantenmitten geprüft werden, geschieht das nur in bestimmten Winkeln, also kurzzeitig in Kurven.

**Fix (ASI, `work/patches/asi/asi-nfsp.patch`):**
- Neuer Hook `SkyCullPlanes` auf `0x45AAF0` im Widescreen-Teil. Signatur `A1 30 81 62 00 83 EC 18 56 8B`, nur aktiv bei `HorPlus = 1`.
- Für die Dauer des Originalaufrufs setzt er `view+0x130` auf den Hor+-Halbwinkel (`atan(tan(h) · Seitenkorrektur · FovMultiplier)`, +5 % Reserve) und stellt danach den Originalwert wieder her. Das Feld speist sonst `SetPerspective` erneut, dort würde der Mod doppelt verbreitern.
- Build mit MSVC/NMake über `work/patches/asi/build_asi.cmd`. Installiert als `Game CD/nfs5_enhanced.asi` (MD5 `306ef8bb3401de68df4fc6cb2350ef1c`), Original gesichert als `nfs5_enhanced.asi.bak` (`55f778dc…`).
- **Achtung:** Das ist der erste eigene ASI-Build. Das Release wurde vermutlich mit einem anderen Compiler gebaut (andere Dateigröße). Beim Test auf Auffälligkeiten auch außerhalb des Himmels achten, und im Mod-Log muss `Hook SkyCullPlanes installed at 0045AAF0` stehen.


**Ergebnis B5 ✅ (2026-10-16):** Mit dem Hook `SkyCullPlanes` sind die Himmels-Löcher weg (Log: `Hook SkyCullPlanes installed at 0045AAF0`, `work/logs/b5-skycull-*`).

### 4.3 B13 – Map-Chunks am Bildrand zu früh ausgeblendet (2026-10-16)

**Symptom** (`doc/screenshots/b13_chunk_cut_right_a.webp`, `…_b.png`): Am rechten Bildrand endet die Strecken-Geometrie (Felsen, Bäume) mit einer senkrechten Kante, dahinter ist nur Himmel. Ein Chunk wird nicht gezeichnet, obwohl er im Bild liegt.

**Ursache (gleiches Muster wie B5):** Eine Suche nach allen Lesern von `view+0x130` (`GhidraNFSP/FindDisplacement.java`) ergibt `FUN_0047f550` (aufgerufen von `FUN_0047fb40` im Frame `FUN_00468ae0`).
- Die Funktion baut mit `FUN_0047f210` die 2D-Sichtpyramide (`tan(Halbwinkel) · Distanz`, Höhe × 0,75) und zwei Seitenebenen (Drehung um ±Halbwinkel). Damit werden die Strecken-Chunks ausgewählt.
- Auch hier ist es der **4:3-Halbwinkel**.
- Weitere Treffer: die Sonnen-Funktionen `0x460290`/`0x460460` lesen `+0x130` des Sonnen-Objekts, nicht der Kamera (Fehltreffer), dazu eine Frontend-Funktion `0x4D3420`.

**Fix:** zweiter Hook `WorldCullVolume` auf `0x47F550` (Signatur `81 EC FC 00 00 00 53 8B 9C 24 04 01 00 00`), dasselbe Verfahren wie bei `SkyCullPlanes`: Halbwinkel für die Dauer des Aufrufs auf den Hor+-Wert setzen, danach zurück. Beide Hooks teilen sich `WidenViewHalfFov()`. ASI-Build MD5 `fffd41ab49df85f466e6573d862cb3ec`, Vorgänger `nfs5_enhanced.asi.b5`.

**Ergebnis ✅ (2026-10-16, `work/logs/b13-worldcull-*`):** Die Chunks am Bildrand bleiben stehen, der Himmel ist ok, keine Auffälligkeiten. **B13 behoben.**

**Mögliche Nebenwirkung:** Es werden mehr Chunks ausgewählt (etwas mehr Last). Bei B2 (Map-Lücke, DDS-abhängig) lohnt eine Gegenprobe, denn dieselbe Auswahl könnte beteiligt sein.

---

## 5. B6: Flackern beschädigter Fahrzeug-Geometrie (3C.8-Vorbereitung ✅)

**Symptom:** Beschädigte Fahrzeug-Geometrie flackert bei Kamera-Bewegung (hintere rechte Ecke); nur mit beschädigtem Fahrzeug; nicht DDS-korreliert.

**Befund dx11z:**
- dx11z kennt **kein Frustum-Culling** – es rendert, was ihm vorgegeben wird. Rasterizer-Cull ist rein seitenbasiert (FRONT/BACK/NONE) und kann kein Flackern bei Kamera-Bewegung erklären.
- → **B6 ist mit hoher Wahrscheinlichkeit Engine-Seite:** Statische Bounding-Boxen in `gimme.dll` vs. verformte (schadenbasierte) Mesh. Cullt die Engine Teile der deformierten Geometrie weg, weil das Culling-Volumen nicht mitverformt wird → Flackern bei Kamera-Bewegung.

**Nächster Schritt (3C.8):** `gimme.dll`-Analyse über den Ghidra-Export: Bounding-Box-/Frustum-Cull-Funktion des Fahrzeug-Rendepfads lokalisieren; prüfen, ob die Box pro Frame aus der deformierten Geometrie neu berechnet wird oder statisch bleibt. Fix-Kandidaten: (a) Culling für beschädigte Fahrzeuge deaktivieren/erweitern, (b) Bounding-Box-Recompute-Hook.

---

## 6. Status

Der Status der Aufgaben steht in `PLAN.md`. Frühere offene Punkte dieses Abschnitts sind dorthin umgezogen (2026-10-16).

---

## 7. Update 2026-10-16: B4-Testergebnis & Konfig-Abgleich

### 7.1 B4-Test (§3.3) – Ergebnis: H-B4a praktisch widerlegt

**Aufbau:** Temporäres `B4TEST`-Log in `Driver::TextureUpdate` (`drivers/dx11/src/driver.cpp` ~Z. 2053–2090, als „REMOVE AFTER TESTING“ markiert). Rebuild am 2026-10-14 23:12, installiert als `Game CD/Drivers/dx11z.dll` (MD5 `7f09ac45681f2301f4d3759513874cce`, identisch mit `drivers/dx11/build/dx11z.dll`; Original-Release gesichert als `dx11z.dll.bak`, MD5 `5863c16195ace78838675e86a30c48b0`). Die `B4TEST`-Format-Strings sind im installierten Binary vorhanden.

**Beobachtung:** `dx11z.log` des letzten Laufs (2026-10-15, inkl. Rennen bei 3840×2160) enthält **keine einzige `B4TEST`-Zeile**, obwohl andere Log-Zeilen (`setvideomode`, `Swap chain`, …) geschrieben wurden.

**Erklärung aus dem Code:** `Driver::About()` meldet dem Spiel nur diese Texturformate als unterstützt:

```cpp
m_FormatFlags[formats::ARGB1555] = 1;
m_FormatFlags[formats::RGB565]   = 1;
m_FormatFlags[formats::ARGB8888] = 1;
m_FormatFlags[formats::ARGB4444] = 1;
```

`P4` (1) und `P8` (2) werden **nicht** angeboten → das Spiel lädt keine palettierten Texturen hoch → der Pfad `ConvertLevel` P4/P8 mit `| 0xFF000000u` (§3.2) wird nie ausgeführt. Leeres Log und Code-Befund sind konsistent.

**Folgerung:** H-B4a (Alpha-Forcing bei Paletten) ist **nicht** die Ursache von B4.
**Vorbehalt:** `dx11z.log` wird bei jedem Start mit `"w"` neu angelegt. Wurde im geloggten Lauf keine Szene mit Büschen gefahren, ist der Test formal nicht abgeschlossen – der Code-Befund (`About()`) gilt aber unabhängig davon.

**Neue Verdächtige für B4 (nächste Analyse):**
1. Konvertierung **ARGB1555 / ARGB4444** in `formats.cpp` (1-Bit- bzw. 4-Bit-Alpha → Kanten-Texel mit RGB = weiß und A knapp über der Alpha-Referenz).
2. **Texture-Filtering + Alpha-Test:** Bilineare Filterung (bzw. Anisotropie/Mip-Erzeugung) mischt den RGB-Anteil transparenter Texel (häufig weiß) in die Kantenpixel; der Alpha-Test lässt diese gemischten Texel passieren → heller/weißer Saum. Im Original-D3D7 mit 16-Bit-Texturen und anderer Filterung evtl. unauffällig. Kandidaten-Fix: Premultiplied/Edge-Padding beim Upload (RGB transparenter Texel mit Nachbarfarbe auffüllen) oder Alpha-Referenz prüfen.
3. Texture-Stage-Ops (`m_WhiteDiffuse`, ALPHAOP/COLOROP) – unverändert offen.

👉 Das temporäre `B4TEST`-Log kann entfernt bzw. `dx11z.dll.bak` zurückgespielt werden, sobald die nächste Test-Variante gebaut wird.

### 7.2 Neue Screenshots (`doc/screenshots/b4/`, 2026-10-15)

| Datei | Inhalt |
|---|---|
| `bush_wide_context.png` | Busch rechts: heller/weißer Saum entlang der **oberen** Blattkanten; Busch links ohne Auffälligkeit; Bäume im Hintergrund mit Alpha korrekt |
| `bush_white_fringe_zoom_a.png`, `bush_white_fringe_zoom_b.png` | Zoom auf den weißen Saum |
| `boat_railing_overlap.png` | Boot (coastal): (a) Seil/Tau rechts ebenfalls mit hellem Saum (gleiches Muster wie B4); (b) innerhalb der Reling ist ein Hintergrund-Billboard (Bäume/Häuser) sichtbar, das vor dem Himmel/Meer liegt → mögliches Überlappungs-/Sortierproblem; Zuordnung zu B4 oder eigenständigem Bug noch offen |

→ Der Saum tritt nicht nur an Vegetation, sondern auch am Tau auf → spricht für ein **generelles** Alpha-Kanten-Problem im dx11z-Pfad (siehe 7.1, Verdächtige 1/2).

### 7.3 Konfig-Abgleich (Ist-Stand `Game CD/nfs5_enhanced.ini`, 2026-10-15 19:04)

| Schlüssel | PLAN.md / §1 (T1) | **Ist** | Bemerkung |
|---|---|---|---|
| `[Detail] DrawDistanceScale` | 4.0 | **3.0** | T1-Baseline nicht mehr aktuell |
| `[Detail] FarClipMultiplier` | 2.0 | **3.0** | Far-Clip = 700·3·3 = 6300 (vorher 5600) |
| `[Widescreen] LodDistanceScale` | – | 4.0 | Car-LOD-Skalierung (Log: „car LOD scale 4.0“) |
| `[Dx11] Antialiasing` | 4 (Default, „MSAA 4×“) | **1 = aus** | Log bestätigt „rendered … with 1 samples“ |
| `[Dx11] Anisotropy` | – | 0 (wie das Spiel es anfordert) | |
| Renderauflösung | 2560×1440 | 3840×2160 auf 2560×1440-Monitor | Log: „Frame 3840x2160 rendered at 3840x2160“ (Downsampling) |

→ Bei der nächsten Validierung (3C.9) die verwendete Konfiguration mit angeben; T1-Zeile in §1 bezieht sich auf DDS=4/FarClip=2.

### 7.4 Sonstige Korrekturen
- `PLAN.md` nannte damals `Porsche.exe` = 249 KB (Launcher-Stub). Installiert ist der **v3.5-Build aus dem Mod-Release** (2.129.965 Byte, MD5 `316f33a59db48a7db2a75cc4b3bd03dc`).
- Ghidra-Export ist jetzt versioniert (`GhidraNFSP/export/*/function_map.csv` + `GhidraNFSP/ExportNFS5FunctionMap.java`); die JSONL-Dateien (identischer Inhalt, ~25 MB) bleiben lokal (Offener Punkt 4 erledigt).

---

## 8. Update 2026-10-16: B4 ist ein Original-Fehler – neues Arbeitsmodell H-B4c

**Neue Erkenntnis (👤 Nutzer):** Der Alpha-Saum zwischen zwei hintereinanderliegenden Alpha-Texturen ist **auch im Originalspiel** vorhanden. Dort ist er wegen niedriger Auflösung und 16-Bit-Ausgabe kaum zu sehen. Damit ist B4 **kein dx11z-Emulationsfehler** (§3/§7 suchten danach), sondern eine Schwäche des Original-Renderings, die dx11z bei 4K sichtbar macht. Ziel ist jetzt ein **Fix**, keine Fehlersuche mehr (PLAN.md Block 3T).

### 8.1 Arbeitsmodell H-B4c: Blending mit Z-Write ohne Sortierung

1. Der Busch wird mit Alpha-Blending (`SRCALPHA`/`INVSRCALPHA`) **und** aktivem Z-Write gezeichnet. Der Alpha-Test lässt dabei auch halbtransparente Kantentexel durch (niedrige oder keine Referenz).
2. Die Kantentexel (durch bilineare Filterung entstehen Alpha-Werte zwischen 0 und 1) mischen sich mit dem, was **zu diesem Zeitpunkt** im Framebuffer steht. Hinter der oberen Bushälfte ist das der Himmel. Gleichzeitig schreiben sie Tiefe.
3. Die Bäume dahinter (ebenfalls Alpha) werden **danach** gezeichnet und scheitern an diesen Kantenpixeln am Tiefentest. Dort bleibt die Himmelsfarbe stehen.

**Abgleich mit den Screenshots (`doc/screenshots/b4/`):**
- Der Saum in `bush_white_fringe_zoom_a.png` ist rosa-weiß, also genau die Abendhimmel-Farbe, nicht reines Weiß. ✔
- „Nur die obere Hälfte“: Unten liegt opaker Fels (früher gezeichnet) hinter dem Busch. Die Kante mischt sich mit Felsfarbe und fällt nicht auf. ✔
- Das Tau am Boot (`boat_railing_overlap.png`) zeigt dasselbe Muster vor dem Himmel. ✔
- Auch im Original vorhanden (Nutzer) – passt, weil Draw-Reihenfolge und Blend-State vom Spiel kommen, nicht von dx11z. ✔

**Verstärkender Faktor:** Das Spiel lädt nur ARGB1555/4444/565/8888 hoch (§7.1). Bei 1-Bit-Alpha (1555) haben voll transparente Texel oft weißes/helles RGB, das per Filterung in die Kante gezogen wird → zusätzlicher heller Saum, selbst ohne Tiefenproblem (= Verdächtiger 2 aus §7.1).

**Noch nicht belegt:** Der tatsächliche Blend- und Z-Write-State beim Busch-Draw. Diesen misst 3T.1 (State-Log), bevor der Fix gebaut wird.

### 8.2 Fix-Optionen (Umsetzung PLAN.md 3T.2/3T.4)

| Variante | Wirkung | Kosten |
|---|---|---|
| Schwelle: bei Blend+Z-Write `a < 0.5` verwerfen | Tiefenlöcher an weichen Kanten verschwinden; Kanten werden härter | trivial (Shader) |
| **Zwei-Pass** (empfohlen): `a ≥ 0.5` mit Z-Write, danach `a < 0.5` ohne Z-Write | weiche Kanten bleiben, keine Tiefenlöcher | doppelte Draws nur für Blend+Z-Write-Draws |
| Alpha-to-Coverage | beste Kantenqualität | braucht MSAA (aktuell `Antialiasing = 1`) |
| Edge-Padding beim Upload | entfernt den weißen RGB-Anteil der transparenten Texel | einmalig pro Upload, CPU |

Alle Varianten hinter INI-Schaltern; Schalter aus = Original-Verhalten.

### 8.3 Bezug zu HD-Texturen

Der Textur-Upload (`Driver::TextureUpdate`) ist die Stelle, an der sowohl Edge-Padding als auch ein **Laufzeit-Textur-Ersatz** (Hash → HD-PNG/DDS, 32-Bit-Alpha, eigene Mips) ansetzen können. Ein HD-Ersatz per FSH-Repack würde bei 16-Bit-Upload die Alpha-Qualität wieder verlieren. Details und Entscheidung: PLAN.md Block 3T (3T.0).

### 8.4 Messbuild 3T.1 (gebaut und installiert 2026-10-16)

**Stand:** gebaut, installiert, **noch nicht im Spiel gelaufen**.

| Was | Wo |
|---|---|
| Patch (gegen den B4TEST-Stand) | `work/patches/dx11z/3T1-measure.patch` (Basis: `work/patches/dx11z/pre-3T1/`) |
| Build-Skript (MSVC BuildTools 18, x86) | `work/patches/dx11z/build_dx11z.cmd` |
| Installiert | `Game CD/Drivers/dx11z.dll` (MD5 `0161465bed2faef388fa49081ebea4d4`); vorheriger B4TEST-Build → `dx11z.dll.b4test`, Original-Release weiterhin `dx11z.dll.bak` |
| Schalter | `Game CD/nfs5_enhanced.ini` `[Dx11] Measure = 2` (Sicherung: `nfs5_enhanced.ini.pre-3T1`) |

**Was der Build tut** (nur bei `Measure ≥ 1`; bei `0` Original-Verhalten):
- `M3T draw #n …` – beim ersten Auftreten jeder State-Kombination texturierter Draws: Texturformat, Blend an/aus, Src/Dest-Blend, Z, Z-Write, Z-Func, Alpha-Test an/aus, Func, Ref, Stage-0-Ops. Kombinationen mit Blend + Z + Z-Write sind mit **`SUSPECT`** markiert (Muster H-B4c).
- `M3T talloc …` – jede neue Kombination aus Format × Größe × Mip-Levels (Antwort auf das F-Risiko: lädt das Spiel Alpha-Texturen als 1555/4444 oder als 8888?).
- `M3T dynamic texture …` – Texturen, die mehr als einmal hochgeladen werden.
- `M3T report …` – alle 3600 Frames und bei jedem Textur-Reset (Streckenwechsel): Zähler je State-Kombination und Formatverteilung.
- `Measure = 2` färbt zusätzlich alle **SUSPECT**-Draws magenta. Ist der Busch (oder sein oberer Rand) magenta, zeichnet das Spiel ihn mit Blending und Z-Write → H-B4c bestätigt.

**👤 Testablauf:**
1. Spiel starten und die Strecke aus `bush_wide_context.png` fahren; an der Busch-Stelle anhalten.
2. Screenshot der Busch-Stelle und der Boot-Stelle (`boat_railing_overlap.png`), beide mit Magenta-Färbung.
3. Spiel beenden und **sofort** `Game CD/Drivers/dx11z.log` nach `work/logs/3T1-dx11z.log` kopieren (✅ erledigt, Ergebnis §8.5) (das Log wird bei jedem Start überschrieben).
4. Danach `Measure = 0` setzen (oder `nfs5_enhanced.ini.pre-3T1` zurückkopieren).

### 8.5 Ergebnis 3T.1 (Testfahrt 2026-10-16) – H-B4c bestätigt ✅

Belege: `work/logs/3T1-dx11z.log` (Rennen bei 3840×2160, ~11.700 Frames) und `doc/screenshots/b4/3t1_bush_measure_tint.png` (`Measure = 2`).

**Screenshot:** Busch und Baumwand dahinter sind magenta, also beide SUSPECT-Draws (Blend + Z-Write). Der obere Buschrand hat eine **nicht eingefärbte, himmelfarbene Kontur**. Das ist genau der Mechanismus aus §8.1:
1. Der Busch wird **vor** den Bäumen gezeichnet.
2. Seine halbtransparenten Kantenpixel mischen sich mit dem Himmel und schreiben Tiefe.
3. Die später gezeichneten Bäume scheitern dort am Tiefentest.

Die Kontur ist himmelfarben und nicht magenta, weil sie Framebuffer-Inhalt von *vor* dem Busch-Draw ist. Der Felsen (`blend 0`, Draw #18) ist erwartungsgemäß nicht eingefärbt.

**Log – Draw-States (Auszug, Zähler 3600–7200 Frames):**

| # | Format | Blend | Z-Write | Alpha-Test | Batches | Primitive |
|---|---|---|---|---|---|---|
| 2 | ARGB1555 | SRCALPHA/INVSRCALPHA | **1** | GREATER 10 | 1.019.762 | 27,7 Mio |
| 4 | ARGB4444 | SRCALPHA/INVSRCALPHA | **1** | GREATER 10 | 731.383 | 9,6 Mio |
| 8 | ARGB8888 | SRCALPHA/INVSRCALPHA | **1** | GREATER 10 | 377.573 | 4,4 Mio |
| 18 | ARGB8888 | **aus** | 1 | GREATER 10 | 496.159 | 10,7 Mio |
| 19/20 | 8888/4444 | SRCALPHA/INVSRCALPHA | 0 | GREATER 10 | 375.411 | 4,8 Mio |

**Befunde:**
- **17 von 24 State-Kombinationen sind SUSPECT.** Der Großteil der Welt wird mit Blending **und** Z-Write gezeichnet, auch weitgehend opake Flächen.
- Alpha-Test ist `GREATER` mit Referenz **10/255 ≈ 0,04** (teils 16 oder 1). Damit schreibt fast jede Kantenstufe ab 4 % Deckung Tiefe. Bei bilinearer Filterung gibt es pro Kante also einen breiten Bereich halbtransparenter Pixel mit Tiefeneintrag.
- Das ist **Original-Spielverhalten** (die States kommen vom Spiel; dx11z übersetzt sie 1:1). Es passt zur Nutzerbeobachtung „auch im Original vorhanden“.

**Log – Texturformate (`talloc`, ganze Sitzung):** ARGB4444 1010 · ARGB8888 426 · ARGB1555 173 · RGB565 54.
*(Korrigiert 2026-10-16: Die erste Auswertung behauptete „bis 1024² im Rennen“ und „keine Mipmaps“. Beides war falsch, weil Menü- und Rennphase nicht getrennt wurden.)*

| Phase (Log-Zeilen) | Größte Textur | Mip-Ketten |
|---|---|---|
| Frontend/Menü 640×480 (bis `setvideomode 48`) | ARGB8888 **512×512 und 1024×1024**, je 1 Level | keine |
| Rennen 3840×2160 | **256×256** (ARGB8888, 4444, 1555, 565) | **ja**, vollständig bis 1×1 (z. B. 256² mit 8 Levels, 64² mit 6) |

- **Im Rennen keine Textur über 256×256.** Das deckt sich mit dem Nutzerwissen „NFS5 unterstützt nur Texturen bis 256×256“ (`doc/knowledge.md`). Ob das eine harte Engine-Grenze ist oder nur an den Original-Assets liegt, zeigt das Log nicht. Das Frontend lädt größere Texturen, also kann der Treiberpfad sie verarbeiten. → PLAN.md 2.0.
- Upload-Format und FSH-Typ: siehe `FSH-REPACK.md` §11.3. Die ARGB4444-Texturen stammen überwiegend aus palettierten Fahrzeugtexturen (`7B`); Strecken-Texturen (`7D`) kommen als ARGB8888 an.
- **Dynamische Texturen:** 18 Texturen werden mehrfach hochgeladen, fast alle RGB565 128²/256² (vermutlich Menü-/Video-/Spiegel-Flächen). Für B4 irrelevant.

**Folgerungen für 3T.2:**
- Ein Fix, der jeden SUSPECT-Draw doppelt zeichnet (Zwei-Pass), würde ~2,1 Mio zusätzliche Batches pro 3600 Frames erzeugen (~600 je Frame). Das ist machbar, aber nicht gratis.
- Plan: (1) **Schwelle** als Shader-Variante (null Zusatzkosten): bei Blend + Z-Write Pixel mit `a < AlphaEdgeRef` verwerfen. (2) **Zwei-Pass nur für Texturen mit Teiltransparenz**: beim Upload wird pro Textur ein Flag „hat Alpha < 255“ ermittelt; opake Texturen bleiben bei einem Pass.

### 8.6 Fix 3T.2 (gebaut und installiert 2026-10-16, noch nicht im Spiel getestet)

| Was | Wo |
|---|---|
| Patch | `work/patches/dx11z/3T2-alpha-edge-fix.patch` (Kette: `pre-3T1/` → `3T1-measure.patch` → `3T2-alpha-edge-fix.patch`) |
| Installiert | `Game CD/Drivers/dx11z.dll` (MD5 `39f16bb8fd3030bb7afe422d53daed96`); Messbuild 3T.1 → `dx11z.dll.3t1` |
| Schalter | `[Dx11] AlphaEdgeFix = 2`, `AlphaEdgeRef = 128` (INI-Default im Code: `AlphaEdgeFix = 0` → Original-Verhalten) |

**Funktionsweise:**
- **Upload:** Jede Textur bekommt ein Flag `m_Translucent` (Level 0 enthält Texel mit Alpha < 255; DXT immer). RGB565 ist damit z. B. opak.
- **Draw:** Ein Batch wird nur behandelt, wenn das Spiel Blending **und** Z-Write gesetzt hat **und** der Batch teiltransparent ist: Textur 0/1 transluzent **oder** ein Vertex mit Diffuse-Alpha < 255 (Ausblenden über Vertexfarbe). Alle anderen Draws laufen unverändert.
- **`AlphaEdgeFix = 1` (Schwelle):** Pixel mit `a < AlphaEdgeRef/255` werden verworfen. Das kostet nichts extra. Kanten werden härter, und per Vertex-Alpha ausgeblendete Objekte verschwinden unterhalb der Schwelle früher.
- **`AlphaEdgeFix = 2` (Zwei-Pass):** Pass 1 zeichnet `a ≥ Ref` wie bisher mit Z-Write. Pass 2 zeichnet `a < Ref` mit Blending, aber **ohne Z-Write**. Weiche Kanten bleiben erhalten, und später gezeichnete Bäume übermalen den Saum, statt am Tiefentest zu scheitern. Kosten: ein zusätzlicher Draw je betroffenem Batch.
- Der HLSL-Shader wurde offline mit `fxc` (ps_4_0/vs_4_0) gegengeprüft; das Spiel kompiliert ihn zur Laufzeit.

**Bekannte Grenze:** Pass 2 schreibt keine Tiefe. Liegt ein Saum vor Geometrie, die *danach* gezeichnet wird und *hinter* ihm liegt, überdeckt diese den Saum (der Saum „verschwindet“ statt hell zu leuchten). Das ist der übliche Kompromiss ohne Tiefensortierung und optisch unauffällig.

**👤 Test 3T.3:**
1. Busch-Szene und Boot-Szene je mit `AlphaEdgeFix = 0`, `1` und `2` fahren (INI ändern, Spiel neu starten). Screenshots nach `doc/screenshots/b4/3t3_<szene>_fix<N>.png`.
2. Auf neue Artefakte achten: Zäune, Schilder, Rauch/Partikel, Schatten, Scheinwerferkegel, HUD, Menüs.
3. FPS-Eindruck je Variante (bei 3840×2160).
4. `Game CD/Drivers/dx11z.log` muss `Config AlphaEdgeFix …` mit dem eingestellten Wert zeigen. Beginnt das Log mit einem Shader-Fehler, sofort `dx11z.dll.3t1` zurückkopieren und melden.

### 8.7 Ergebnis 3T.3 (2026-10-16) – Busch-Szene mit `AlphaEdgeFix = 2` ✅

`doc/screenshots/b4/3t3_szene_fix2.png`: Der Busch vor der Baumwand ist **ohne hellen Saum**, und die Bäume sind an den Blattkanten durchgehend sichtbar. Nutzerurteil: „Test war erfolgreich.“

**Nicht abgedeckt:** Vergleich mit `AlphaEdgeFix = 0/1`, Boot-Szene (Tau), Suche nach neuen Artefakten auf anderen Strecken, FPS-Vergleich. Diese Punkte bleiben für eine spätere Fahrt offen; die Abnahme von B4 erfolgt auf Basis dieses Screenshots.

### 8.8 Ergebnis 3T.3 – Variante `AlphaEdgeFix = 1` (Schwelle) ❌ verworfen

Screenshots (2026-10-16, `doc/screenshots/b4/`):

| Datei | Befund (Nutzer) | Erklärung |
|---|---|---|
| `3t3_szene_fix1.png` | Ursprüngliches Fehlerbild am Busch **weiterhin vorhanden** | Pixel mit `a ≥ 0,5` werden weiter geblendet **und** schreiben Tiefe. Der Saum wird nur schmaler, verschwindet aber nicht. |
| `3t3_szene_fix1_car_reflection.png` | **Reflexionen auf dem Auto** falsch: harte, rosa Flecken statt weicher Spiegelung | Die Spiegelung ist eine geblendete Überlagerung mit überwiegend niedrigem Alpha. Die Schwelle verwirft alles unter 0,5, übrig bleiben nur die hellsten Stellen als harte Flecken. |
| `3t3_szene_fix1_shore_blending.png` | **Übergang Ufer → Meer** falsch: harte Kante statt Verlauf | Der Übergang wird über Alpha (Vertexfarbe/Textur) ausgeblendet. Die Schwelle macht aus dem Verlauf eine Stufe. |

**Folgerung:** Variante 1 behebt B4 nicht und beschädigt jede Fläche, die absichtlich halbtransparent geblendet wird. Das ist das in §8.6 genannte Risiko, nur deutlich größer als erwartet, weil das Spiel fast alles mit Blend + Z-Write zeichnet (§8.5). **Variante 1 nicht verwenden.** Sie bleibt nur als Vergleichsschalter im Code. Kandidat zum Entfernen beim nächsten Patch-Schritt.

**Variante 2 ist davon prinzipiell nicht betroffen:** Beide Pässe zusammen zeichnen alle Pixel mit unverändertem Blending, nur der schwache Anteil schreibt keine Tiefe. **Noch nicht explizit geprüft:** Autoreflexionen und Ufer/Meer mit `AlphaEdgeFix = 2`. Das gehört zur Abnahme von 3T.3 (Boot-Szene, Autoreflexion, Uferlinie, je ein Screenshot).

**Empfohlene Einstellung:** `AlphaEdgeFix = 2`, `AlphaEdgeRef = 128` (Ist-Stand der INI).

### 8.9 Abschluss B4 (2026-10-16) ✅

Gegenprüfung von `AlphaEdgeFix = 2` an Autoreflexion, Ufer/Meer-Übergang und Boot-Szene: **keine Auffälligkeiten** (Nutzer). Damit ist **B4 behoben** und Gate 3T bestanden (PLAN.md).

- **Gültige Konfiguration:** `[Dx11] AlphaEdgeFix = 2`, `AlphaEdgeRef = 128`, dx11z-Build mit Patch-Kette `pre-3T1` → `3T1-measure.patch` → `3T2-alpha-edge-fix.patch` (MD5 `39f16bb8fd3030bb7afe422d53daed96`).
- **Aufräumen beim nächsten Patch-Schritt:** Variante 1 entfernen (§8.8). Den Messcode (`Measure`) als Diagnose behalten; er ist bei `Measure = 0` wirkungslos.
- **3T.4 (Edge-Padding)** ist zurückgestellt, weil kein Rest-Saum sichtbar ist. Wieder aufgreifen, falls HD-Texturen (Phase 2) helle Kanten zeigen.

---

## 9. B7–B9: weitere Darstellungsfehler (gemeldet 2026-10-16)

Vom Nutzer vermutet: auch im Original vorhanden, wegen der geringen Auflösung kaum sichtbar. Belege: `doc/screenshots/b7-b9/`.

### 9.1 B7 – Dünner Rand an der Oberkante von Busch-Billboards

**Symptom** (`b7_bush_top_edge.png`): Der Busch besteht aus zwei gleichen, um 90° gedrehten Flächen. An der **Oberkante beider Flächen** ist eine feine, dunkle/grüne Linie zu sehen.

**Hypothese H-B7 (Textur-Wrapping):** Das Spiel setzt die Texturadressierung auf `WRAP` (Kachelung). Bilineare Filterung an der Oberkante (v ≈ 0) mischt dann die **unterste** Texelzeile mit hinein. Ist der Busch unten deckend grün, entsteht oben ein deckender grüner Strich, obwohl die oberste Zeile der Textur transparent ist. Das ist eine bekannte Schwäche von Billboards mit Wrap-Adressierung und passt zum Muster „nur oben, bei beiden Flächen gleich“.

**Gegenhypothese:** Die Textur selbst hat in der obersten Zeile deckende Pixel (Asset-Fehler).

**Textur-Befund (2026-10-16, `work/hd/edge_scan.py` auf coastal):** Objekt `0110` „shrb“ (Strauch, 256², Typ `7D`):
- Die oberste Zeile ist fast transparent (Alpha max. 51 ≈ 0,2). Das liegt trotzdem über der Alpha-Referenz des Spiels (≈ 0,04), der Alpha-Test lässt sie also durch.
- Die **unterste** Zeile hat genau **zwei deckende Abschnitte** (Spalten 62–79 und 185–195).
- Bei Wrap-Adressierung werden diese beiden Abschnitte an die Oberkante gemischt. Es entstehen zwei kurze, dünne Linien je Fläche. Das passt zu `b7_bush_top_edge.png` und stützt H-B7 deutlich.
- In den kleineren Mip-Stufen nimmt die Deckung der obersten Zeile leicht zu (64²: 2 % … 4²: 50 %), aus der Ferne wird der Effekt also eher stärker.

**Test:** Schalter `[Dx11] ClampUnitUv = 1`. Für Draws, deren Texturkoordinaten komplett in 0…1 liegen, wird auf `CLAMP` umgeschaltet. Bei solchen Draws ist das Ergebnis im Inneren identisch, nur die Kanten mischen nicht mehr die Gegenseite ein. Gekachelte Flächen (Koordinaten über 1) bleiben bei `WRAP`. Verschwindet der Strich → H-B7 bestätigt und der Schalter ist der Fix. Bleibt er → Asset-Fehler; dann die Kantenzeilen korrigieren (oberste und unterste Zeile von `0110` auf Alpha 0 setzen bzw. Mips neu erzeugen).

**Stand Test:** Lauf 1 und 2 (2026-10-16) liefen **ohne** die Testschalter. Die INI hatte eine eigene Überschrift `[Test]`, dx11z liest die Schalter aber nur aus `[Dx11]`. Deshalb waren auch `AlphaEdgeFix` (B4-Saum wieder sichtbar) und `Measure` aus. Korrigiert; Lauf 2 und 3 müssen wiederholt werden.

### 9.2 B8 – Rückseite von Fensterläden unsichtbar

**Symptom** (`b8_shutter_back_missing_a.png`, `…_b.png`): Aufgeklappte Fensterläden sind nur von vorn sichtbar. Von hinten sieht man durch sie hindurch.

**Ursache (sehr wahrscheinlich):** Die Läden sind einseitige Flächen, und das Spiel zeichnet mit Backface-Culling. Rückseiten werden verworfen. Das ist Original-Verhalten.

**Test:** Schalter `[Dx11] ForceTwoSided = 1` zeichnet alle Flächen beidseitig. Die Beleuchtung steckt in den Vertexfarben (vortransformierte Vertices), also sieht die Rückseite aus wie die Vorderseite.

**Risiken beim Erzwingen für alles:**
- Innenseiten geschlossener Körper werden mitgezeichnet. Das ist meist unsichtbar, kostet aber Füllrate.
- Flächen, die bewusst nur von einer Seite sichtbar sein sollen, etwa Scheiben aus der Cockpitsicht oder Innenseiten der Karosserie, können plötzlich die Sicht verdecken.
- Falls die Engine schon **vor** der Übergabe an dx11z selbst Rückseiten aussortiert, wirkt der Schalter dort nicht. Dann bleibt der Fehler, und die Ursache liegt in `gimme.dll`.

**Bewertung:** „Für alles erzwingen“ lohnt sich nur, wenn der Test keine solchen Nebenwirkungen zeigt. Sonst muss die Auswahl gezielter erfolgen (z. B. nur Strecken-Geometrie, nicht Fahrzeug).

### 9.3 B9 – Kaputtes Glas nur von außen sichtbar

**Symptom** (`b9_broken_glass_outside.png`, `b9_cockpit_view.webp`, `b9_rear_inside.png`): Die Textur für gesplittertes Glas ist von außen sichtbar, von innen nicht.

**Ursache:** wie B8. Die Scheibe ist eine einseitige Fläche, von innen ist sie die Rückseite. Im Original ergibt das Sinn: Die Textur ist deckend, und eine deckende Scheibe von innen würde die Sicht versperren.

**Mit `ForceTwoSided = 1`** wird das Glas von innen sichtbar, aber **deckend**. Aus der Cockpitsicht könnte die Frontscheibe dann die Sicht verdecken (auch die heile Scheibe, falls sie eine deckende Textur hat). Genau darauf ist beim Test zu achten.

**Halbtransparenz:** möglich, ist aber ein Asset-Eingriff in die Fahrzeugtextur (`.crp` → `.fsh`, Werkzeug `NFSPUTexture.exe`, ungetestet). Achtung: Bei Fahrzeugtexturen ist Alpha eine Material-Maske (`knowledge.md` §1). Ein beliebiger Zwischenwert wie 50 % ist dort nicht vorgesehen und kann als falsches Material gedeutet werden. Ob und wie die Engine Glas halbtransparent zeichnet, muss erst gemessen werden (Messlog: Blend-State beim Glas-Draw). → Gehört zu den Fahrzeugtexturen in Phase 2, nicht zum Schalter-Test.


### 9.4 Ergebnis Testfahrt `ClampUnitUv = 1` + `ForceTwoSided = 1` (2026-10-16)

Konfiguration laut Log: `AlphaEdgeFix 2`, `ClampUnitUv 1`, `ForceTwoSided 1`, `Measure 1` (`work/logs/3C11-twosided-clamp-*.log`).

| Bug | Ergebnis | Beleg |
|---|---|---|
| B4 | weiterhin behoben (Nutzer) | – |
| B7 | **nicht behoben** mit `ClampUnitUv = 1`. Nutzerbeobachtung: nur sichtbar, wenn man **von unten** zum Busch hochschaut (vor Himmel); von oben nicht, weil der untere Teil der Textur im Boden steckt | `b7_bush_top_edge_clamp1.png` |
| B8 | Fensterläden sind von hinten sichtbar | `b8_shutters_twosided_a.png`, `…_b.png` |
| B9 | **nicht behoben**, Autoscheiben von innen weiter unsichtbar | `b9_glass_inside_twosided.png`, `b9_glass_outside_twosided.webp` |
| **B11 (neu)** | Schilder, deren Rückseite schon als eigene Fläche modelliert ist, flackern. Vorder- und Rückseite liegen in derselben Ebene und kämpfen um die Tiefe (Z-Fighting) | `b11_sign_zfight_twosided.webp` |

**Ergebnis `ForceTwoSided = 2` (2026-10-16, `work/logs/3C12-twosided2-*.log`):** Die Schilder flackern nicht mehr (B11 ✅), die Fensterläden sind von hinten korrekt (B8 ✅).

**Rest-Effekt:** Auf der Rückseite des Test-Schilds `0043` ist am Rand oben links ein lila Saum zu sehen (`b11_sign_back_twosided2_rim.png`). Ursache: Im Rückseiten-Durchgang wurde auch der schwache Kantenanteil der gespiegelten Vorderseite gezeichnet (Zwei-Pass aus `AlphaEdgeFix`). Dieser Anteil schreibt keine Tiefe, und die echte Rückseite verdeckt ihn deshalb nicht. Lila erscheint er, weil der Magenta-Streifen der Testtextur dort über den Rand läuft. Mit normalen Texturen wäre es ein roter Saum. Dass die 512er-Vorderseite und die 256er-Rückseite ihre Kante leicht unterschiedlich filtern, verstärkt das.

**Fix 3C.14** (`work/patches/dx11z/3C14-backface-solid-only.patch`, Build MD5 `790ac900231c13d648a71e5e41c8f5cd`): Der Rückseiten-Durchgang zeichnet bei teiltransparenten Texturen nur noch den deckenden Teil (Alpha ≥ `AlphaEdgeRef`). Dieser Teil liegt deckungsgleich hinter der deckenden echten Rückseite und verliert dort den Tiefentest.

**B7 – Stand:** Offen ist, ob `ClampUnitUv` beim Busch überhaupt greift. Das tut es nur, wenn **alle** Texturkoordinaten des Batches in 0…1 liegen. Prüfung: Build 3C.13 mit `Measure = 3` färbt jeden Draw grün, bei dem Clamp aktiv ist. Ist der Busch grün und die Linie trotzdem da, ist H-B7 (Wrap) widerlegt. Weitere Kandidaten:
- Inhalt der obersten Texelzeile: Bei `0110` ragt ein Zweig bis an die Kante (Spalten 83–92, Alpha bis 206 in der zweiten Zeile).
- Ein anderes Objekt (Draht, Kante einer Nachbarfläche), das nur vor dem Himmel auffällt.

**Ergebnis `Measure = 3` (2026-10-16, `b7_bush_measure3_not_green.png`, `work/logs/3C13-greentint-*.log`):** Der Busch ist **nicht** grün, die Linien sind weiter da. `ClampUnitUv` greift beim Busch also gar nicht, weil seine Texturkoordinaten nicht in 0…1 liegen. Damit ist H-B7 weiterhin **ungetestet**, nicht widerlegt. Grün sind dagegen große Flächen wie Meer und Himmel.

**Nächster Schritt – Build 3C.15** (`work/patches/dx11z/3C15-uv-range-log.patch`): Das Messlog schreibt für jede teiltransparente Textur ab 128 px die Spanne der Texturkoordinaten (`M3T   uv tex <hash> … u … v …`). `work/hd/texture_hash.py` ordnet den Hash einem Objekt zu, z. B. `0110` shrb = `CE33539A`. Aus der Spanne folgt der passende Fix:
- Die Koordinaten liegen in einem verschobenen Einheitsbereich (z. B. v in 1…2) → Clamp nach Verschiebung um ganze Zahlen.
- Die Textur wird absichtlich gekachelt → Asset-Fix der Kantenzeilen.

**Ergebnis Koordinaten-Log (Normalbetrieb 3C.16, 2026-10-16, `work/logs/normalbetrieb-3C16-dx11z.log`):** Die Strauch-Textur `CE33539A` (= `0110` shrb) wird mit **u 0,000…0,490 und v 0,000…1,250** gezeichnet (165 000 Batches, 0 geklemmt).
- v reicht bis 1,25. Das unterste Viertel der Fläche (im Boden) wiederholt also den oberen Teil der Textur. Deshalb steht die Adressierung auf Wrap, und `ClampUnitUv = 1` konnte nie greifen.
- Die Fläche nutzt nur die **linke Hälfte** der Textur (u ≤ 0,49). In der untersten Texelzeile liegt dort genau **ein** deckender Abschnitt (Spalten 61–83).
- An der Oberkante (v = 0) mischt der Wrap diese unterste Zeile ein. Es entsteht eine kurze Linie je Fläche, also zwei Linien beim gekreuzten Busch, leicht links der Mitte.
- **H-B7 bestätigt.**

**Fix 3C.17 `ClampUnitUv = 2` (Kantenschutz)** (`work/patches/dx11z/3C17-edge-guard.patch`, Build MD5 `9d4271ee4e4a6600c679d5ae474783da`):
- Nur bei teiltransparenten Texturen und nur an den Seiten, an denen die Koordinaten eines Batches **genau** auf dem Texturrand enden (0 bzw. 1, ±0,002): Dort liest der Shader höchstens bis zur Mitte des Randtexels.
- Der Abstand richtet sich nach der Mip-Stufe: halbes Texel, mindestens die halbe Pixelbreite in Texturkoordinaten.
- Andere Seiten (beim Strauch v bis 1,25) und alle gekachelten Flächen bleiben unverändert.
- Erwartung beim Strauch: Schutz an u = 0 und v = 0, keine Linie mehr an der Oberkante.

**Ergebnis 3C.17 (2026-10-16, `b7_bush_edgeguard_3c17.png`, `work/logs/3C17-edgeguard-dx11z.log`):** Linie **weiter da**, obwohl der Schutz für `CE33539A` gelten müsste (u und v beginnen bei genau 0,000).

Nebenbefund aus dem Koordinaten-Log: Die meisten Strecken-Billboards (ARGB1555, andere Strecke) liegen bei 0,002…0,998. Das Spiel setzt dort also schon selbst einen Halbtexel-Abstand. Beim Strauch fehlt dieser Abstand (0,000).

Offene Erklärungen:
- (a) Die anisotrope Filterung (`Anisotropy = 16`) greift beim schrägen Blick von unten über den Rand hinaus.
- (b) Die Linie stammt gar nicht vom Wrap.

**Diagnose 3C.18** (`work/patches/dx11z/3C18-hard-clamp-diag.patch`, Build MD5 `c2d9b8282e68712a2368a62e5f7b8393`): `ClampUnitUv = 3` klemmt jede teiltransparente Textur hart (nur Test), `Measure = 3` färbt diese Draws grün.
- Test 1: `ClampUnitUv = 3`, `Measure = 3`. Busch grün und Linie weg → Wrap bestätigt, dann Test 2. Busch grün und Linie da → (b).
- Test 2: `ClampUnitUv = 2`, `Measure = 1`, `Anisotropy = 0`. Linie weg → Ursache ist (a).

**Ergebnis Test 1 ✅ (2026-10-16, `b7_test1_hardclamp_a/b.png`):** Busch grün, **Linie weg** → Wrap ist die Ursache von B7 (H-B7 endgültig bestätigt). Die senkrechten Balken sind die erwartete Nebenwirkung des harten Clamps auf Flächen mit u > 1 (gestreckte Randspalte); der Modus ist deshalb nur zur Diagnose geeignet.

**Ergebnis Test 2 (2026-10-16, `b7_test2_aniso0.png`):** Mit `Anisotropy = 0` ist die Linie weiter da. Die Anisotropie ist also nicht die Ursache.

**Ursache gefunden – `shrb` ist ein Textur-Atlas** (`b7_shrb_atlas_preview.png`):
- Links liegen **vier Büsche übereinander** (je 64 px), rechts **zwei Bäume** (je 128 px).
- Jeder Busch nutzt nur seinen Ausschnitt. Der oberste Busch wird mit **v 1,00…1,24** angesprochen, also eine Wiederholung höher (daher das Maximum 1,24 im Koordinaten-Log).
- An seiner Oberkante (v = 1,0) mischt der Wrap die **unterste Atlaszeile** ein, den Stammfuß des gelben Busches ganz unten (Spalten 61–83). Das sind die Linien.
- Gleiches droht an jeder inneren Zellgrenze: Die Oberkante eines Busches liest den Fuß des Busches darüber.
- Der Kantenschutz (3C.17) prüft nur 0/1 über den ganzen Batch und erreicht weder v = 1,0 noch die inneren Grenzen.
- Der harte Clamp (Test 1) hat die Linie zwar entfernt, den Busch mit v > 1 aber zu den senkrechten Balken gestreckt.

**Fix (Asset, 2026-10-16):** `work/hd/atlas_gutter.py coastal-b7 110 0:128:4 128:256:2`
- Setzt in allen Mip-Stufen die **unterste Zeile jeder Atlas-Zelle** auf Alpha 0, insgesamt 764 Texel.
- Diese Zeile ist der Fuß im Boden und unsichtbar. Es gibt dann nichts mehr, was eingemischt werden kann.
- Repack mit `fshtool` ok, Round-Trip identisch.
- Installiert als `Game CD/GameData/Track/coastal.fsh` (MD5 `ad633612da2faab2cf68d9ed5c882fa3`, enthält weiter das 512er-Test-Schild). Vorheriger Stand: `coastal.fsh.512test`.
- `ClampUnitUv` steht für den Test auf 0.

**Ergebnis ✅ (2026-10-16, `b7_fixed_atlas_gutter.png`):** Die Linie ist weg, und am Busch fehlt nichts Sichtbares. **B7 behoben** (für coastal `shrb`; andere Strecken mit ähnlichen Atlanten folgen mit der HD-Pipeline).

**Folgerung für Phase 2:** Atlanten mit Wrap-Adressierung brauchen **transparente Lücken an den Zellgrenzen** (und am Atlasrand). Das gehört als fester Schritt in die HD-Pipeline (2.2). Die dx11z-Modi `ClampUnitUv` 1–3 sind für B7 wirkungslos bzw. nur Diagnose und können beim nächsten Aufräumen entfallen.

**B9 – Folgerung:** Wenn die Rückseiten auch mit abgeschaltetem Culling in dx11z nicht erscheinen, gibt die Engine diese Dreiecke gar nicht erst ab. Sie verwirft Rückseiten von Fahrzeugen vermutlich schon selbst, in Software in `gimme.dll`. Ein Fix ist dann nur in der Engine möglich (3C.2-Umfeld) und nicht über dx11z. Die Strecken-Geometrie (Fensterläden) wird dagegen ungefiltert übergeben.

**B11 – Fix `ForceTwoSided = 2`** (Build 3C.12, `work/patches/dx11z/3C12-backface-pass.patch`):
- Statt das Culling abzuschalten, zeichnet dx11z jeden Dreiecks-Batch normal und danach **ein zweites Mal nur mit den sonst verworfenen Rückseiten**.
- Dieser zweite Durchgang ist in der Tiefe minimal nach hinten verschoben (`DepthBias 64` ≈ 4·10⁻⁶ plus `SlopeScaledDepthBias 1.0`).
- Hat ein Objekt schon eine eigene Rückseite in derselben Ebene, gewinnt diese immer. Fehlt sie, erscheint die gespiegelte Rückseite.
- Das ist die gewünschte Wirkung „nur für Objekte, bei denen es vorher nicht aktiv war“. Eine Erkennung pro Objekt ist dafür nicht nötig.
- Kosten: doppelte Draws für Dreiecke mit Culling.
- Grenze: Liegt die eigene Rückseite nicht exakt in derselben Ebene, sondern weiter als der Versatz entfernt, können sich beide überlagern. Dann den Versatz erhöhen.


### 9.5 B12 – Farbiger Rand um Schild-Rückseiten (2026-10-16)

**Symptom** (`b12_sign_back_rim_a.png`, `b12_sign_back_rim_b.webp`): Auf der Rückseite runder und dreieckiger Schilder ist ein roter Rand zu sehen, beim 512er-Test-Schild ein lila Rand. Die Vorderseite ist korrekt (`b12_sign_front.png`).

**Ursache:**
- `ForceTwoSided` zeichnet die Vorderseite gespiegelt und minimal nach hinten versetzt hinter die echte Rückseite.
- Die Rückseiten-Textur ist etwas **kleiner ausgeschnitten** als die Vorderseite (eine Ungenauigkeit im Asset).
- Am Rand liegt deshalb keine echte Rückseite, die verdecken könnte, und der Rand der Vorderseite wird sichtbar. 3C.14 (nur deckender Teil) reicht dafür nicht.
- Ohne `ForceTwoSided` tritt das nicht auf, weil das Spiel die Vorderseite von hinten nicht zeichnet.

**Fix 3C.20** (Build MD5 `7e1a536614358cf0089bf8953d7b6331`, im Gesamtpatch enthalten): Der Rückseiten-Durchgang läuft nur noch für **deckende** Texturen (ohne Alpha-Ausschnitt), z. B. Fensterläden. Ausgeschnittene Objekte (Schilder, Zäune, Vegetation) bekommen keine ergänzte Rückseite mehr. Grenze: Einseitige, ausgeschnittene Flächen ohne eigene Rückseite bleiben von hinten unsichtbar wie im Original.

**Kontrollstart ✅ (2026-10-16):** Schild-Rückseiten ohne Rand, Fensterläden weiter sichtbar, Busch ok. **B12 behoben.**

---

## 10. B10 – Profil fehlt sporadisch in der Profilauswahl

**Symptom:** Beim Spielstart fehlt das Profil „Sam“ in der Profilauswahl. Bei einem späteren Start ist es wieder da.

| Sitzung (2026-10-16) | dx11z-Schalter | Profil sichtbar? |
|---|---|---|
| ~11:26 | Build 3T.2, `AlphaEdgeFix = 2`, `Measure = 1`, `RaceColorDepth = 32` | ❌ |
| Läufe 1–3 (11:34–11:48) | alle Schalter wirkungslos = 0 (`[Test]`-Überschrift), `Measure` aus | ✅ |
| ~11:55 | `AlphaEdgeFix = 2`, `Measure = 1`, `ClampUnitUv = 1`, `ForceTwoSided = 1` | ❌ |
| ~12:15 | `AlphaEdgeFix = 2`, `Measure = 1`, `ClampUnitUv = 1`, `ForceTwoSided = 2` | ❌ |
| ~12:29 | `AlphaEdgeFix = 2`, `Measure = 3`, `ClampUnitUv = 1`, `ForceTwoSided = 2` | ❌ |
| ~12:50 Start 1 | alle Schalter 0 (Log bestätigt), Build 3C.15 | ❓ nicht erhoben (Missverständnis: „Bug ist da“ bezog sich auf B7) |
| ~13:00 Start A | Original-`dx11z.dll` aus dem Release | ✅ (`work/logs/B10-startA-*`) |
| Start 1b | Build 3C.15, alle Schalter 0 | ✅ (`work/logs/B10-start1b-*`) |
| Start 2 | nur `Measure = 1` | ✅ (`work/logs/B10-start2-*`) |
| Start 3 | nur `AlphaEdgeFix = 2` | ❌ (`work/logs/B10-start3-*`) – Liste leer, beim Überfahren mit der Maus ertönt aber der Auswahl-Ton; mit einem zweiten Profil lässt sich in der „leeren“ Liste wechseln |

**Befunde:**
- Die Spielstand-Dateien sind jedes Mal intakt. `2Sam.sav` wird vom Spiel weiter beschrieben, `player.lst` = `2Sam`, `pic16.fsh` unverändert.
- Weder dx11z noch der übrige Mod greifen auf `SaveData` zu. Die dx11z-Schalter wirken nur auf das Zeichnen.
- Im Mod-Log steht schon **6 s nach dem Start**, also vor oder in der Profilauswahl: `'gimme.dll' shape palette count … is not valid, palette ignored`. Das ist der bekannte Originalfehler im Bild-Parser von `gimme.dll` (`BUGS.md` §3.4). Er liest einen **uninitialisierten Wert vom Heap**, und der Mod verwirft ihn. Was dort steht, hängt von der Speicherbelegung der Sitzung ab, also auch davon, wie viel Speicher unsere Builds und Schalter belegen.

**Hypothesen:**
- **H-B10a:** Ein Originalfehler, der zufällig vom Speicherinhalt abhängt, z. B. der genannte Parser beim Profilbild. Der Mod und unsere Änderungen wären dann nur indirekt beteiligt, über eine andere Speicherbelegung.
- **H-B10b:** Einer der dx11z-Schalter (am ehesten `Measure` oder `AlphaEdgeFix`) macht den Profileintrag in der Liste unsichtbar. Der Eintrag wäre dann vorhanden, aber nicht gezeichnet.

**Ursache gefunden (2026-10-16): `AlphaEdgeFix = 2`** (Start 3). Die Profilliste wird geladen und ist bedienbar, sie wird nur **nicht gezeichnet**. Damit ist H-B10b bestätigt.

**Mechanismus (sehr wahrscheinlich):** Das Menü grenzt die Liste über die Tiefe ab. Es zeichnet erst eine voll transparente Fläche **ohne Alpha-Test**, aber mit Z-Write, und danach die Einträge mit Tiefentest dagegen. Der Edge-Fix hat diese Fläche als „teiltransparent“ behandelt: Pass 1 hat sie verworfen, Pass 2 ohne Tiefe gezeichnet. Die Maske fehlte, also scheitern die Einträge am Tiefentest. Im Messlog 3T.1 gibt es genau zwei Blend+Z-Write-States **ohne** Alpha-Test (#10, #14), beide nur im Menü. Die Welt (B4) wird dagegen immer mit Alpha-Test gezeichnet.

**Fix 3C.16** (`work/patches/dx11z/3C16-edgefix-alphatest-only.patch`, Build MD5 `130986f21fe5afc3cc264e10565ab356`): Edge-Split und Rückseiten-Kantenverwurf nur noch bei **aktivem Alpha-Test**. **Gegenprobe ✅ (2026-10-16, `work/logs/B10-3C16-gegenprobe-*`):** Mit `AlphaEdgeFix = 2` ist die Profilliste sichtbar, und der Busch-Saum (B4) bleibt behoben. **B10 behoben.**

**Nebenbefund B7:** Die Linien am Busch sind auch mit **allen Schaltern auf 0** (Start 1) und mit der **Original-`dx11z.dll`** (Start A, `b7_bush_original_dx11z.png`) da. B7 ist also kein Fehler unserer dx11z-Änderungen.

**Eingrenzung (👤):** Bei jedem Start notieren, ob das Profil sichtbar ist, und danach `Game CD/nfs5_enhanced.log` + `Drivers/dx11z.log` sichern.
1. Alle vier Schalter auf 0, zweimal starten.
2. Nur `Measure = 1`.
3. Nur `AlphaEdgeFix = 2`.

Zusatzfrage für H-B10b: Ist bei fehlendem Profil die Liste kürzer (Eintrag fehlt), oder ist eine leere Zeile da, die sich auswählen lässt?

---

## 11. B14 – Verschmierte Textur an einem einzelnen Balkon (gemeldet 2026-10-16)

**Symptom** (`doc/screenshots/b14_balcony_left_smear.png`): Bei einem Holzbalkon ist der linke Teil mit waagerecht verschmierter, gestreifter Textur dargestellt. Fünf gleichartige Balkone in der Nähe sind korrekt. Der Fehler tritt bei jeder Sichtweite auf.

**Einordnung:** Das Muster passt zu Texturkoordinaten, die an dieser Fläche in einer Richtung fast zusammenfallen. Dann wird eine einzelne Texelspalte über die ganze Fläche gezogen. Weil nur eine Instanz betroffen ist, liegt ein Mapping-Fehler in der Strecken-Geometrie nahe (Original-Asset).

**Gegentest:** dieselbe Stelle mit `ForceTwoSided = 0` ansehen. Bleibt der Fehler, ist es das Original-Asset. Verschwindet er, ist es eine von dx11z ergänzte Rückseite: Dann diese Fläche über den Textur-Hash im Messlog bestimmen und den Rückseiten-Durchgang dafür prüfen.

**Ergebnis Gegentest (2026-10-16):** Der Fehler tritt auch mit `ForceTwoSided = 0` auf → **Original-Asset** (Mapping-Fehler der Strecke). Vom Nutzer als nicht dringend eingestuft, zurückgestellt.

**Fix-Weg bei Original-Asset:** Die Texturkoordinaten stecken in der Strecken-Geometrie, nicht in der Textur. Ein HD-Textursatz behebt das nicht. Das wäre ein Eingriff ins Streckenformat, das noch nicht dokumentiert ist, daher niedrige Priorität.

## 12. B15 – Ungenaue Spiegelung im Wasser (gemeldet 2026-10-16)

**Symptom** (`doc/screenshots/b15_water_reflection_boat.png`, mit neuer Skybox-Textur aufgenommen): Unter dem roten Fischerboot liegt eine gespiegelte Kopie. Sie ist richtig herum gekippt (Kajüte unten, Mast als feine Linie nach unten), aber grau und fast ohne Textur, eher ein Schatten als eine Spiegelung.

**Arbeitsmodell (Nutzer-Vermutung, passt zum Bild):** Die Engine hat keine echte Spiegelung (kein Render-to-Texture). Sie zeichnet Objekte auf dem Wasser ein zweites Mal, an der Wasserhöhe gespiegelt, und legt die halb durchsichtige Wasserfläche darüber.

**Mögliche Ursachen für das Grau:**
1. **Normalen nicht mitgespiegelt:** Die Kopie wird beleuchtet, als zeige sie nach unten, und wird dunkel → Original-Fehler.
2. **Gewollte Abtönung:** Material/Nebel der Kopie bewusst dunkel, dazu deckt das Wasser stark → Original-Design.
3. **Seiteneffekt der Mods:** Spiegeln dreht die Umlaufrichtung. `ForceTwoSided` oder die Licht-Emulation in dx11z könnten eingreifen.

**Eingrenzung (👤, offen):**

| Test | Einstellung | Aussage |
|---|---|---|
| T1 | `ForceTwoSided = 0`, `AlphaEdgeFix = 0` | Ändert sich das Bild, liegt es an unseren Fixes (Ursache 3) |
| T2 | Original `dx11z.dll` + `nfs5_enhanced.asi.bak` | Grau auch dort → nicht unsere Builds |
| T3 | Original-Renderer ohne Mod bzw. alter Screenshot | Fehler original → Ursache 1 oder 2 |

**Falls original:** Spiegelpfad in Ghidra suchen (Matrix mit Skalierung (1, −1, 1), Wechsel des Cull-Modus) und prüfen, ob Normalen bzw. Licht mitgespiegelt werden. Möglicher Fix: Option, die die Kopie korrekt beleuchtet und die Deckkraft des Wassers abstimmt.

**Statische Suche (2026-10-18):** dx11z enthält keinen Wasser- oder Spiegel-Code. `porsche.exe`/`gimme.dll` enthalten keine Strings „reflect“/„mirror“. „water“ kommt nur als Ebenen-Modus der Himmels-Konfiguration vor (`FUN_0045fb80`: `basicwater` `0x202`, `multinormalwater` `0x204` … Flag `0x200`), das ist die Spiegelung des Himmels. Der einzige Kandidat „−1,0 + Cull-Modus“ (`FUN_0043cdd0`) ist ein Licht-Billboard. Neue Hypothese **H-B15a:** Die Kopie ist fertige, dunkle Geometrie im Strecken-Asset (eigene Textur), kein Spiegel-Durchgang der Engine. T1 auf Wunsch des Nutzers übersprungen.

**Messlauf M-B15 (dx11z, im Gesamtpatch):** Der Schnappschuss mit der Rollen-Taste (`Measure = 1`) schreibt je Textur jetzt zusätzlich den Cull-Modus des Spiels (eigene Gruppe je Modus), den Flächenanteil im Uhrzeigersinn (`cw %`) und das Bildschirm-Rechteck (`rect x0,y0-x1,y1`). Von den 40 größten Gruppen wird die Textur als `m3t_<hash>_<b>x<h>.bmp` neben `dx11z.log` abgelegt (höchstens 64 je Sitzung).

**Auswertung:** Gruppe mit dem Rechteck des Boots und Gruppe direkt darunter vergleichen.

| Befund | Bedeutung |
|---|---|
| Gleicher Hash, aber anderer Cull-Modus oder umgekehrter `cw %` | Die Engine zeichnet das Boot gespiegelt nach (Ursache 1/2) → Spiegelpfad über die Aufrufer (`M3T caller`) suchen |
| Eigener Hash, dunkle/graue Textur im BMP | H-B15a: Die Kopie liegt im Asset → Fix über Textur-Ersatz für diesen Hash (dx11z) oder das Streckenformat |
| Keine Gruppe unter dem Boot | Die Kopie gehört zum Wasser selbst (Wassertextur mit eingemaltem Boot) |

**Ergebnis M-B15 (2026-10-18, Schnappschuss #1, `doc/screenshots/b15/`):** H-B15a bestätigt, die Spiegelung ist **Original-Asset**.

| Gruppe | Textur | Rechteck (800×600) | Vertexfarbe | Cull |
|---|---|---|---|---|
| Boot | `BF250B72` 128×128 ARGB8888 (Rumpf, Kajüte, Bullaugen) | 394,244–450,301 | 128,6 95,3 107,3 | 1 |
| Spiegelung | `41C66523` **64×64**, eigenes, vorgefertigtes Bild des Boots (Fenster, roter Rumpf, Bullaugen), Alpha als weicher Verlauf | 394,300–450,357, direkt darunter | **66,0 59,0 82,0** | 2 |
| Felsen-Spiegelung (gleiches Prinzip) | `92CDBBAB` 64×64, grüne Felsfarbe mit Alpha-Verlauf | 262,289–523,350 | 74,5 63,2 81,4 | 2 |

- Die Kopie ist eigene Geometrie mit eigener Textur. Die Engine spiegelt nichts, und kein Durchgang mit umgekehrtem Cull liegt im Wasserbereich. Die Gruppen mit Cull 3 liegen alle bei 294–504 × 0–73, das ist der Rückspiegel.
- Aufrufer: Beide laufen über die Render-Warteschlange `FUN_00466660` (aus `FUN_00467290` < `FUN_00468ae0`). Die Spiegelung kommt aus der **ersten Liste** (`this+4`, über `FUN_004668d0`, Rücksprung `0x466699`), das Boot aus der dritten (`this+0xC`, Rücksprung `0x46672E`). Die Spiegelungen werden also vor der übrigen Szenerie gezeichnet.
- Warum sie grau wirkt: Die Textur ist ein Viertel so groß wie die des Boots und auf ~56×57 px gestreckt, die Vertexfarbe ist etwa halb so hell und bläulich, und der Alpha-Verlauf blendet sie zusätzlich aus. Das ist Original-Design für 640×480. B15 ist also kein Mod-Fehler, ein Fix ist eine Verbesserung des Originals.

**Fix-Wege:**

| Weg | Inhalt | Bewertung |
|---|---|---|
| A | HD-Spiegeltextur: `41C66523` (und je Strecke die übrigen Spiegeltexturen) aus der Boots-Textur neu erzeugen (gespiegelt, höher aufgelöst, Original-Alpha-Verlauf hochskaliert), per FSH-Repack (Weg F, `FSH-REPACK.md` §10) | schärfste Spiegelung; Teil von Phase 2, je Objekt Handarbeit, `.fsh` der Strecke muss gefunden werden |
| B | dx11z: Vertexfarbe der Spiegel-Draws anheben (Hash-Liste oder Erkennung über die erste Warteschlangen-Liste per `lfsp.asi`) | wirkt sofort und auf allen Strecken; bleibt unscharf |
| C | A + B | bestes Ergebnis, größter Aufwand |

**Entscheidung 👤 (2026-10-18):** Weg **E**: die fertigen Spiegelungen weglassen, die Original-Objekte an der Wasserebene gespiegelt neu zeichnen und einen Wasser-Farbfilter darüberlegen (Vorschlag des Nutzers). A/B lassen die Schattierungsfehler des Assets stehen.

**Messlauf M-B15b (2026-10-18, lfsp v7f + dx11z `62522553…`, `work/logs/b15_queue_dx11z.log`):** `lfsp` meldet dx11z über `LfspQueueList`, welche Liste der Warteschlange `FUN_00466660` gerade zeichnet (Markierungen an `0x46666D`, `0x4666B6`, `0x466703`, `0x466744`, Rücksetzen nach `0x467293`). Der Schnappschuss gruppiert zusätzlich nach Liste.

| Liste | Füllung (`FUN_00450de0`) | Inhalt an der Boot-Stelle |
|---|---|---|
| 0 | außerhalb der Warteschlange | Himmel, Rückspiegel-Rahmen, HUD |
| 1 | Flag `0x20000` (`FUN_004668f0`) | **deckende Strecke** (Straße, Fels, Gelände: `47A9E41D`, `E32211E0`, `DE8EA703`, `F7697B03` …) **und** die Spiegelungen `41C66523` (Boot) und `92CDBBAB` (Fels). Halbtransparent sind in Liste 1 nur diese beiden |
| 2 | Flag `0x100000` | an dieser Stelle leer |
| 3 | `0x10000`/`0x40000` | alles Halbtransparente: **Wasser `28A6F8AA`** (64×64, gleichmäßiges Alpha), Ufer-Übergang `63B3CF44` (Alpha-Verlauf), Boot `BF250B72`, Autos, Gebäude, Schatten `66252345` |

- Liste 1 darf nicht ganz entfallen (Einwand der B21-Sitzung bestätigt). Die Spiegelungen erkennt man an **Liste 1 + Alpha-Blending**. Die Vertexfarbe ist auffällig fest (66 59 82 bzw. 74 63 82).
- `51347DAA` (Fels, deckend, Liste 1) trägt dieselbe Vertexfarbe 66 59 82 und ist vermutlich ebenfalls Spiegel-Geometrie ohne Alpha. Offen.
- Die Wasserfläche wird in Liste 3 nach der deckenden Strecke gezeichnet. Das Boot liegt ebenfalls in Liste 3 und kann vor oder nach dem Wasser kommen.
- **Nebenwirkung 👤 (2026-10-18):** „Liste 1 + Alpha-Blending“ reicht nicht als Merkmal. An anderen Stellen liegen auch die Baumstreifen auf Felskämmen halbtransparent in Liste 1 und fielen mit `RemoveBakedReflections = 1` weg (Felskante ohne Bäume). Fix: entfernt werden nur noch die Texturen aus `[Water] BakedTextures` (Vorgabe `41C66523,92CDBBAB`). Weitere Spiegel-Texturen anderer Strecken müssen per Messlog ergänzt werden.

## 13. B16 – Boxster: Netz im Überrollbügel nur von vorn sichtbar (gemeldet 2026-10-16)

**Symptom:** Von vorn ist der rote Bügel mit einem karierten Netz gefüllt (`b16_boxster_rollbar_front.png`). Von hinten ist der Bügel schwarz und das Netz fehlt, man sieht durch ihn auf den Sitz (`b16_boxster_rollbar_back.png`). Tritt auch im Originalspiel auf; `ForceTwoSided = 1` behebt es nicht.

**Einordnung:**
- Das Netz ist eine einseitige Fläche mit Alpha-Ausschnitt. `ForceTwoSided` verdoppelt absichtlich nur **deckende** Texturen (wegen B11/B12, §9.4/§9.5), deshalb wird das Netz übersprungen.
- Die schwarze Rückseite des Bügels ist eigene, vereinfachte Geometrie. Das Netz wurde dort nicht nachgebaut → Modellfehler im Original.

**Messlauf `Measure = 2` (2026-10-16, `work/logs/b16-measure2/`):**
- Der installierte Messbuild (Stand der Licht-Messungen) schreibt Hashes nur für Licht-Durchgänge und für halbtransparente Texturen ab 128 px (`uv tex`). Eine Zuordnung Draw ↔ Textur für das Netz liefert er nicht.
- Fahrzeugtexturen erscheinen als **256×256 ARGB4444** (Kandidaten im Log u. a. `1D94F06A`, `15F0BCDC`, `A5ED93E8`, `02742C36`, `44850823`, `A3DDE0F0`). Das sind ganze Fahrzeug-Texturseiten; das Netz ist nur ein Ausschnitt darin.
- `boxsterD.fsh`/`boxsterL.fsh` enthalten nur Schmutz-/Schadens- bzw. Lichtstücke (palettiert `7B`, an Positionen auf zwei 256er-Seiten). Die Lack-/Innenraumtexturen mit dem Netz liegen in `boxster.crp`.

**Folge für den Fix:** Eine Hash-Liste für `ForceTwoSided` würde die **ganze Texturseite** des Boxsters doppelseitig machen, nicht nur das Netz. Das ist bei geschlossenen Karosserien meist unsichtbar (Rückseiten liegen hinter Vorderseiten), birgt aber das Risiko von B11/B12 an deckungsgleichen Flächen.

**Wege:**

| Weg | Inhalt | Bewertung |
|---|---|---|
| A | dx11z: Hash-Liste `ForceTwoSidedHashes`, auch für Ausschnitt-Texturen | einfach; wirkt auf die ganze Texturseite, Testfahrt muss B11/B12 am Boxster ausschließen |
| A' | wie A, zusätzlich nur innerhalb eines UV-Rechtecks (das Netz in der Seite) | gezielt; braucht die Lage des Netzes in der Seite |
| B | Modell: Netzfläche in `boxster.crp` umgekehrt duplizieren | sauberste Lösung, aber Eingriff ins `.crp`-Format (`FENSTER-VERDECK.md`, Teile `Atri`) |

**Nächster Schritt:** Texturseite des Netzes bestimmen, dafür ein Messbuild, der beim Druck auf F11 die Texturen der Fahrzeug-Draws dumpt, oder die Texturen aus `boxster.crp` entpacken (CRPInspector/`NFSPUTexture.exe` im Editing Kit). Weil der Messbuild im selben Quellbaum wie die Licht-Arbeit der anderen Sitzung entsteht, vorher abstimmen.

## 14. B17 – Scheinwerferlicht erreicht nicht jede Geometrie (gemeldet 2026-10-17)

**Symptom** (`doc/screenshots/b17_headlight_cliff_partial.webp`): Das Auto fährt auf eine Felswand zu. Die rechte Wandfläche ist vom Scheinwerfer aufgehellt (gelbgrün), die linke direkt daneben nicht (grau-violett). Zwischen beiden Flächen verläuft eine harte senkrechte Kante.

**Stand:** nur aufgenommen, auf Wunsch des Nutzers noch nicht untersucht. Bezug: Scheinwerfer-Funktion der Licht-Arbeit (`Headlights`/`HeadlightsDay` in `[Dx11]`, `doc/LICHT.md`).

## 15. B18 – Schadensanzeige: Farbverlauf zerfällt in Linien (gemeldet 2026-10-17)

**Symptom** (`doc/screenshots/b18_hud_damage_gradient_lines.png`): Im HUD-Element „damage“ liegt zwischen „0%“ und „100%“ ein Farbverlauf von Grün nach Rot. Er sollte eine durchgehende Fläche sein, erscheint aber als mehrere dünne parallele Linien mit Lücken dazwischen.

**Stand:** nur aufgenommen, auf Wunsch des Nutzers noch nicht untersucht. Vermutung des Nutzers: Folge der höheren Auflösung.

## 16. B19 – Risse in der Strecke aus der Ferne (gemeldet 2026-10-17)

**Symptom** (`doc/screenshots/b19/`): In mittlerer Entfernung kleine helle Punkte entlang der Kante Straße/Grünstreifen in Himmelsfarbe (`b19_gap_far_marked.webp`); an anderer Stelle fehlt ein großes Dreieck Gelände (`b19_triangle_missing.png`). Fährt man näher heran, werden die Risse kleiner (`b19_gap_near.png`) und verschwinden dann ganz. Tritt mit `DrawDistanceScale = 1.0` und `FarClipMultiplier = 1.0` auf.

**Befund:**
- Himmelsfarbe statt Bodenfarbe → echte Lücke zwischen Flächen, kein Z-Fighting. Der Shader in dx11z ändert keine Positionen (nur Farben), der Treiber scheidet aus.
- Abhängig von der Entfernung → Wechsel der Detailstufe. `FUN_00450de0` (Strecken-Abschnitt in die Zeichen-Warteschlange): Kopf `FUN_0043c7d0(this, 4, -1)`, Anzahl Stufen bei `+0x0C`, Stufen-Einträge zu 12 Byte ab `+0x64`, Reichweite als `ushort` bei Eintrag `+0x0A`. Die Schleife `0x450E2D–0x450E7B` nimmt die **erste Stufe mit Reichweite² > Entfernung**; liegt der Abschnitt jenseits der letzten Stufe, wird er nicht gezeichnet. Mit gesetztem `DAT_006197e5` (vermutlich niedrige Streckendetails) beginnt die Suche bei Stufe 1.
- Die gröberen Stufen schließen offenbar nicht dicht an die Nachbarn an (Originalfehler, in 640×480 kaum sichtbar).

**Fix (LFSP v6f, `lfsp.ini [World] TrackDetail = 1`):** Byte-Patch bei `0x450E86`: `LEA EAX,[ESI+ESI*2]; LEA EDX,[EDI+EAX*4+0x64]` → `LEA EDX,[EDI+0x64]; NOP×4`. Die Reichweiten-Schleife bleibt (Abschnitte jenseits der letzten Reichweite fallen weiter weg), gezeichnet wird aber immer Stufe 0. Log: `Patch Strecke feinste Detailstufe @00450E86 installiert`.

**Ergebnis 👤 (2026-10-17):** Die kleinen Risse sind mit `TrackDetail = 1` weg ✅. Das große fehlende Dreieck (Gelände samt Bäumen, Himmel dahinter) bleibt (`b19_triangle_after_fix.png`) → eigene Ursache, nicht die Detailstufe. 👤: Das Dreieck tritt bei DDS = 1 auf, auch schon vor `TrackDetail = 1`; einige Meter näher ist es weg (Test A, `b19_triangle_missing.png`).

**Hypothese H-B19b:** Der Abschnitt liegt jenseits der Reichweite seiner letzten Detailstufe und wird in `FUN_00450de0` verworfen (Schleifenende `0x450E7D` → `return`), während die Nachbarn mit größerer Reichweite noch gezeichnet werden. Möglicher Bezug zu B2.

**Test LFSP v6g, `TrackDetail = 2`:** zusätzlich `0x450E7D` `POP EDI; POP ESI` → `JMP 0x450E86`, d. h. ausgewählte Abschnitte werden unabhängig von ihrer Reichweite gezeichnet (nur zusammen mit dem Patch aus Stufe 1). Dreieck weg → H-B19b bestätigt. Bleibt es → Ursache in der Chunk-Auswahl `0x47F550` (B13-Hook `WorldCullVolume`) oder davor.

**Ergebnis v6g/v6h `TrackDetail = 2` (👤 2026-10-17, DDS = 1):** Das Dreieck ist weg (`b19_triangle_fixed_td2.png`) → **H-B19b bestätigt**. Nebenwirkungen:
- Über der Baumreihe erscheinen Bäume späterer Streckenteile (gewollt: jetzt sichtbar, was die Chunk-Auswahl liefert; 👤 kein Bug).
- An der Küste ist in der Ferne ein Teil (Felsinsel) gezeichnet, davor fehlt Strecke (`b19_coast_edge_td2.webp`). Deutung: Ohne die Reichweite je Abschnitt bestimmt nur noch die Chunk-Auswahl `0x47F550` den Rand. Sie nimmt ganze Chunks, deren Fläche den Sichtkeil (Radius = Sichtweite `0x6197CC`, `FUN_0044f1a0`) berührt; ein großer Chunk ragt hinein und wird ganz gezeichnet, ein näherer, aber außerhalb liegender nicht. Der Rand ist dadurch zackig statt rund.
- Mögliche Verfeinerung (Stufe 3, nicht gebaut): Abschnitte nicht nach ihrer eigenen Reichweite, sondern einheitlich nach der Sichtweite `0x6197CC` abschneiden (runder Rand, skaliert mit DDS). Vorher per Log klären, ob Entfernung (`param_2`, quadriert) und `0x6197CC` dieselbe Einheit haben.

**Ergebnis DDS = 3 (👤 2026-10-17, `b19_coast_dds3.png`):** Der Teil an der Küste erscheint nicht vorzeitig; der Aufbau wirkt wie im Original, nur weiter entfernt. Stufe 3 ist damit vorerst nicht nötig. Bei DDS = 4 ändert sich das Verhalten etwas → gehört zu B2 (`§2`, feste Tabellen der Engine), nicht zu B19.

**Stand nach DDS-3-Test:** Standard `TrackDetail = 2` (Code und INI-Vorlage, Commit `a262335`).

**Rückschlag (👤 2026-10-17, `b20_floating_section.webp`):** Mit `TrackDetail = 2` schwebt auf einer Alpen-/Normandie-Strecke ein weit entfernter Abschnitt (Hecke/Gelände) frei vor dem Bergpanorama. Deutung: Die Reichweite je Abschnitt ist dort **absichtlich** kurz, damit ferne Teile, die die Chunk-Auswahl mitliefert, nicht vor dem Hintergrund erscheinen. Stufe 2 hebt das auf → als Standard ungeeignet.

**Stufe „Reichweite × Faktor“ (LFSP v6j, `TrackDetail = 1`, `TrackRange = 1.5`):** Die Grenze je Abschnitt bleibt, liegt aber um den Faktor weiter. Umsetzung: `0x450E48–0x450E50` (`FILD qword [EBP-0x10]; LEA; FSTP [EBP-0xC]`) springt in `TrackDistDetour`, das die Entfernung² vor dem Vergleich mit `1/TrackRange²` multipliziert. `[EBP-0xC]` dient nur dem Vergleich; die Sortier-Entfernung `param_2` der Warteschlange bleibt unverändert. Log: `Strecke: TrackDetail 1, TrackRange 1.50`, `Patch Strecke Reichweite x TrackRange @00450E48 installiert`. Standard in Code und INI jetzt `TrackDetail = 1`, `TrackRange = 1.5`.

**Ergebnis v6j (👤 2026-10-17, DDS = 3):** Der schwebende Abschnitt bleibt; dazu ein Loch im Boden nah vor dem Auto. Gegenprobe mit `TrackDetail = 0` (keine Strecken-Patches): **beides tritt auch ohne LFSP auf** → nicht B19, eigene Bugs B20/B21 (§17, §18). Die Deutung „Reichweite absichtlich kurz“ oben ist damit nicht belegt. INI wieder `TrackDetail = 1`, `TrackRange = 1.5`.

**Ergebnis (👤 2026-10-17):** Das Dreieck (Küsten-Strecke, DDS 1) bleibt mit `TrackDetail = 1` + `TrackRange = 1.5` weg ✅.

**Stand B19:** ✅ behoben mit LFSP v6j-Standard `[World] TrackDetail = 1`, `TrackRange = 1.5`.

## 17. B20 – Ferner Streckenabschnitt schwebt vor dem Panorama (gemeldet 2026-10-17)

**Symptom** (`screenshots/b20_floating_section.webp`, `b20_floating_section_close.webp`): Alpen-Strecke, Blick über den Zaun ins Tal. Vor dem Bergpanorama schwebt ein Stück Strecke mit Straße, Hecke und Gelände, ohne Verbindung zum Boden.

**Stand:** tritt mit und ohne LFSP-Strecken-Patches auf (👤-Gegenprobe `TrackDetail = 0`), auch bei DDS = 1 (👤 2026-10-17) → nicht die erhöhte Sichtweite. Offen: tritt es im Spiel ohne Mod auf (Originalfehler) oder durch die übrigen Eingriffe (Hor+-Hooks `WorldCullVolume`/`SkyCullPlanes`, dx11z)?

**Ursache (Messung LFSP v7c, 👤 2026-10-18, Kamera 400 37 −118, Blick 128°):** Für Objekte ab 260–360 m zeigt die Messung eine kleine Gruppe, deren Sichtlinie zur Mitte genau durch eine **Lücke zwischen zwei Verdeckungswänden** geht (Ids 1020–1023, 265–268, 1186 um 590…690 / −290…−340, „Wand 0 → gezeichnet“). Das Gelände direkt daneben, nur 5–10° weiter seitlich und gleich weit entfernt (Ids 262–274, 1024–1027), fällt an der Wand weg. Übrig bleibt eine Insel ohne Umgebung vor dem Panorama. Der Verdeckungstest `FUN_00488b50` prüft nur die Linie zur Objektmitte (vgl. B21). Die Liste enthält zwei Durchgänge (Hauptkamera 128° und vermutlich Rückspiegel −52°); der Winkel-Filter nimmt jeweils den Blick des laufenden Durchgangs.

**Fix LFSP v7d, `[World] TrackOcclusionEdge = 1.0`:** Ab `TrackOcclusionRange` ruft `OcclusionReject` den Wandtest des Spiels zusätzlich für die beiden Randpunkte der Objekt-Kugel auf (Mitte ± Radius × Faktor, quer zur Sichtlinie in der Draufsicht; gleicher Flag-Schalter `0x400020`, Byte `[ESP+0x20]` am Sprungpunkt). Kreuzt eine der drei Linien eine Wand, fällt das Objekt weg. Objekte am Ende einer Wand verschwinden so mit ihren Nachbarn. `0` = nur die Mitte wie im Spiel. Die B20-Messung zeigt dazu `Rand` (Bit 0 links, Bit 1 rechts).

**Ergebnis v7d (👤 2026-10-18, `screenshots/b20_edge_v7d.png`):** Der schwebende Teil ist deutlich kleiner (ein Objekt mit Bäumen bleibt). Nebenwirkung: Nähere Streckenteile (ab 150 m) erscheinen zu spät, weil ihre Ränder schon an einer Wand liegen. **LFSP v7e:** Der Rand-Test gilt erst ab `TrackOcclusionEdgeRange` (Vorgabe 300 m, Abstand minus Radius); `TrackOcclusionEdge` bleibt der Faktor auf den Radius.

**Messwerkzeug (dx11z, `[World] DistanceKey` in lfsp.ini, Vorgabe `INSERT`):** Die Taste schaltet ein Fadenkreuz an der Mausposition ein, daneben die Entfernung in Metern (Sieben-Segment-Ziffern per `ID3D11DeviceContext1::ClearView` auf den Back-Buffer, nach dem Blit). Gemessen wird wie der Tiefentest: In `Flush` prüft `ProbeBatch` jedes Dreieck mit Tiefentest, ob es den Punkt unter der Maus (in Spiel-Pixeln) enthält, und nimmt das größte interpolierte `rhw`; Anzeige `1 / rhw` = Tiefe entlang der Blickrichtung in Spiel-Metern (dieselben Einheiten wie Sichtweite und B20-Messung). Am Bildrand etwas kleiner als der direkte Abstand.

**Vorgaben (👤 2026-10-18):** `TrackOcclusionRange = 220` (Loch bleibt zu, nahe Teile erscheinen rechtzeitig), `TrackOcclusionEdge = 1.0`, `TrackOcclusionEdgeRange = 300`; übernommen in Code-Vorgabe und INI-Vorlage (wirkt ab dem nächsten LFSP-Build; die INI setzt den Wert ohnehin).

## 18. B21 – Loch im Boden nahe vor dem Auto (gemeldet 2026-10-17)

**Symptom** (`screenshots/b21_ground_hole_near.webp`): Alpen-Strecke, Rechtskurve mit Zaun; hinter dem Zaun auf der Wiese, mittig im Bild und nicht weit entfernt, fehlt ein Streifen Boden.

**Stand:** tritt mit und ohne LFSP-Strecken-Patches auf (👤-Gegenprobe `TrackDetail = 0`), auch bei DDS = 1 (👤 2026-10-17). Offen: verschwindet es beim Näherkommen? Tritt es im Spiel ohne Mod auf?

**Gegenproben (👤 2026-10-17):** bleibt mit `AlphaEdgeFix = 0` und mit `HorPlus = 0` (ohne Hor+-Hooks `WorldCullVolume`/`SkyCullPlanes`). `ForceTwoSided = 1` ist aktiv → keine verworfenen Rückseiten. Ohne ASI-Mods (`dinput.dll` umbenannt, dx11z weiter als Spiel-Treiber aktiv): **Loch bleibt** (👤 2026-10-17) → Fehler des Spiels bzw. der Streckendaten, nicht der ASI-Mods. dx11z ist ebenfalls ausgeschlossen: Das Loch ist in etwa 6 Jahre alten Videos zu sehen, vor dx11z (👤 2026-10-17). **B21 ist ein Fehler des Originalspiels.**

**Analyse 2026-10-17 (`FUN_00450de0`, Objekt-Schleife `0x450EEA–0x450FCF`):** Je Abschnitt werden die Objekte (Kennung 5) im Bereich der Detailstufe (`+4`/`+6` des Stufen-Eintrags) durchlaufen. In eine Zeichen-Warteschlange kommen nur Objekte mit Flag `0x20000` (`FUN_004668f0`), `0x10000` (`FUN_00466a90`), `0x100000` (`FUN_004669c0`) oder `0x40000` **und** Detail-Schalter `0x6197E6` (`FUN_00466a90`). Alle übrigen verwirft das Spiel ohne Zeichnen. `0x6197E6` setzt `FUN_0044ed10`: 1 nur bei Grafik-Detailstufe `0x657D6C` ≥ 2 und Treiber-Kennung (`About+0x6C`, `0x6197B0`) ≠ 0xE; dx11z meldet 0x10. Hypothese H-B21a: Das fehlende Bodenstück ist ein solches verworfenes Objekt.

**Diagnose LFSP v6t, `[World] TrackDiag`:** Sprung bei `0x450F81` (16 Byte bis `0x450F91`) nach `TrackFlagDetour` → `TrackSkipCheck`. Stufe 1: jede neue Flag-Kombination verworfener Objekte einmal ins Log; Taste `TrackDiagKey` (G): für 100 ms alle verworfenen Objekte im Umkreis 200 der Kamera mit Flags, Position und Abstand. Stufe 2: zusätzlich zeichnen (deckende Warteschlange) → verschwindet das Loch, ist H-B21a bestätigt.

**Ergebnis v6t/v6v (👤 2026-10-17):** Loch bleibt mit `TrackDiag = 1` und `2`; Patch laut Log installiert, aber **keine einzige `B21:`-Zeile** → kein Strecken-Objekt läuft in den Verwerfen-Zweig. **H-B21a widerlegt.**

**Nächste Messung LFSP v6w:** `0x450E86–0x450E8C` springt bei `TrackDiag ≥ 1` oder `TrackDetail = 3` nach `TrackLevelDetour` → `TrackLevelSelect(Kopf, Stufe des Spiels, Entfernung²)`. Taste G: für 100 ms je Abschnitt im Umkreis 150 Kopf-Flags, Abstand, Stufenzahl, gewählte Stufe und je Stufe Objekt-/Mesh-Byte, Id-Bereich `+4..+6` und Reichweite `+0xA`. Neu `TrackDetail = 3`: immer gröbste Stufe. Hypothese H-B21b: Das Bodenstück steckt nur in einer gröberen Stufe (Id-Bereiche der Stufen nicht deckungsgleich) → mit `TrackDetail = 3` wäre das Loch zu.

**👤 Klarstellung:** Das Loch verschwindet beim Näherkommen → entfernungsabhängig. Da `TrackDetail = 1` die feinste Stufe erzwingt, ist die wahrscheinlichste Ursache die Reichweiten-Grenze je Abschnitt (wie das Dreieck in B19, H-B19b), die mit `TrackRange = 1.5` dort noch nicht reicht (H-B21c). Test: `TrackDetail = 2` an der Stelle; mit Taste G erscheinen dann auch abgeschnittene Abschnitte (Stufe des Spiels = Stufenzahl), Abstand und Reichweite zeigen den nötigen Faktor.

**Ergebnis v6w, `TrackDetail = 2` (👤 2026-10-17, `screenshots/b21_approach_*.png`):** Loch unverändert; es schließt sich beim Heranfahren **stückweise** immer weiter. Messung (Kamera 414.8 41.8 −72.8): ~45 Abschnitte im Umkreis 150, alle mit der Stufe des Spiels innerhalb ihrer Reichweite (keiner abgeschnitten), gezeichnet Stufe 0. Stufen-Tabellen: Abschnitte mit 1 Stufe (Reichweite 10000), 2 Stufen (150/500 bzw. 150/65535) oder 3 Stufen (130/600/65535, Kopf-Flag 0x10000); Ids je Stufe 0..4095, 4096..8191, 8192..12287. **H-B21c widerlegt.**

**Deutung (H-B21d):** Kein Abschnitt und kein Objekt fehlt; das allmähliche Schließen beim Näherkommen passt zu einer **echten Lücke im Strecken-Mesh** (zwei Flächen treffen nicht aufeinander, z. B. Stufe zwischen Wiese und Bankett), die nur unter flachem Blickwinkel durchscheint. Behebung nur über die Geometrie: Streckendatei bearbeiten oder Vertices nach dem Laden im Speicher verschieben; beides braucht das Streckenformat (Abschnitt/Objekt → Vertex-Daten) und die genaue Stelle. Aufwand hoch für ein kleines Artefakt.

INI wieder `TrackDetail = 1`, `TrackDiag = 0`.

**👤:** Die Lücke verschwindet ganz, wenn man nah genug ist. **Neue Hypothese H-B21e:** Der Bodenstreifen ist eine alpha-getestete Textur (ausgeschnittene Kante Wiese/Bankett). In den kleineren Mip-Stufen wird Alpha gemittelt und fällt unter die Alpha-Referenz → die Kante wird in der Ferne durchsichtig und schließt sich beim Näherkommen schrittweise (passt zum stückweisen Schließen; Befund zu kleinen Mip-Stufen schon in §9.1). Das trat auch im Original auf, weil das Spiel dieselben Mip-Stufen nutzt.

**Test dx11z (Stand 11.5d + B21), `[Dx11] AlphaMipScale`:** Im Pixel-Shader wird bei aktivem Alpha-Test das Alpha von Textur 0 mit `1 + Mip-Stufe × AlphaMipScale` multipliziert (Mip-Stufe aus `ddx/ddy` der Texel-Koordinaten); `g_Debug.w`, kein neues cbuffer-Feld. 0 = aus. Test mit 0.5. Schließt sich das Loch auf Entfernung → H-B21e bestätigt, Feinabstimmung des Faktors (Nebenwirkung: weiche Alpha-Kanten in der Ferne etwas kräftiger).

**Ergebnis (👤 2026-10-17):** Mit `AlphaMipScale = 0.5` und `1.0` ist das Loch unverändert groß, die Entfernung, ab der es sich schließt, ebenfalls; sonst unauffällig. **H-B21e (Alpha-Test in kleinen Mip-Stufen) widerlegt** (gilt nur für alpha-getestete Draws).

**Nächste Tests (dx11z, `[Dx11]`, je einzeln):** `TestNoMips = 1` (Sampler `MaxLOD = 0`, nur oberste Mip-Stufe für alle Texturen; erfasst auch geblendete Draws) und `TestVertexAlpha = 1` (Vertex-Alpha im Pixel-Shader = 1; prüft H-B21f: das Spiel blendet ein Bodenstück per Vertex-Alpha nach Entfernung ein, darunter fehlt die Grundfläche). Neues cbuffer-Feld `g_Test` am Ende (Offset 2448).

**Ergebnis (👤 2026-10-18):** `TestNoMips = 1` und `TestVertexAlpha = 1` ändern nichts → weder Mip-Stufen (auch geblendet) noch Vertex-Alpha. 

**Nächster Test, `TestNoZBias = 1`:** Hypothese H-B21g: Eine weiter entfernte Fläche wird mit Z-Bias des Spiels (State 0x18/0x66, dx11z: konstant `v / 32768` auf z) gezeichnet. Ein konstanter Versatz in z entspricht in der Ferne einer großen Strecke in Weltkoordinaten; die Fläche kommt dann vor das nähere Bodenstück und überdeckt es. Beim Näherkommen schrumpft der Weltversatz, das Loch schließt sich schrittweise. D3D7-ZBIAS wirkt ebenso konstant → auch im Original. dx11z protokolliert je neuem Bias-Wert `Depth bias … first used`.


**Ergebnis (👤 2026-10-18):** Mit `TestNoZBias = 1` ist die Lücke unverändert; dafür flackert der Schatten unter dem Auto, und die Lichtflecken auf Texturen werden teils schwarz (die Versätze werden also gebraucht). Das Spiel nutzt Versätze von 5 bis −76 (`dx11z.log`, State 0x18). **H-B21g widerlegt.** `TestNoZBias` wieder 0.

**Bildbefund (`b21_ground_hole_near.webp`):** In der Lücke ist kein Himmel zu sehen, sondern weiter hinten liegendes Gelände (Felsen, Zaun, Straße). Entweder fehlt die nähere Wiese, oder weiter entferntes Gelände wird ohne Tiefentest darübergezeichnet (H-B21h). Beides schließt sich beim Näherkommen schrittweise.

**Nächster Test dx11z (B21-TestDraw), `[Dx11] TestDraw`:** `1` = deckende Dreiecke (ohne Blending) immer mit Tiefentest `LESSEQUAL` und Tiefen-Schreiben; `2` = alles als Drahtgitter. In beiden Stufen protokolliert dx11z jede Textur, die deckend ohne vollen Tiefentest gezeichnet wird, einmal (`B21 opaque draw without depth`). Schließt sich die Lücke mit `1` → H-B21h bestätigt. Mit `2` zeigt das Gitter, ob an der Stelle Dreiecke der Wiese liegen oder ein Spalt im Mesh ist (H-B21d).

**Ergebnis (👤 2026-10-18):** `TestDraw = 1` ändert nichts → kein deckendes Gelände ohne Tiefentest; **H-B21h widerlegt**. Im Log nur ein Draw ohne Tiefen-Schreiben (Textur 0, 134 Dreiecke, `zfunc 8`, vermutlich Himmel/Overlay). Mit `TestDraw = 2` (Drahtgitter) ist die Stelle weiterhin leer (`screenshots/b21_wire_*.png`).

**Analyse 2026-10-18, EAGL-Pipeline (statisch gelinkt in `Porsche.exe`):**
- `FUN_005385b0` transformiert in den Kamera-Raum (Sichtfeld `|x|, |y| ≤ z`, Modus `0x20B`), die Clip-Codes rechnet die Routine `0x538350` (Tabelle `0x5DE9CC`): `0x01` z < near, `0x02` z > far, `0x04/0x08` y, `0x10/0x20` x; Byte 1 = strenges Sichtfeld, Byte 0 = Guard-Band (`0x5DEA60`, Exponent-Versatz) plus near/far.
- `FUN_005408f0` verwirft ein Mesh nur, wenn alle Vertices hinter derselben Ebene liegen (UND der Codes `& 0x3C03`), je Dreieck ebenso (`0x540A48…0x540B53`). Nachgerechnet (Simulation der Bit-Routine): auch Vertices hinter der Kamera bekommen die richtigen Ebenen-Codes → diese Verwerfung ist korrekt.
- Dreiecke mit ODER-Code `& 0x3F ≠ 0` gehen an den Clipper `FUN_0054c030` bzw. `FUN_005498b0` (cdecl, 14 Argumente, je ~10 KB); liefert er < 3 Ecken, fällt das Dreieck weg. Das ist die letzte Stelle, an der die CPU Dreiecke verwirft.
- Guard-Band: `FUN_00549220` fragt den Treiber (`setstate 0x3F`); dx11z meldet keins → Guard 0, jedes Dreieck über den Bildrand wird auf der CPU geschnitten. Große Wiesen-Dreiecke reichen fast immer über den Rand.

**Hypothese H-B21i:** Der Clipper verliert große Dreiecke, die mehrere Ränder (oder near) schneiden; je nach Kamera-Lage betrifft das andere Dreiecke → das Loch schließt sich stückweise beim Näherkommen.

**Messung LFSP v6z, `[World] ClipDiag = 1`:** Inline-Hook an beiden Clippern; nach dem Original schneidet LFSP das Dreieck selbst gegen near/far und `|x|, |y| ≤ z` und vergleicht die sichtbare Fläche in `(x/z, y/z)`. Ist die eigene Fläche > 1e-4 und die des Spiels kleiner als die Hälfte → `B21 Clip…: Dreieck verloren` mit Codes, Guard, near/far, Ecken; alle 2 s eine Summe. Die ersten 40 Fälle immer, Taste G: 100 ms lang alle.

**Ergebnis v6z (👤 2026-10-18):** Clipper läuft (Taste G registriert), aber **keine einzige `Dreieck verloren`-Zeile** → der Clipper verliert nichts Sichtbares. **H-B21i widerlegt.**

**Analyse Objektauswahl 2026-10-18 (`FUN_0047fb40` → `FUN_0047f550`, `FUN_0047f890`):**
- `FUN_0047f550` baut aus Sichtweite und halbem FOV ein 2D-Sichtfeld, umschließt es mit einem Kreis und markiert per `FUN_004873b0` die **Verdeckungswände** der Karte (`map+0x2C`, je 0x1C Byte: Punkt `+4/+8`, Richtung `+0xC/+0x10`, Byte 0 = Art) im Kreis als aktiv.
- `FUN_0047f890` prüft je Objekt der Abschnitte: Kugel gegen EAGL-Sichtfeld (`FUN_00539480`), dann **`FUN_00488b50`**: Linie Objektmitte → Kamera in der Draufsicht (x, z) gegen jede aktive Wand (`FUN_00489cc0`, Strecken-Schnitt); kreuzt sie eine, **fällt das ganze Objekt weg** (`0x47F9AE TEST AL,AL; JNZ 0x47FAD1`). Danach Entfernung − Radius gegen die Sichtweite.
- Der Test kennt keine Höhe. Liegt ein Wiesen-Objekt hinter einer Wand (z. B. Hang in der Kehre), die man von der Straße aus überblickt, fehlt es trotzdem; da nur die Objektmitte zählt, taucht ein Objekt nach dem anderen auf, sobald die Sichtlinie die Wand nicht mehr kreuzt → **H-B21j**, passt zu allen Befunden (Spiel-Logik, unabhängig von Treiber, LOD, Mods; stückweises Schließen; G-Messung sah nur schon ausgewählte Objekte).

**Fix-Test LFSP v7a, `[World] TrackOcclusion = 0`:** `0x47F9AE` `84 C0 0F 85 1B 01 00 00` → `84 C0 90×6`; der Verdeckungstest läuft weiter, sein Ergebnis zählt nicht. Verdecktes zeichnet die GPU dann über den Tiefenpuffer korrekt weg; Kosten: mehr Objekte je Bild (auf heutiger Hardware unkritisch). `1` = Original.

**Ergebnis v7a (👤 2026-10-18):** Mit `TrackOcclusion = 0` ist **das Loch verschwunden** (`screenshots/b21_fixed_noocclusion.png`). **H-B21j bestätigt: Ursache ist der 2D-Verdeckungstest `FUN_00488b50`.** Nebenwirkung: Weit entfernte Streckenteile, die bisher hinter den Wänden aussortiert wurden, erscheinen jetzt ohne das Gelände drumherum, schwebend vor dem Berg-Hintergrund (`screenshots/b21_noocclusion_far_track.webp`, verwandt mit B20). Die Wände erfüllen für die Ferne also einen Zweck.

**Verfeinerung (LFSP v7b, geplant):** Der Verdeckungstest bleibt für ferne Objekte und gilt nur in der Nähe nicht mehr: `TrackOcclusionRange` (m), Objekte mit Abstand − Radius darunter werden trotz Wand gezeichnet.

**LFSP v7b:** `[World] TrackOcclusionRange` (Vorgabe 250 m). Mit `TrackOcclusion = 0` springt `0x47F9AE` nach `OcclusionDetour` → `OcclusionReject(Ergebnis, Kugel [ESP+0x3C], Kamera [ESP+0x14])`: verworfen wird nur noch, wenn die Wand die Sicht kreuzt **und** Abstand − Radius ≥ Reichweite. `0` = Test nirgends (wie v7a). Enthält außerdem die geänderten Vorgaben der Rücklicht-Sitzung (`CockpitFov` 90, `HeadlightsHighFade` 100).

**Ergebnis v7b (👤 2026-10-18):** Mit `TrackOcclusionRange = 150` bleibt das Loch zu → Vorgabe 150 (INI-Vorlage, installierte INI, Code-Vorgabe; Code-Vorgabe wirkt ab dem nächsten Build). Der schwebende ferne Streckenteil bleibt aber selbst mit 100 m sichtbar; dort gilt der Test also wie im Original. Offen: Ist es derselbe Teil wie B20 (auch ohne Mods sichtbar)? Gegenprobe `TrackOcclusion = 1` an derselben Stelle.

**👤 Gegenprobe:** Der schwebende Teil erscheint auch mit `TrackOcclusion = 1` → **B20 ist unabhängig vom B21-Fix**.

**B20-Messung LFSP v7c (`TrackDiag = 1`, Taste G):** In `OcclusionReject` (läuft für jedes Objekt nach dem Sichtfeld-Test) protokolliert LFSP 100 ms lang alle Objekte ab 200 m innerhalb ±30° der Blickrichtung: Eintrag, Flags, Id, Kugel, Abstand, Winkel zur Blickrichtung, Wand-Ergebnis und Entscheidung. Erwartung: Der schwebende Teil kommt ohne Wand durch („gezeichnet“), das Gelände drumherum fällt an einer Wand weg (oder kommt gar nicht erst bis hierher). Danach folgt noch die Entfernungsprüfung (Objekte mit Flag `0x400000` bis 600 m, `0x20` bis 700 m, sonst Abstand − Radius < Sichtweite).

## 19. B22 – Reifenspuren teils unsichtbar (gemeldet 2026-10-22)

**Bild (👤):** Hinter dem Hinterrad endet die Spur an einer gezackten Kante aus geraden Stücken; oberhalb der Kante ist sie vollständig, darunter fehlt sie. Tritt auch auf Original-Strecken auf.

**Deutung:** Die Fahrbahn verdeckt die Spur entlang ihrer Dreieckskanten, die Spur liegt also etwas unter dem gezeichneten LOD 0. Die Simulationsrate scheidet aus (sie würde Lücken in Fahrtrichtung erzeugen, keine geraden Schnittkanten).

**Messung (`SICHTWEITE.md` §2d):** Die Wegpunkte `SimD` der Physik haben keine Querneigung und liegen außerhalb der Fahrbahnmitte bis über 1 m neben LOD 0; LOD 1/2 der Straße weichen um einige cm bis Dezimeter ab. Welche Fläche die Spur-Höhe liefert, ist noch nicht im Code belegt.

**Weg:** Physik angleichen geht nur grob (Ebene je 6 m, KI hängt daran). Vorschlag: Spur-Draws in dx11z mit Tiefen-Bias zeichnen oder LFSP hebt die Spur-Vertices an. Offen: Zeichenstelle der Spuren finden.

### 19.1 Ghidra: Erzeugung und Höhe der Spuren (2026-10-22)

- **Textur:** `SKDP` in `GameData/Render/common.fsh`; Tabelle `0x5D00E8` (16 × `SKDP`, Index = Untergrund `& 0xF`).
- **Aufruf je Bild:** `FUN_004a2880(auto)` ruft für die 4 Räder (Schritt 0xC4) `FUN_00478f30(spur, auto+0x7F8+i·0xC4, Schlupf, Untergrund, auto+0x348, …)`. Nur bei Detailstufe ≥ 2 (`DAT_006197e7`, Detailschalter `DAT_00657d6c`, Export `Porsche.exe.c` Zeile 60680).
- **Radpunkt:** `FUN_00496860` setzt `auto+0x7F8`/`+0x8BC`/`+0x980`/`+0xA44` = Autoposition `+0x330` ± Achsen `+0x364`/`+0x370`/`+0x37C` × halbe Maße (`+0x3A8`/`+0x3AC`/`+0x3B0`). Das sind die Ecken des Fahrzeugkastens auf Bodenhöhe, **kein abgetasteter Radkontakt**. Die Bodenebene am Auto kommt aus `FUN_00474060` → `FUN_00473240` (Raumbaum `DAT_00628960+0x24`, Flächenstück mit kleinstem Höhenabstand) und der Normalen `FUN_00474420`.
- **Spur-Vertices:** `FUN_00479190` setzt je Punkt zwei Ecken = Radpunkt ± Breite (`spur+0`) quer zur Fahrtrichtung `auto+0x348`, **beide mit derselben Höhe** (Radpunkt y). Die Spur ist quer also immer waagrecht und folgt keiner Querneigung.
- **Abstand der Punkte:** Ein neuer Punkt nur, wenn `Zähler & DAT_006197ec` = 0 (Stufe 2/3: Maske 7 → jeder 8. Aufruf; Stufe 0/1: 15). Dazwischen läuft die Spur als gerades Band. Bei 100 km/h und 8 Aufrufen à 1/60 s ≈ 3,7 m; über eine gewölbte Fahrbahn schneidet das Mesh in der Mitte durch.
- **Ablage/Zeichnen:** Verwaltung `FUN_00479900` (Vtable `0x5B4750`, einer der sechs Sortierer aus `SICHTWEITE.md` §2c, `DAT_00628b70`); Puffer je Streckenabschnitt (Satz 0x50 Byte, `+0x38` Liste à 20 Spuren, `FUN_00479140`, Abschnitt über `FUN_0047bad0`). Gezeichnet wird mit der Szenerie dieses Sortierers; die genaue Draw-Funktion ist noch nicht verfolgt.

**Ursachen der Verdeckung (alle drei wirken zusammen):**
1. Höhe aus der Kasten-Ebene des Autos, nicht aus dem Mesh unter dem Rad (Federweg, Neigung des Autos, Ebene des Flächenstücks).
2. Spur quer waagrecht, Fahrbahn quer geneigt/gewölbt.
3. Gerade Stücke über mehrere Meter (jeder 8. Aufruf) über eine gekrümmte Fahrbahn.

**Simulationsrate:** Punkt 3 hängt an der Aufrufrate: Läuft `FUN_004a2880` öfter, werden die Stücke kürzer und die Spur folgt der Fahrbahn besser. Sie ist also beteiligt, aber nicht die Hauptursache.

**Fix-Wege:** (a) dx11z: Draws mit Textur `SKDP` mit Tiefen-Bias zeichnen (einfach, wirkt für alle drei Ursachen bis zum Bias-Betrag); (b) LFSP: in `FUN_00479190` die Höhe je Ecke aus dem Mesh holen oder anheben; (c) Maske `DAT_006197ec` kleiner (z. B. 1 oder 3) → kürzere Stücke (mehr Vertices).

### 19.2 Fix dx11z: Tiefen-Bias für Reifenspuren (2026-10-22)

- Zeichenstelle: Tabelle `0x5B3D28` (Klasse aus `FUN_004513d0`, nur für die Spurverwaltung angelegt), Eintrag 2 `0x5B3D30` = `0x451420` → `FUN_0047a060` → je Spur `FUN_00478aa0`. Gleicher Aufbau wie der Strecken-Draw `0x5B3CF8`.
- dx11z ersetzt den Eintrag (`SkidDrawHook`, prüft vorher den Originalwert) und verschiebt in `PushVertex` die Tiefe aller Vertices in diesem Aufruf: `z' = z − b·(1 − z)`. Bei üblicher Projektion ist `1 − z` ≈ Near/Abstand, die Spur rückt also um den Anteil `b` ihres Abstands zur Kamera nach vorn (b = 0,003: 10 m → 3 cm, 50 m → 15 cm). Gilt auch für Strips/Fans, die den Spiel-Bias nicht bekommen.
- Einstellung `[World] SkidDepthBias` in `lfsp.ini` (Vorgabe 0.003, 0 = aus, max 0.1). Log: `Config SkidDepthBias …`, `Skid hook installed at 005B3D30 …`.
- Build `nfs5_enhanced_LHedit/drivers/dx11/build/dx11z.dll` (MD5 `ef350018…`), installiert 2026-10-22 in `Game CD/drivers/` (alte DLL: `drivers/_backup/dx11z.dll.vor-b22.dll`, MD5 `b1a09b00…`), `SkidDepthBias = 0.003` in der installierten `lfsp.ini`. **👤-Test (2026-10-22) ✅:** sieht korrekt aus; nur noch in seltenen Situationen verdeckt.

## 20. B24 – Rückspiegel, Minimap und ESC-Menü im Replay (gemeldet 2026-10-24)

**Bild:** Rückspiegel hellblau, Minimap fehlt, ESC-Menü in der Replay-Kamera blau; trat vorher gelegentlich auf, zuletzt dauerhaft, auf allen Strecken.

- **Ursache:** `[World] GuardBand = 1` (LFSP, B15). Damit verwirft das Spiel Meshes und Dreiecke erst hinter dem Guard-Band statt am Bildrand, in **jeder** Ansicht – auch in Rückspiegel, Minimap und Replay. Ausschlusstests 2026-10-24/25: dx11z bis zum Stand vom 19.10., nfs5_enhanced.asi, lfsp.asi v8f, Partikel-Dateien, Standard-`nfs5_enhanced.ini`, HUD-Dateien, neues Profil – Fehler jeweils weiter da; mit `GuardBand = 0` weg, mit `CullRadiusScale = 2.0` allein ebenfalls weg.
- **Behebung:** `GuardBand` standardmäßig 0 (LFSP, dx11z, `config/lfsp.ini`). Die Wasserspiegelung braucht es nicht mehr (Himmels-Ersatz am Bildrand). 👤-Test ✅.
- **Folgefehler Rand im Rückspiegel:** danach eine Pixelspalte bzw. -zeile in Löschfarbe am Spiegelrand. Das Spiel schneidet die Polygone eines Teilbildes etwa einen Spielpixel innerhalb seines Clip-Rechtecks, dx11z löscht das ganze Rechteck; der halbe Pixel Versatz (D3D7-Pixelmitten) wurde in Spielpixeln gerechnet und bei größerer Renderauflösung zu einem ganzen Renderpixel. dx11z: Versatz 0 (Clip-Kanten liegen auf Renderpixel-Kanten) und Ecken bis 1 Spielpixel vor einer Clip-Kante auf die Kante (`g_Clip` in den Draw-Konstanten, `MainVS`). 👤-Test ✅.
