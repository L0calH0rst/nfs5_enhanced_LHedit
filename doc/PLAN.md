# PLAN.md – Need for Speed: Porsche Unleashed (NFS5, 2000)

**Stand:** 2026-10-22 · **Workspace:** `G:\NFSP` (Code und Doku im Repo `nfs5_enhanced_LHedit`, Ablage `knowledge.md` §0) · **Ziel:** das Originalspiel modernisieren (Weg W1): stabil unter Windows 11, HD-Auflösung, HD-Texturen.

## Dokumentenstruktur

| Dokument | Inhalt |
|---|---|
| **`PLAN.md`** (dieses) | nur **Status, Aufgaben, Entscheidungen, offene Punkte**; Herleitungen nur als Verweis |
| **`knowledge.md`** | kurze, belegte Fakten (eine Zeile, mit Quelle) |
| Themen-Dokumente | Herleitungen, Messungen, Spezifikationen: `BUGS.md` (Grafikfehler), `SHPI.md` (Container/Texturformat), `FSH-REPACK.md` (Repack + HD-Strategie), `INI-formats.md`, `MEDIA.md`, `MOD.md` (nfs5_enhanced + eigene dx11z-Patches), `FENSTER-VERDECK.md` (Phase 8), `LICHT.md` (Phase 9), `KAMERA.md` (Phase 11.1/11.2), `MENU.md` (Phase 10), `VERKEHR.md` (Phase 12), `GHIDRA.md` (Funktionskarten), `TOOLS.md` (Werkzeuge, Verzeichnisse) |

**Regeln:** Ein Ergebnis wird im Themen-Dokument beschrieben, hier nur abgehakt und verlinkt. Ändert sich ein Fakt, zuerst das Themen-Dokument anpassen, dann `knowledge.md`. Verlauf steht in der Git-Historie, nicht in Update-Blöcken.

**Legende:** 🤖 KI · 👤 Nutzer · ✅ erledigt · 🔄 in Arbeit · ⏳ offen · ⏸ zurückgestellt · ❌ verworfen/entfällt

---

## 1. Entscheidungen

| Datum | Entscheidung | Begründung |
|---|---|---|
| 2026-10-13 | **W1:** Originalspiel modernisieren, `nfs5_enhanced` als Basis. W2 (OpenNFS-NFS5-Loader) nur optional (Phase 5) | W2 wäre Neuentwicklung, NFS5 ist in OpenNFS zu 0 % umgesetzt |
| 2026-10-13 | Bug-Fixes vor HD-Texturen | Artefakte hätten die HD-Prüfung blockiert; seit Gate 3T blockieren B1–B3/B5/B6 Phase 2 nicht mehr |
| 2026-10-16 | **F:** HD-Texturen per FSH-Repack, nicht per Laufzeit-Ersatz in dx11z | `FSH-REPACK.md` §10 |
| 2026-10-16 | B4-Fix: `AlphaEdgeFix = 2` (Zwei-Pass); Variante 1 verworfen | `BUGS.md` §8.7–8.9 |
| 2026-10-16 | Dokumentenstruktur wie oben | Status-Drift zwischen mehreren Dokumenten |
| 2026-10-22 | Mod-Code (ASI, dx11z, LFSP) und Doku nur noch im Repo `nfs5_enhanced_LHedit` (`doc/`); `G:\NFSP` (NFSP-M) wird nicht mehr committet | doppelte Kopien liefen auseinander (`knowledge.md` §0) |
| 2026-10-16 | Gelände-Unterteilung (wie Skyrim „Smooth Terrain“) als optionale Phase 7, zuerst Machbarkeitsprüfung; **nicht** in dx11z | dx11z erhält nur transformierte, vorbeleuchtete Vertices ohne Normalen und Nachbarschaft (`knowledge.md` §3) |

## 2. Arbeitsweise

- 🤖 hat kein Spiel, keine GPU und keine GUI. 👤 ist Testfahrer und liefert Screenshots und Logs.
- 👤 trifft Kunst- und Designentscheidungen (Zielgröße, Stil, Prioritäten) und installiert Patches.
- 🤖 liefert zu jeder Nutzer-Aufgabe konkrete Schritte, das erwartete Ergebnis und was zurückgemeldet werden soll.
- Eine Aufgabe ist erst erledigt, wenn ihr Abnahmekriterium bestätigt ist.

---

## 3. Phasen und Aufgaben

**Reihenfolge:** Gate 0 → Phase 1 → Block 3T ∥ Phase 3C → Phase 2 → Phase 3A/3B → Phasen 4–7 (optional).

### Phase 0 – Setup ✅ (Gate 0 bestanden 2026-10-13)

| ID | Wer | Aufgabe | Status / Ergebnis |
|---|---|---|---|
| 0.1 | 👤 | W1 oder W2 wählen | ✅ W1 |
| 0.2 | 🤖 | Arbeitsverzeichnis `work/` + Tool-Katalog | ✅ `TOOLS.md` |
| 0.3 | 👤 | Mod-Stack installieren (`nfs5_enhanced` 1.1.0, ASI Loader, `dx11z.dll`) | ✅ `MOD.md` §2 |
| 0.4 | 👤 | Spiel starten, eine Runde fahren, Log liefern | ✅ läuft stabil unter Win11 |
| 0.5 | 🤖 | Bei Problemen Log auswerten | ✅ nicht nötig |

### Phase 1 – Format-RE und Doku ✅ (Gate 1 durch 1.6 belegt, 2026-10-14)

| ID | Wer | Aufgabe | Status / Ergebnis |
|---|---|---|---|
| 1.1 | 🤖 | SHPI-Header spezifizieren | ✅ `SHPI.md` |
| 1.2 | 🤖 | `fshtool` benutzbar machen, `.fsh` nach BMP extrahieren (inkl. Alpha) | ✅ `FSH-REPACK.md` |
| 1.3 | 👤 | Extrahierte Texturen im Spiel wiedererkennen | ✅ durch 1.6 belegt (bearbeitete coastal-Texturen im Spiel sichtbar) |
| 1.4 | 🤖 | INI-Textformate dokumentieren | ✅ `INI-formats.md` |
| 1.5 | 🤖 | Medienformate `.viv`/`.mad` kurz spezifizieren | ✅ `MEDIA.md` |
| 1.6 | 🤖+👤 | fshtool-Round-Trip inkl. Test im Spiel (Gate für Phase 2) | ✅ `FSH-REPACK.md`; offen: veränderter Alpha-Inhalt → 2.0 |

### Block 3T – Textur-Pfad: B4-Fix ✅ (Gate 3T bestanden 2026-10-16)

| ID | Wer | Aufgabe | Status / Ergebnis |
|---|---|---|---|
| 3T.0 | 👤 | HD-Strategie R oder F | ✅ F (`FSH-REPACK.md` §10) |
| 3T.1 | 🤖+👤 | Messbuild (Draw-States, Texturformate) + Testfahrt | ✅ H-B4c bestätigt (`BUGS.md` §8.5) |
| 3T.2 | 🤖 | B4-Fix in dx11z (`AlphaEdgeFix`) | ✅ `BUGS.md` §8.6, `MOD.md` §6 |
| 3T.3 | 👤 | Varianten im Spiel vergleichen | ✅ Variante 2 gut, Variante 1 ❌ (`BUGS.md` §8.7–8.9) |
| 3T.4 | 🤖 | Edge-Padding transparenter Texel beim Upload | ⏸ kein Rest-Saum sichtbar; wieder aufgreifen, falls HD-Texturen helle Kanten zeigen |
| 3T.5/3T.6 | – | Laufzeit-Textur-Ersatz | ❌ entfällt (Entscheidung F) |

### Phase 2 – HD-Texturen (Kern) ⏳

Voraussetzungen erfüllt (Gate 1, Gate 3T). Weg: FSH-Repack mit `fshtool`.

| ID | Wer | Aufgabe | Ergebnis | Abnahme | Status |
|---|---|---|---|---|---|
| **2.0** | 🤖+👤 | **Größengrenze prüfen:** eine Strecken-Textur mit Alpha als FSH-Typ `0x7D` in 512×512 mit voller Mip-Kette und **verändertem Alpha** repacken; mit `Measure = 1` fahren | `FSH-REPACK.md` §11 | belegt, ob > 256² geht (`talloc`-Größe, Absturz, Verkleinerung) und ob der neue Alpha-Inhalt ankommt. Bei harter Grenze entscheidet 👤: Grenze per Hook aufheben oder HD auf 256² beschränken | ✅ Teil A: 512×512 wird genutzt (§11.5). ✅ Teil B: `RaceColorDepth` ohne Wirkung; Strecke `7D` → 8888, Fahrzeuge `7B` → 4444 (§11.3, §11.6). Offen: veränderter Alpha-Inhalt |
| **2.0b** | 🤖+👤 | **Speicherbedarf messen, bevor ein Memory-Patch erwogen wird:** dx11z-Messlog um Prozess-Speicher (virtueller Adressraum, Private Bytes) je Bericht erweitern; Fahrt mit Original-Satz vs. Strecke mit allen Texturen verdoppelt | Messwerte in `FSH-REPACK.md` §12 | Entscheidung LAA-Flag ja/nein auf Basis der Zahlen | ✅ Lauf 1: 1047 MB Adressraum, Texturen 26–30 MB. Lauf 2 (coastal komplett HD, Faktor 4): 1176 von 2048 MB, Texturen 225 MB → kein LAA nötig; für Faktor 8 LAA vorsehen (`FSH-REPACK.md` §12.1) |
| 2.1 | 👤 | Kunstentscheidung: Faktor, Methode (Nearest / KI-Upscaler), Reihenfolge (Strecke, Auto, HUD) | `HD-TEXTUREN.md` | Spezifikation liegt vor | 🔄 Methode chaiNNer, Faktor 4 vorgegeben; Modelle je Kategorie offen |
| 2.2 | 🤖 | Pipeline BMP → Upscale → FSH; Alpha-Masken von Fahrzeugtexturen ohne Interpolation (`knowledge.md` §1); transparente Lücken an Atlas-Zellgrenzen (`BUGS.md` §9.4, B7) | `tools/hd/hd_pipeline.py`, `HD-TEXTUREN.md` | Rück-Extraktion korrekt | ✅ 2026-10-23 (Lanczos-Test, 186 Objekte); Eingaben für coastal liegen bereit |
| 2.3 | 👤 | Eine HD-Textur im Spiel prüfen | Screenshot + Rückmeldung | „sieht gut aus“ oder Problemliste | 🔄 1024er-street-Test läuft (Pool-Absturz behoben, `nfsp_texpool.asi`); Flimmern auch im Original; Test mit echtem Modell offen |
| 2.4 | 🤖 | Pipeline verfeinern und auf alle priorisierten Texturen anwenden | kompletter HD-Satz | Liste gepackt + Log | ⏳ |
| 2.5 | 👤 | HD-Satz im Spiel durchspielen | Freigabe | schriftliche Freigabe (= Gate 2) | ⏳ |

### Phase 3A/3B – HD-Auflösung und Win11-Feinschliff ⏳ (nach Gate 2)

Der Mod liefert Win11-Start, HD-Auflösung, HUD-Skalierung und No-CD bereits (`MOD.md` §3). Diese Phase ist Feinschliff und Prüfung.

| ID | Wer | Aufgabe | Ergebnis | Status |
|---|---|---|---|---|
| 3.1 | 👤 | Zielauflösung in `nfs5_enhanced.ini` festlegen | INI angepasst | ⏳ |
| 3.2 | 🤖 | Bei Problemen Parameter erklären (`FovMultiplier`, `ScaleMultiplier`, `Sharpness` …) | Setup-Vorschlag | ⏳ |
| 3.3 | 👤 | In HD durchspielen (HUD, Performance) | Rückmeldung + Screenshot | ⏳ |
| 3.4 | 🤖 | Nachjustieren, Leitfaden schreiben | `RESOLUTION.md` | ⏳ |
| 3.5 | 🤖 | Rest-Blockaden unter Win11 prüfen | `WIN11.md` | ⏳ |
| 3.6 | 👤 | Maßnahmen anwenden, Ergebnis melden | „läuft stabil“ | ⏳ |
| 3.7 | 🤖 | Nur bei Restproblemen: gezielte Binary-Analyse und Patch | Patch/Workaround | ⏳ |

### Phase 3C – Grafikfehler B1–B9 🔄

Beschreibung und Analyse je Bug: `BUGS.md` §0–§8. **Grundsatz:** dauerhaft beheben, nicht durch kleinere Sichtweite umgehen (Ziel: korrekt bei DDS 1.0–4.0).

| Bug | Kurz | Ursache (Stand) | Status |
|---|---|---|---|
| B1 | Boot-Rauch am falschen Ort | Engine-Tabellen kennen den DDS-Faktor nicht; Fix nur über `gimme.dll` (`BUGS.md` §2) | ⏳ |
| B2 | Map-Teil nicht geladen | dto. | ⏳ |
| B3 | Leuchtturm-Licht ohne Turm | dto. | ⏳ |
| B4 | Heller Saum an Alpha-Kanten | Originalfehler, Blend + Z-Write (`BUGS.md` §8) | ✅ behoben (3T) |
| B5 | Graue Dreiecke am Himmelsrand (linker/rechter Bildrand, in Kurven) | neue Hypothese H-B5w: Hor+ zeigt Bereiche, deren Himmelsdreiecke die Engine nach 4:3-Logik verwirft (`BUGS.md` §4.1) | ✅ behoben mit ASI-Hook `SkyCullPlanes` (`BUGS.md` §4.2) |
| B6 | Beschädigtes Auto flackert | vermutlich Engine-Culling mit statischer Bounding-Box (`BUGS.md` §5) | ⏳ |
| B7 | Rand an der Oberkante von Busch-Billboards (nur von unten sichtbar) | **bestätigt:** Wrap mischt an v = 0 die unterste Zeile ein (v reicht bis 1,25); Fix `ClampUnitUv = 2` (Kantenschutz) installiert (`BUGS.md` §9.4) | ✅ behoben (coastal) durch Asset-Fix: Alpha-Lücken an Atlas-Zellgrenzen (`atlas_gutter.py`); andere Strecken über HD-Pipeline 2.2 |
| B8 | Rückseite von Fensterläden fehlt | einseitige Flächen + Backface-Culling (`BUGS.md` §9.2) | ✅ mit `ForceTwoSided = 2` (2026-10-16) |
| B9 | Kaputtes Glas von innen unsichtbar | Engine verwirft Fahrzeug-Rückseiten vermutlich selbst; Fix nur in `gimme.dll` (`BUGS.md` §9.4) | ⏳ Engine-Analyse |
| B10 | Profilliste leer (unsichtbar gezeichnet) | **verursacht durch `AlphaEdgeFix = 2`**: zerstörte die Tiefenmaske des Menüs (`BUGS.md` §10) | ✅ behoben mit 3C.16 (2026-10-16) |
| B11 | Doppelseitige Schilder flackern mit `ForceTwoSided = 1` | Z-Fighting koplanarer Flächen; Fix `ForceTwoSided = 2` (`BUGS.md` §9.4) | ✅ kein Flackern mehr; Fix 3C.14 gegen den Randsaum installiert (in 3C.15 enthalten), Sichtprüfung offen |
| B12 | Farbiger Rand um Schild-Rückseiten mit `ForceTwoSided` | Rückseiten-Ausschnitt kleiner als Vorderseite; Fix 3C.20: Rückseiten nur für deckende Texturen (`BUGS.md` §9.5) | ✅ behoben mit 3C.20 (Schilder, Fensterläden, Busch ok, 2026-10-16) |
| B13 | Map-Chunks am Bildrand zu früh ausgeblendet | Chunk-Auswahl `0x47F550` nutzt das 4:3-Halb-FOV (`BUGS.md` §4.3) | ✅ behoben mit ASI-Hook `WorldCullVolume` (2026-10-16) |
| B19 | Risse in der Strecke aus der Ferne (Himmel scheint durch) | Detailstufe je Abschnitt nach Entfernung (`FUN_00450de0`), gröbere Stufen nicht dicht (`BUGS.md` §16) | ✅ kleine Risse behoben mit LFSP v6f `[World] TrackDetail = 1` (👤 2026-10-17); ✅ großes fehlendes Dreieck behoben mit `TrackDetail = 2` (👤, DDS = 1); bei DDS = 3 Aufbau wie im Original. Auffälligkeiten bei DDS = 4 → B2. Standard jetzt `TrackDetail = 1` + `TrackRange = 1.5` (LFSP v6j); schwebender Abschnitt und Bodenloch (Alpen) treten auch ohne Patch auf → B20/B21. Dreieck mit v6j-Standard weg (👤) → ✅ (`BUGS.md` §16) |
| B20 | Ferner Streckenabschnitt schwebt vor dem Panorama (Alpen) | Originalfehler: Sichtlinie zur Objektmitte geht durch eine Lücke zwischen zwei Verdeckungswänden, das Gelände daneben fällt weg (`BUGS.md` §17) | ✅ deutlich kleiner mit LFSP v7e `[World] TrackOcclusionEdge = 1.0`, `TrackOcclusionEdgeRange = 300` (Rand-Test für ferne Objekte); kleiner Rest bleibt (👤 2026-10-18) |
| B21 | Loch im Boden nahe vor dem Auto (Alpen) | Originalfehler: 2D-Verdeckungstest `FUN_00488b50` ohne Höhe verwirft sichtbare Wiesen-Objekte (`BUGS.md` §18) | ✅ behoben mit LFSP v7b `[World] TrackOcclusion = 0`, `TrackOcclusionRange = 220` (👤 2026-10-18) |
| B22 | Reifenspuren teils unsichtbar (gezackte Kante) | Spur liegt unter dem gezeichneten LOD 0, Fahrbahnquerschnitt weicht ab (`BUGS.md` §19, Messung `SICHTWEITE.md` §2d) | 🔄 Ursache im Code (§19.1): Spur-Höhe aus dem Fahrzeugkasten, quer waagrecht, gerade Stücke über jeden 8. Aufruf. Fix dx11z (§19.2): Hook der Spur-Zeichenstelle `0x5B3D30`, Tiefe abstandsabhängig nach vorn, `[World] SkidDepthBias = 0.003` in `lfsp.ini`; ✅ 👤 2026-10-22: korrekt, nur noch selten verdeckt |
| B23 | Scheinwerfer-Glow ab ~92 m zur Kamera teils schwarz (auch im Original) | Tiefenversatz des Glow-Sprites `≈ −20000·b/d²` rundet in der Ferne auf 0, Glow kämpft mit dem Scheinwerferglas (`LICHT.md` §9.7) | ✅ Versatz höchstens −1: erst LFSP v8e, jetzt in nfs5_enhanced (`[Detail]`, Hook `LampGlowDepth`, PR nach upstream; LFSP v8f ohne eigenen Fix, `LICHT.md` §9.7); 👤 getestet |

| ID | Wer | Aufgabe | Status / Ergebnis |
|---|---|---|---|
| 3C.0 | 🤖 | Baseline-Matrix | ✅ `BUGS.md` §1 |
| 3C.1 | 🤖 | Mod-Quellen-Analyse B1–B3 (H2a/H2b) | ✅ kein Mod-Hook vorhanden → 3C.2 (`BUGS.md` §2) |
| 3C.2 | 🤖 | `gimme.dll`-Analyse über den Ghidra-Export: Zell-Laden, Partikel-Emitter, Licht-Spawn (ggf. B6-Culling). Grenzen und Lücken des Exports beachten (`GHIDRA.md` §3) | ⏳ wartet auf 👤-Freigabe (offener Punkt 1) |
| 3C.3 | 🤖 | Fix B1 (Emitter) | ⏳ nach 3C.2 |
| 3C.4 | 🤖 | Fix B2 (Map-Streaming) | ⏳ nach 3C.2 |
| 3C.5 | 🤖 | Fix B3 (Leuchtturm + Licht) | ⏳ nach 3C.2 |
| 3C.6 | – | Fix B4 | ✅ über Block 3T |
| 3C.7 | 🤖 | Fix B5 (Sky/Depth in dx11z) | ⏳ Analyse als Hypothese fertig (`BUGS.md` §4); nächster Schritt: Messlog um Z-Func/Depth-Write für Sky-Draws erweitern |
| 3C.8 | 🤖 | Fix B6 (Culling) | ⏳ Zuordnung fertig (`BUGS.md` §5) |
| 3C.11 | 🤖+👤 | Testschalter `ClampUnitUv` (B7) und `ForceTwoSided` (B8/B9) in dx11z; je einzeln im Spiel prüfen, auf Nebenwirkungen achten (Cockpitsicht, Karosserie-Innenseiten) | 🔄 gebaut und installiert. Läufe 1–2 ungültig (Schalter standen unter `[Test]` statt `[Dx11]`, korrigiert); B7 durch Texturbefund gestützt (`BUGS.md` §9.1). Läufe 1–3 wiederholen |
| 3C.9 | 👤 | Validierung aller Fixes bei DDS = 4 und DDS = 1 | ⏳ |
| 3C.10 | 🤖 | Doku und finale INI | ⏳ |

**Gate 3C:** Alle Fehler (B1–B9) behoben, Spiel stabil in HD mit erhöhter Sichtweite. **Fallback**, falls ein Fix zu invasiv ist: DDS auf den höchsten stabilen Wert fixieren und betroffene Zellen in `.scn` priorisieren (👤 entscheidet nach 3C.2).

### Phase 4 – Fahrzeugdaten (optional) ⏳

| ID | Wer | Aufgabe | Ergebnis |
|---|---|---|---|
| 4.1 | 🤖 | `.sim/.crp/.lsp/.key` parsen | `SIM.md` + Dumper |
| 4.2 | 👤 | Zielwerte festlegen | `work/SIM_SPEC.md` |
| 4.3 | 🤖 | Werte anpassen und zurückpacken | geänderte Fahrzeuge |

### Phase 5 – W2 OpenNFS-NFS5-Loader (optional) ⏳

| ID | Wer | Aufgabe | Ergebnis |
|---|---|---|---|
| 5.1 | 🤖 | Lückenanalyse `src/Loaders/` für NFS5 | `W2_GAP.md` |
| 5.2 | 🤖 | Prototyp `nfs5_loader.cpp` (SHPI + FSH → Vulkan) | Demo-Szene |
| 5.3 | 👤 | Testen | Freigabe |

### Phase 6 – Konsolidierung ⏳

| ID | Wer | Aufgabe | Ergebnis |
|---|---|---|---|
| 6.1 | 🤖 | Doku und Tool-Suite konsolidieren | reproduzierbares Paket |
| 6.2 | 🤖 | Anleitung „von Grund auf“ für eine frische Installation | `README-HOWTO.md` |
| 6.3 | 👤 | End-to-End-Test auf sauberer Win11-Maschine | „fertig & stabil“ |

### Phase 7 – Gelände-Unterteilung (optional) ⏳

Ziel: runderes Gelände durch feinere Unterteilung, sinngemäß wie Skyrim „Smooth Terrain“. Erst Machbarkeit (7.1–7.3), dann entscheidet 👤 über den Weg. **Nicht in dx11z:** Der Treiber sieht nur Bildschirm-Vertices ohne Normalen, Nachbarschaft und Weltkoordinaten; Unterteilung dort gäbe Risse und Beulen. Randbedingung: Die Fahrbahn darf sich optisch nicht von der Physik lösen (Räder sinken ein oder schweben).

| ID | Wer | Aufgabe | Ergebnis | Abnahme | Status |
|---|---|---|---|---|---|
| 7.1 | 🤖+👤 | Messlog in dx11z: Dreiecke je Bild und Kantenlängen der Strecken-Draws (z. B. als Histogramm) erfassen; eine Fahrt auf coastal und alps | Messwerte | Aussage, ob das Gelände so grob ist, dass sich Unterteilung sichtbar lohnt | ⏳ |
| 7.2 | 🤖 | Format `GameData/Track/<strecke>.crp` prüfen: enthält es die Gelände-Geometrie, und nutzt die Physik dieselben Daten oder eigene Kollisionsdaten (`.edg`, `.jnc`?) | Formatnotiz | Aussage, ob Weg B möglich ist | 🔄 Gelände-Geometrie: ja (`SICHTWEITE.md` §2b). Physik/KI haben eigene Wegpunkte `SimD` ohne Querneigung (§2d); ob die Räder SimD oder das Mesh abtasten, ist offen |
| 7.3 | 🤖 | Weg A bewerten: Stelle in `gimme.dll`, an der Strecken-Vertices noch in Weltkoordinaten vorliegen, und Puffergrenzen der Engine (Ghidra) | Kurzbewertung | Aufwandsschätzung A vs. B | ⏳ |
| 7.4 | 👤 | Entscheidung: Weg A (Hook vor der Transformation), Weg B (Streckendateien offline unterteilen) oder verwerfen | Eintrag in §1 | Entscheidung liegt vor | ⏳ |

### Phase 8 – Fenster und Verdeck live umschalten (optional) 🔄

Ziel: Seitenscheiben im Rennen per Taste (später ggf. Mausklick) öffnen und schließen, ohne Dateien zu tauschen; danach dasselbe für das Cabrio-Verdeck. Herleitung: `FENSTER-VERDECK.md`. Weg: eigenes ASI-Plugin, das `porsche.exe` hakt (wie `nfs5_enhanced`), nicht dx11z.

| ID | Wer | Aufgabe | Ergebnis | Abnahme | Status |
|---|---|---|---|---|---|
| 8.1 | 🤖 | Patch „Window Opener“ und Style-Auswertung der Engine analysieren | `FENSTER-VERDECK.md` §1–§3 | Mechanismus belegt | ✅ Fenster machbar (Tabelle `+0x56C`, Draw-Liste neu bauen); Verdeck lädt zusätzlich Texturen |
| 8.2 | 👤 | Im Menü prüfen: animiert das Spiel das Verdeck oder schaltet es nur um? Taste für Fenster festlegen; Maus ja/nein | Rückmeldung | Entscheidungen liegen vor | ✅ Menü animiert das Verdeck (GIF); Taste in INI frei wählbar, Standard `F`; Maus ohne Priorität |
| 8.3 | 🤖+👤 | Mess- und Test-Plugin `nfsp_carparts.asi` v1: Messlog, Taste schaltet Fenster | `work/logs/8.3-carparts/`, `FENSTER-VERDECK.md` §6.1 | Verdeck-Mechanismus und Fensterwirkung geklärt | ✅ ausgewertet: keine Wirkung, weil Style ab 0 zählt; Verdeck = Mesh-Animation Slot 1 (`FENSTER-VERDECK.md` §3.1) |
| 8.3b | 🤖+👤 | Plugin v2: Style-Fix, nur Spielerauto, Verdeck-Taste `V` spielt Slot 1 ab | `FENSTER-VERDECK.md` §6.2–§6.3 | Verdeck bewegt sich im Rennen; Scheiben ändern sich | ✅ ausgewertet: Verdeck im Menü ✅, im Rennen ❌ (Teil fehlt); Fenster ❌ (falsche Liste neu gebaut) |
| 8.3c | 🤖+👤 | Plugin v3: Geometrie-Neuaufbau über `0x43C1B0`, Verdeck-Teil im Rennen einschalten | `FENSTER-VERDECK.md` §6.4–§6.5 | Scheiben und Verdeck ändern sich im Rennen, ohne Doppel-Dach | 🔄 **Fenster ✅, Verdeck ✅ (v3f)**; Rest: ein Flacker-Bild nach dem Öffnen → v3g endet knapp unter dem letzten Bild (MD5 `f44e61a0…`, installiert), Test 👤 offen |
| 8.3d | 🤖+👤 | Verdeck für alle Cabrios: Teile wie im Menü statt `[fileN.top]` | `FENSTER-VERDECK.md` §6.6 | Boxster und weitere Cabrios ohne starres Verdeck, Spoiler intakt, Geräusche | ✅ Boxster komplett mit v3o (MD5 `1d519f84…`, 2026-10-16); offen: weitere Cabrios (964/993/996) |
| 8.3e | 🤖+👤 | Scheiben animieren: Messbuild v4a, Taste `G` verschiebt die Scheiben einmal um die halbe Höhe (eigener Vertex-Puffer je Modell) | `FENSTER-VERDECK.md` §6.7 | Verschieben wirkt, Gegner unverändert, Innenansicht beurteilt | 🔄 v4a ❌ (keine Bewegung: Puffer schon vorhanden, falsche Achse); v4b: Animation ✅, aber nach oben (INI-Vorzeichen falsch gelesen); v4c ✅ (nach unten, nur eigenes Auto); v4d ✅ (folgt der Neigung nach außen); v4e: `WindowDepth = 1.05`; v5a: Animation an `F` gekoppelt ✅; v5b: Verdeck-Umkehr ✅; v5c ✅ (Kappen an der Brüstung); v5d ✅ (Coupés teils nur Innenscheibe, modellbedingt); v5e ✅ (nach vorn); Nutzerwerte als Standard übernommen (MD5 `d677373b…`) |
| 8.4 | 🤖 | Plugin fertigstellen: nur Spielerauto, Fehler 914 1.7/944 über andere Varianten, Speicherleck beim Neuaufbau prüfen | `nfs5_enhanced_LHedit/lfsp/` | nach 8.3 | ⏳ |
| 8.5 | 👤 | Testfahrt mit Cabrio und Targa, beide Zustände, Cockpit- und Außenansicht | Screenshots | Scheiben gehen auf und zu, kein Absturz | ⏳ |
| 8.6 | 🤖 | Verdeck fertigstellen: Startzustand aus dem Menü übernehmen, Teile/Texturen bei offenem Verdeck prüfen | Plugin-Erweiterung | nach 8.3b | ⏳ |
| 8.7 | 🤖 | Optional: Mausklick auf die Scheibe (Zeiger + Trefferprüfung im Engine-Hook) | Erweiterung | nach 8.5 | ⏳ |

### Phase 9 – Licht und Reflexionen (optional) 🔄

Ziel: Scheinwerfer bei Nacht schöner (Lichtkegel, Abfall mit Entfernung), Live-Reflexionen am Auto (Weg A: Umgebungstextur jedes Bild aus dem letzten Bild erneuern), Spiegel prüfen. Weg: dx11z. Nachbearbeitung testet 👤 vorab mit ReShade (`dxgi.dll` neben `Porsche.exe`).

| ID | Wer | Aufgabe | Ergebnis | Abnahme | Status |
|---|---|---|---|---|---|
| 9.1 | 🤖 | Messlog um Licht-Kandidaten erweitern: Draws mit zweiter Textur-Stufe und multiplizierende/addierende Blends (`M3T pass`), BMP-Abzug der Texturen (`m3t_*.bmp`), mittlere Vertexfarbe der deckenden Welt | `dx11z-nfsp.patch` | baut | ✅ |
| 9.2 | 👤 | Nachtfahrt mit `Measure = 1`, Scheinwerfer an/aus; Log und `m3t_*.bmp` sichern | `work/logs/9.2-scheinwerfer/`, `LICHT.md` §2 | Mechanismus belegt (Projektion, Zusatz-Pass oder Vertexfarben) | ✅ keine zweite Stufe, keine Lichttextur; nur Leuchtsprites; Aufhellung vermutlich in Vertexfarben. Zuordnung an/aus unsicher (keine Zeitangaben) |
| 9.2b | 🤖+👤 | Messlog mit Zeitangaben und 10-s-Berichten; Stand-Fahrt an der Startlinie, an/aus/an | `work/logs/9.2b-scheinwerfer/`, `LICHT.md` §4 | Vertexfarbe springt mit dem Licht → Vertex-Beleuchtung belegt | ✅ Halo-Sprites folgen dem Licht; gemittelte Vertexfarbe zu unempfindlich, Frage offen |
| 9.2c | 🤖+👤 | F11-Momentaufnahme (Farbe je Textur nach Bildfläche gewichtet); Licht aus/an/aus, je F11 | `work/logs/9.2c-snapshot/`, `LICHT.md` §5 | Vertex-Beleuchtung belegt oder widerlegt | ✅ **belegt:** Engine addiert neutral-weißen Anteil zu den Vertexfarben (+19…+56), Mittel 71/67/57 → 88/84/74 und exakt zurück; Lichtkegel = Textur `40EB6B06` |
| 9.3 | 🤖 | Auswertung, Vorschlag Scheinwerfer-Verbesserung | `LICHT.md` §5.3 | 👤 entscheidet: Pixel-Spotlicht (Engine-Hook nötig) oder nur HD-Texturen für Kegel/Halos | ✅ Vorschlag liegt vor, Entscheidung offen |
| 9.4 | 🤖+👤 | Engine-Suche Scheinwerfer: Aufrufer-Adressen, Stack-Scan, Ghidra-Pipeline, Haltepunkte | `LICHT.md` §6 | Funktion gefunden, die den Vertex-Anteil berechnet; danach Gesamtaufwand neu schätzen | ✅ **`FUN_00453110`** (Licht an? → `FUN_00452910` Lichtfaktor aus 128×128-Lichtkarte `DAT_0061e1cc` → Grundfarbe + Lichtfarbe × Faktor), `LICHT.md` §6.14–6.15. F11 = Spieltaste (§6.13) |
| 9.5 | 🤖+👤 | Messbuild: Lichtkarte als BMP, Matrix/Skalen/Farbe loggen; Modell offline nachrechnen | `LICHT.md` §7 | nachgerechneter Faktor = Engine-Faktor | ✅ alle 12 Faktoren exakt nachgerechnet; Lichtkarte = weicher ovaler Fleck, Farbe `FF808080`, Positionen in Weltkoordinaten (`LICHT.md` §7.2) |
| 9.6 | 🤖+👤 | Pixel-Scheinwerfer in dx11z (Daten-Hook, Lichtkarte als Textur, Vertex-Anteil aus, Shader), INI-Schalter | `LICHT.md` §7 | 👤: sieht besser aus, kein doppeltes Licht | ✅ 9.6b: Licht pro Pixel an derselben Stelle wie das Original, weicher Verlauf, kein doppeltes Licht (👤 Sichtprüfung, `LICHT.md` §7.6) |
| 9.6c | 🤖+👤 | Scheinwerfer pro Pixel auch bei Tag (`[Dx11] HeadlightsDay`) | `LICHT.md` §7.7 | 👤: Licht bei Tag sichtbar, Lichtschalter wirkt (Stufe 1) | ✅ 9.6h: Licht bei Tag sichtbar (👤 Screenshot), Lichtschalter wirkt am Tag (`light bytes 1 1`); Ursachen auf dem Weg: Farbknoten nur nachts, Matrizen erst im Draw gültig, Tag-Projektion mit w in Spalte 2 (`LICHT.md` §7.8–§7.13) |
| 9.7 | 🤖+👤 | Eigene Lichtkarte nur im Treiber (`[Dx11] HeadlightsMap`, Graustufen-BMP, auch > 128²), mitgelieferte Karte mit zwei Kegeln, hellem Kern, Abfall über die Entfernung | `LICHT.md` §8.1 | 👤: eigene Karte wirkt bei Tag und Nacht, sieht nach Scheinwerfer aus | ✅ 9.7a: eigene Karte wirkt bei Tag und Nacht, Ausrichtung geprüft (Schriftzug „RECHTS“ rechts, nicht gespiegelt; 👤 Screenshots `screenshots/9.7a/`, `LICHT.md` §8.5) |
| 9.8 | 🤖+👤 | Baujahr des Spielerautos live aus dem Spiel lesen (auch Zusatz-/Mod-Autos): Quelle der Menü-Jahre finden, Speicherstelle am Auto | `LICHT.md` §8.2 | Jahr für mind. 3 Autos richtig im Log (z. B. 356, 911, 996) | ✅ 9.8a (LFSP v6a, damals `nfsp_carparts.asi`): Jahr aus dem `.sim`-Namen in `car + 0x760`; 356 B = 1960, 959 = 1987, GT3 = 1998 für Spieler und Gegner richtig (`LICHT.md` §8.2, Log `work/logs/9.8a-lfsp/`) |
| 9.9 | 🤖+👤 | Lichtfarbe nach Baujahr (`[Dx11] HeadlightsColourByYear`): Glühlampe gelb → Xenon kaltweiß, fließend | `LICHT.md` §8.3 | 👤: 356 deutlich gelber als 996, Übergänge plausibel | ✅ 9.9a Farbe nach Baujahr wirkt (👤 Screenshots `screenshots/9.9a/`); ✅ 9.9b: Licht mischt sich mit farbiger Umgebung (Summe nicht mehr vor der Textur gekappt, `LICHT.md` §8.3a; 👤: „sieht realistisch aus“) |
| 9.10 | 🤖+👤 | Fernlicht: `L` gedrückt halten → zweite Karte (`HeadlightsMapHigh`) oder Abblendkarte gestreckt und heller, weniger Höhenabzug | `LICHT.md` §8.4 | 👤: Fernlicht reicht sichtbar weiter, Taste kollidiert nicht mit dem Spiel | ✅ 9.10c: Taste K, nur nach oben gestreckt (Höhenabzug 0,4, Helligkeit 1,2), wirkt auch ohne Abblendlicht wie eine Lichthupe (👤 getestet, Log `work/logs/9.10c/`: Taste bei `light bytes 0 0`; `LICHT.md` §8.4b). 9.10d (Lichtschalter mitsetzen) öffnete die Klappen, schaltete aber Heckleuchten mit und blieb nach Antippen an. ✅ 9.10e: LFSP fährt nur die Klappen-Animation selbst, Schalter unberührt (LFSP v6i; 👤: Klappen öffnen mit K, Heckleuchten bleiben aus, Antippen schließt wieder; `LICHT.md` §9.4). ✅ 9.10f (LFSP v8e): Glow der eigenen Scheinwerfer bei Fernlicht heller (`HeadlightsHighGlow` 1,6, `LICHT.md` §9.7); Seitenmarkierungen auf Kanal 0/1 nicht mehr mit verstärkt (nach Lage, 👤 Test offen). ✅ 9.10g: `HeadlightsHighHeight` = Höhe an Wänden (1 = Abblendlicht, 2 = doppelt; vorher Kehrwert 0,4), 👤 „funktioniert“ |
| 9.11 | 🤖+👤 | Scheinwerfer der Gegner beleuchten nachts die Strecke, wenn ihr Licht an ist (bis 8 Lichter, Matrix je Auto wie `FUN_004526c0`) | `LICHT.md` §8.4a | 👤: Gegner vor/neben dem Spieler werfen sichtbares Licht, kein Einbruch der Bildrate | ✅ 9.11b (dx11z 9.11b, LFSP v6d; `LICHT.md` §8.4a): 👤 „solide, keine Probleme“; alle Licht-Schalter in `lfsp.ini [Lights]`. Das gesicherte Log enthält nur ein Tagrennen; dort blieben die Gegner aus, weil die Matrix-Prüfung nur nachts lief → 9.11c. Nachts schlug die Matrix-Prüfung fehl (Translation falsch nachgebildet), Gegner waren nie aktiv → ✅ 9.11d: Gegner leuchten nachts, unabhängig vom eigenen Licht (👤 Screenshot, Log: Variante transponiert/Zeile exakt) |

### Phase 10 – Menü-Komfort (Feature-Requests) 🔄

Wünsche des Nutzers an die Bedienung. Herleitung: `MENU.md`.

| ID | Wer | Aufgabe | Ergebnis | Abnahme | Status |
|---|---|---|---|---|---|
| 10.1 | 🤖+👤 | Scrollen in Menülisten (Fahrzeugauswahl, Lackauswahl, Strecken usw.) per Mausrad und Pfeiltasten. Heute nur durch Ziehen der Scrollleiste rechts mit der Maus; Pfeiltasten wirken nicht (gemeldet 2026-10-17) | `MENU.md` §1 | 👤: Listen lassen sich mit Mausrad und Pfeiltasten scrollen | ✅ Ursache: Verteiler der Steuerelemente kennt Mausrad-Ereignisse nicht, Tasten-Slot der Listen leer (`MENU.md` §1.3). 10.1a (LFSP v7g, MD5 `97b8f868…`, installiert): Rad über der Liste 3 Zeilen je Rastung, Pfeile 1 Zeile, Bild hoch/runter 1 Seite, Pos1/Ende; `lfsp.ini [Menu]`. 👤: Rad wirkt, Autoliste aber nicht bis zum Ende (Zeilenhöhe geschätzt). 10.1b (v7h): Grenze aus der Höhe jedes Eintrags, 👤: noch weniger weit. 10.1c (v7i, MD5 `caff8158…`, installiert): Scroll-Accessor zieht die letzte Seite schon selbst ab, Grenze = Anzahl − 1 wie die Leiste (`MENU.md` §1.5). ✅ 👤 2026-10-18: funktioniert |
| 10.2 | 🤖+👤 | Zugeklappte Liste per Mausrad durchschalten: Maus auf der Liste, Rad nach unten = nächster Eintrag, nach oben = vorheriger (Wunsch 2026-10-18) | `MENU.md` §1.6 | 👤: Auswahl wechselt mit dem Rad wie per Klick | 🔄 10.2a (LFSP v7j, MD5 `4ff6880a…`, installiert): `[Menu] WheelSelect`; Test 👤 offen |

### Phase 11 – Neue Spielfunktionen (Feature-Plan des Nutzers) ⏳

Feature-Plan vom 2026-10-17. Nur aufgenommen, noch nicht untersucht oder priorisiert. Punkte mit Bezug zu Licht und Reflexionen gehören fachlich zu Phase 9 und werden dort umgesetzt, sobald sie dran sind.

| ID | Wer | Aufgabe | Abnahme | Status |
|---|---|---|---|---|
| 11.1 | 🤖+👤 | **Kamera „Cinematic“:** neue Ansicht im Rennen mit den Kameraperspektiven der Wiederholungen | 👤: Ansicht wählbar, wirkt wie im Replay | 🔄 Kamera-System entschlüsselt (Typen, Zustand `0x627E80`, Regisseur der Wiederholung `FUN_00413b80` mit Zonen aus `*_cameras.scn`; `KAMERA.md` §1–3). 11.1a (LFSP v6l): Cinematic als fünfte Ansicht der Taste C und auf Taste N, Streckenkameras wechseln mit den Zonen (`KAMERA.md` §4). ✅ 👤 funktioniert (Test mit v6m). 11.1b (v6n): beim Einschalten sofort die nächste Streckenkamera statt bis zur ersten Zone die Verfolgerkamera; stürzte beim Kamerawechsel ab (Vektor falsch gelesen, `KAMERA.md` §4), behoben in v6o; Test offen |
| 11.2 | 🤖+👤 | **Kamera „Action“:** reagiert dynamisch auf die Fahrt und ändert Position, Winkel und FOV. Linkskurve: Kamera wandert nach rechts, Auto bleibt links im Bild zentriert, Blick dreht leicht in die Kurve. Starkes Bremsen/Crash: Kamera rückt näher ans Auto. Sprünge/starkes Auf und Ab: Kamera schwingt mit dem Schwung mit. Offroad/Schaden: Kamera wackelt bzw. vibriert leicht | 👤: Verhalten wie beschrieben, angenehm, nicht übertrieben | 🔄 11.2a (LFSP v6l): Verfolgerkameras mit Seitversatz und Blick in die Kurve, Heranrücken beim Bremsen, Feder bei Sprüngen, FOV nach Tempo, Wackeln auf unebenem Grund; Taste J zum Vergleichen, Stärken in `lfsp.ini [Camera]` (`KAMERA.md` §4). 👤 (Test mit v6m): funktioniert; Kamera hing bergauf zu tief (Feder ohne Steiggeschwindigkeit), Grundhöhe etwas höher, Blick in die Kurve stärker. 11.2b (v6n): Feder folgt der Steigung, höchstens 0,25 m tiefer, `ActionHeight` +0,25 m, `ActionLook` 7°. 11.2c (v6o, 👤 Wunsch): Rollen in Kurven, Schwung beim Einlenken mit Nachschwingen (unterdämpfte Federn), Stoß-Wackeln bei Aufprall und Landung. 👤: Stoß-Wackeln zu stark und ständig beim Bremsen, auch langsam (Auslöser rauschte). 11.2d (v6p): Auslöser = Geschwindigkeitssprung je Simulationsschritt über 3 m/s, Stärke halbiert (`KAMERA.md` §4). ✅ 👤 „besser“, aber gummiartig, Schwung beim Einlenken zu stark, Zurückstellen zu heftig. 11.2e (v6q): Anstoß halbiert und nur beim Einlenken, Federn stärker gedämpft, Zurückstellen langsam ohne Überschwingen. 👤: Einlenken noch zu heftig (Tastatur). 11.2f (v6r): Kurven-Effekt baut sich über `ActionBuildUp` 1,2 s auf, `ActionOvershoot` 0,5. ✅ 👤 „passt gut“. 11.2g (v6s, 👤 Wunsch): Kurven-Effekte mit dem Tempo bis +35 % stärker, FOV-Zuwachs bis +20 % ab ≈ 30 km/h; neu `CockpitFov` (Vorgabe 90°, Spiel 75°, fest) für die Innenansichten. 👤: Zittern offroad und ab ≈ 200 km/h, bei hohen FPS wie Unschärfe. 11.2h (v6y): Höhenfeder ohne Stufen des Simulationstakts, Offroad-Wackeln mit Schwelle, tiefer und schwächer (`KAMERA.md` §4); Test offen. Schaden als Wackel-Quelle fehlt noch |
| 11.3 | 🤖+👤 | **HUD-Instrumente Tag/Nacht nach Lichtschalter:** analoge Instrumente wechseln zwischen Tag- und Nacht-Version, wenn das Licht ein- oder ausgeschaltet wird. Heute hängt das am Streckentyp (Tag/Nacht) | 👤: Instrumente folgen dem Lichtschalter | 🔄 11.3a (v6k) stürzte beim Rennstart ab (Bildnummern über den HUD-Puffer hinaus). ✅ 11.3b (LFSP v6m, getestet mit v6r): Zusatzbilder mit den Nummern der ersetzten Bilder, Instrumente wechseln mit L, nicht mit der Lichthupe (`LICHT.md` §10.3); `InstrumentsFollowLights = 1` Standard |
| 11.4 | 🤖+👤 | **Scheinwerfer beleuchten alles:** heute werden Gegner-Autos nicht angestrahlt (Bezug Phase 9, B17) | 👤: Gegner im Lichtkegel sichtbar beleuchtet | ✅ 11.4a (👤 2026-10-18: Licht funktioniert gut): `[Lights] HeadlightsObjects = 1`, Licht des Bildes auf alle Draws mit Tiefentest (`LICHT.md` §11) |
| 11.5 | 🤖+👤 | **Rücklichter mit schwachem rotem Schein:** wie die Frontscheinwerfer, nur rot und deutlich schwächer (Bezug Phase 9). Ergänzt 2026-10-17: immer leicht sichtbar (Tag und Nacht), beim Bremsen etwas stärker; Blinker als optionales Ziel; ist die Leuchte im Spiel beschädigt, strahlt sie nicht mehr (wie das Spiel es bei den Scheinwerfern schon macht) | 👤: dezenter roter Schein hinter dem Auto, beim Bremsen heller, defekte Leuchte dunkel | 🔄 Leuchten-Tabelle `[model + 0x2D4]` und Leuchten-Logik `0x439360` entschlüsselt (Bremse 2/3+10/11, Rücklicht 12/13, Rückfahrlicht 8, Blinker 4–7/10/11, Schaden je Leuchte; `LICHT.md` §9.1–9.3, Messlauf 11.5a ausgewertet). ✅ 11.5b: roter Schein hinter jedem Auto, Bremse stärker, Blinker bernsteinfarben, beschädigte Leuchte dunkel (👤 „sieht gut aus“, `LICHT.md` §9.5). 11.5c: Rücklicht schwächer (0,06), Bremslicht gleich hell (0,47). 👤: Blinker-Schein blinkt nicht, vorn fehlt er, Farbe fest. 11.5d (dx11z 11.5d, LFSP v6v): jede Blinker-Leuchte einzeln im Takt, Schein auch vorn, Farbe aus der Leuchte des Modells (`LICHT.md` §9.6). 👤: vorn/hinten gegenphasig, 356 hinten weiß, Bremse nicht heller. 11.5e: vorn = Modell-z < 0, alle Blinker im Takt P, Bremse über die Stärke `[model + 0x2D8]`. 👤: Farben passen; Leuchten des Spiels sollen auch gleichzeitig blinken, bei Abblendlicht kein Schein. 11.5f (LFSP v6x): `SignalsInSync` (Spiel-Blinker hinten im Takt), `TailGlowLights` 0,12 bei Licht an. 11.5g (👤 Wunsch): Rückfahrlicht `TailGlowReverse` 0,35, nur bei Modellen mit Leuchte 8; Test offen. 11.5i (LFSP v7h): Fernlicht aus bei kaputten Scheinwerfern, 3. Bremslicht nur beim Bremsen (`ThirdBrakeLightBrakingOnly`, `LICHT.md` §9.6); ✅ 👤 beides funktioniert |
| 11.6 | 🤖+👤 | **Rauch-Emitter an der Ziellinie** | 👤: Rauch an der Ziellinie sichtbar | ⏳ nur aufgenommen |
| 11.7 | 🤖+👤 | **Mehr Vegetation und Kleinkram:** mehr Büsche, Gräser und sonstige Streckendeko; eventuell weitere Objekte wie zerstörbare Kisten | 👤: Strecken wirken dichter, ohne Leistungseinbruch | ⏳ nur aufgenommen |
| 11.8 | 🤖+👤 | **Durchdrehende Reifen/Burnout:** das Spiel scheint einen echten Burnout zu verhindern; dieses Verhalten anpassen | 👤: Burnout möglich, Fahrverhalten sonst unverändert | ⏳ nur aufgenommen |
| 11.9 | 🤖+👤 | **Besserer Autoschatten:** der Bodenschatten ist kein Echtzeitschatten, sondern heute ein Rechteck. Ist er eine Textur, eine realistischere Form erstellen (Kandidat: `GameData/CarModel/Shadow.fsh`) | 👤: Schatten passt zur Autoform | ⏳ nur aufgenommen |
| 11.10 | 🤖+👤 | **Live-Reflexionen auf Auto, Fenstern und Spiegeln:** bereits in Phase 9 geplant. Zusatzidee: an Gebäuden spiegelnde Bereiche (Fenster) über die Alpha-Map der Textur markieren | 👤: Fenster an Gebäuden spiegeln, Wände nicht | ⏳ nur aufgenommen |
| 11.11 | 🤖+👤 | **Auspuffrauch bei Motorschaden** (Wunsch 2026-10-23): beschädigter Motor qualmt etwas aus dem Auspuff | `knowledge.md` | 👤: Auto mit Motorschaden zieht leichten Rauch hinter sich her, unbeschädigt kein Rauch | 🔄 11.11a (LFSP v8g, installiert, v8f als `lfsp.asi.v8f.bak`): zwölfter Emitter je Partikel-Satz mit System VehicleExhaust, Stärke nach Motorschaden `FUN_004938a0`, `[Damage]` in `lfsp.ini`; Test 👤 offen |

### Phase 12 – Verkehr: mehr und vielfältiger (Feature-Request) 🔄

Wunsch 2026-10-18: mehr Verkehr, im Menü wählbar, und mehr Abwechslung. Herleitung: `VERKEHR.md`.

| ID | Wer | Aufgabe | Ergebnis | Abnahme | Status |
|---|---|---|---|---|---|
| 12.1 | 🤖+👤 | Ursprung von Anzahl und Modellwahl finden, Messbuild | `VERKEHR.md` §1–4 | Log einer Fahrt mit Verkehr ausgewertet | ✅ Anzahl = Menüwert `Track1.Traffic`, Grenze 9 Autos gesamt, Modell starr `ID[i % 7]` aus dem Strecken-Datensatz. 12.1a (LFSP v7k, MD5 `e0a1784c…`): `[Traffic] Measure`; 👤: Absturz beim Laden des Rennens; 12.1b (v7l): Absturz beim Öffnen der Streckenoptionen. Ursache: zwei Hooks als thiscall, die Funktionen sind cdecl (`ret`), Stapel zerstört (`VERKEHR.md` §4). Modellliste und Autos vorher korrekt geloggt (coastal: 7 Verkehrsautos, IDs 201 193 191 189 196 189 189). 12.1c (v7m, MD5 `970f0491…`): Rennaufbau per cdecl-Stub, Hook auf `FUN_004FC0B0` entfernt. ✅ 👤 2026-10-18: läuft; Daten aus `fe_out.txt` und `FEData/Data/nfs5.trk`/`nfs5.car` ausgewertet (`VERKEHR.md` §3). Leicht = 2, Mittel = 5, Schwer = 7 |
| 12.2 | 🤖+👤 | Menüauswahl für mehr Verkehr (Stufe A: bis zur 9er-Grenze) | `VERKEHR.md` §2 | 👤: Stufe wählbar, mehr Autos sichtbar | ✅ Stufen liefern 0/2/5/7 (Optionstabelle `0x5B9628`). 12.2a (LFSP v7q, MD5 `8a3c4807…`, nicht installiert; enthält v7p der B15-Sitzung): `[Traffic] Light/Medium/Heavy`, Vorgabe Schwer = 8. 👤-Test 2026-10-18 lief versehentlich mit v7p: Schwer + 7 Gegner → 9 Autos (1 Verkehr), Kürzung des Spiels ✅, Rennen stabil. ✅ 👤 2026-10-18 (v7q): Schwer ohne Gegner = 8 Verkehrsautos (9 Autos gesamt), läuft |
| 12.3 | 🤖+👤 | Mehr Vielfalt (Modellmix zufällig, weitere Modelle) | `VERKEHR.md` §4a | 👤: Verkehr abwechslungsreicher | ✅ je Strecke nur 4–6 von 16 Modellen, fest `i % 7`. 12.3a (LFSP v7n, MD5 `7a0d3cfa…`): `[Traffic] RandomModels`, `Models`. ✅ 👤 Mischung wirkt; Schneepflug `EAS4` fuhr aber auf coastal. 12.3b (v7o, MD5 `6b9c8680…`, installiert): `TrackOnly = EAS4`, nur auf alps/monaco2 (`VERKEHR.md` §4a). ✅ 👤 2026-10-18 |
| 12.5 | 🤖+👤 | Verkehrsauto auf der falschen Straßenseite (👤 2026-10-18, Screenshot) | `VERKEHR.md` §4b | 👤: Verkehr hält Rechtsverkehr | ❌ verworfen 2026-10-18 (👤: vermutlich Ausweichmanöver) |
| 12.6 | 🤖+👤 | Porsches (Spielerfahrzeuge) im Verkehr – Wunsch 2026-10-22 | `VERKEHR.md` §4e | 👤: Porsches fahren als Verkehr, kein Absturz | 🔄 machbar: gleicher Datensatz und Aufbau-Weg wie die Polizei. 12.6a (LFSP v7z, MD5 `714c939b…`, enthält v7y, nicht installiert): `[Traffic] Porsches` = Anteil in %; Test 👤 offen |
| 12.8 | 🤖+👤 | Verkehr in typischen Lacken statt Zufallsfarben (👤 2026-10-22) | `VERKEHR.md` §3 | 👤: überwiegend Grau/Silber/Schwarz/Weiß | 🔄 12.8a (LFSP v8b, LHedit `build-asi/lfsp`, MD5 `4f89a038…`, nicht installiert): `[Traffic] Colours`. 12.8b (v8d, MD5 `14262e10…`, enthält v8c der Fernlicht-Sitzung): `ColourRandom` = Anteil Zufallsfarbe (Vorgabe 10 %); Test 👤 offen |
| 12.7 | 🤖+👤 | Verkehr überholt ohne schneller zu sein und fährt neben anderen her, Unfälle mit Gegenverkehr (👤 2026-10-22) | `VERKEHR.md` §4c | 👤: kein Nebeneinander auf der Gegenfahrbahn | 🔄 Ursache: Abzug für die Gegenfahrbahn in der Spurwahl nur −12 (`0x401A21`). 12.7a (LFSP v8a, MD5 `ce4fb20e…`, enthält v7z, nicht installiert): `[Traffic] Overtake = 0` → −1000; Test 👤 offen |
| 12.4 | 🤖+👤 | Mehr als 9 Autos (Verkehr, später Gegner) – Wunsch 2026-10-18 | `VERKEHR.md` §6 | 👤: 12+ Autos im Rennen ohne Absturz | 🔄 Machbarkeit geprüft: machbar per Limit-Adjuster (Tabellen verlegen); ≥ 10 Auto-Listen à 9, KI-Zeiger, Beschreibungen (9 × `0x580`), für Gegner zusätzlich 8er-Ranglisten; Stufen A–C (`VERKEHR.md` §6.4). Inventar für Stufe B (`VERKEHR.md` §6.5), Umfang B1 (§6.6). 12.4a (LFSP v7t): `[Cars] MaxCars` (9 = aus, höchstens 16), 221 Code-Stellen + Kopier-/Löschstellen + Menügrenze. 👤 2026-10-21 (MaxCars 12): Absturz nach den ersten Bildern, Pool der KI-Zustände (10 Plätze) leer (`VERKEHR.md` §6.7). 12.4b (v7u): Pool mit verlegt, 242 Stellen. 👤: Rennen läuft, Absturz beim Beenden (Ergebnis-Tabelle `0x5E75A0`, 9 Plätze, überschrieb Zähler). 12.4c (v7v): Ergebnisse und Kartenpunkt-Blinken verlegt, unteres `.data` inventarisiert, 260 Stellen. 👤: Absturz Sekunden nach Start im Ton (gefaltete Basis `0x5E8AEC` = Liste aller Autos − 0x48). 12.4d (v7w, MD5 `168a479f…`, installiert): 3 Ton-Stellen der Liste aller Autos zugeordnet. ✅ 👤 2026-10-21: 12 Autos (11 Verkehr), Ziel und ESC-Abbruch ohne Absturz. Offen: 16 Autos, Wiederholung/Neustart, dx11z/Interpolation ab Auto 10, Stufe C. 12.4e (v7x, MD5 `f13516e1…`, installiert): gespeicherte Wiederholungen mit Begleitdatei `lfsp_replays\`. ✅ 👤 2026-10-21: 16 Autos (15 Verkehr), Wiederholung im Rennen, gespeicherte Wiederholung laden, Neustart ohne Absturz; 2 Begleitdateien angelegt. 12.4f (dx11z MD5 `f7bd8e9b…`, nfs5_enhanced MD5 `4b38f685…`, nicht installiert): Scheinwerfer/Rücklichter anderer Autos und Interpolation lesen die Liste über `LfspCarList`; Test 👤 offen. 👤 2026-10-22 (16 Autos): Verkehr setzt in Pulks ein. Ursache: Tabelle der Wartezeit `0x5CC4A0` hat 12 Einträge, ab 12 Verkehrsautos liest das Spiel dahinter (`VERKEHR.md` §4d). 12.4g (LFSP v7y, MD5 `42dcba2d…`, nicht installiert): eigene Tabelle bis 16 Autos; neu `[Traffic] Active` (gleichzeitig fahrend, Spiel 5); Test 👤 offen. Offen: Stufe C |

### Phase 13 – Größere Sichtweite ohne Löcher, Dunst statt Pop-in ⏳

Auftrag 👤 2026-10-21 (`plan_pvs_haze_1.md`). Abgleich mit dem Projektstand und angepasster Weg: `SICHTWEITE.md`. Eine Sichtbarkeitsliste wie in NFS4 gibt es nicht. Ihre Rolle spielen die 2D-Verdeckungswände (B20/B21).

| ID | Wer | Aufgabe | Ergebnis | Abnahme | Status |
|---|---|---|---|---|---|
| 13.1 | 🤖+👤 | Messbuild dx11z: Nebel-Zustände (Modus, Farbe, Dichte, Start/Ende, Tabelle) bei jeder Änderung loggen, dazu Sichtweite/Far-Clip und Draws ohne Tiefen-Schreiben (Himmel). 👤 fährt 3 Strecken Tag/Nacht/Regen, DDS 1 und 3 | `SICHTWEITE.md` §3 | Nebelquelle und Himmels-Draws belegt | ⏳ |
| 13.2 | 🤖 | Ghidra: Herkunft der Verdeckungswände `map+0x2C` (Ladefunktion, Datei, Byte 0 „Art“, Höhe?) | `SICHTWEITE.md` §4 | Format der Wände dokumentiert | ✅ Chunk `Vis ` in `<strecke>.crp` (QFS → `Trak`), 24 Byte je Wand = 2 Punkte, `y` = Art 0/1, **keine Höhe**; Art 1 gilt auch für ferne Objekte (`SICHTWEITE.md` §2a) |
| 13.3a | 🤖 | Trak-Container und Geometrie entschlüsseln, Werkzeug Blender ↔ `.crp` (Wunsch 👤 2026-10-21) | `SICHTWEITE.md` §2b, `work/track/trak_obj.py` | Round-Trip byte-genau; 👤: bearbeitete Strecke lädt und zeigt die Änderung | ✅ Format entschlüsselt, Export/Import fertig, Round-Trip byte-genau (4 Strecken); 👤 2026-10-21: coastal in Blender bearbeitet, lädt im Spiel |
| 13.3b | 🤖+👤 | Import mit neuer Topologie: Flächen hinzufügen (Unterteilen), Objekte innerhalb eines Objekts klonen, Vertexfarben (Wunsch 👤 2026-10-21) | `SICHTWEITE.md` §2b | 👤: unterteiltes Objekt und geklonter Busch im Spiel richtig texturiert und beleuchtet | 🔄 gebaut, offline geprüft. 👤-Test 1: Absturz (Primitive > 252 Ecken), Klone als neue Objekte ignoriert → Grenze 252 Ecken, Warnungen (`SICHTWEITE.md` §2b). ✅ 👤-Test 2: unterteilter Berg (3210 Vertices) läuft. Klone als `.NNN`-Objekte werden angehängt. ✅ 👤-Test 3: Schild-Klone sichtbar, ohne eigene Kollision, fest am Original |
| 13.3c | 🤖+👤 | Neue eigenständige Objekte: Wie erfasst das Spiel Objekte? Test mit kopierten Objekten (`trak_clone.py`) | `SICHTWEITE.md` §2c | 👤: kopiertes Schild steht neben dem Original, hat eigene Kollision; kopierter Busch sichtbar | 🔄 Spiel erfasst Objekte dynamisch aus der `Arti`-Liste (`FUN_004879d0`, Sortierer nach `Base`-Flags); Testdatei `work/track/out/coastal_klon.crp`. ✅ 👤: kopierte Schilder sind eigenständige physische Objekte |
| 13.3d | 🤖+👤 | Fremdes Modell als eigenständiges Objekt (Beachball), Stufe 1 mit vorhandener Textur | `SICHTWEITE.md` §2c, `trak_clone.py` | 👤: Ball sichtbar, umstoßbar, kein Absturz | 🔄 gebaut (Vorlage Kiste `0x711`, Hütchen sind `0x8000`-Vorlagen), `work/track/out/coastal_ball.crp`. ✅ 👤: Ball sichtbar, zerspringt beim Anfahren wie eine Kiste (Verhalten der Vorlage) |
| 13.3e | 🤖+👤 | Eigene Textur für ein neues Objekt (Beachball): Bild in die `.fsh`, neuer `mt`-Eintrag | `SICHTWEITE.md` §2c, `fsh_add.py`, `trak_clone.py` | 👤: Ball mit Streifen-Textur, übrige Texturen unverändert | ✅ 👤 2026-10-21: Ball mit eigener Textur im Spiel sichtbar (`out/coastal.fsh` + `out/coastal_ball2.crp`) |
| 13.3f | 🤖+👤 | Neue Objekte direkt aus Blender: Namensregel `NEU_<Vorlage>_<Textur>_<frei>` im OBJ-Import | `SICHTWEITE.md` §2c, `TOOLS.md` | 👤: in Blender platziertes Objekt erscheint an der Stelle im Spiel | 🔄 gebaut, offline geprüft (Lage, Material); 👤-Test offen |
| 13.3 | 🤖+👤 | Wandtest mit Höhe (A-a) bzw. Offline-Sichtbarkeit (A-b), je nach 13.2 | `SICHTWEITE.md` §4 | 👤: kein B20/B21, keine Löcher an Felswänden, DDS 3 | ⏳ |
| 13.4 | 🤖+👤 | Dunst in dx11z (`lfsp.ini [Haze]`), Himmel ausgenommen | `SICHTWEITE.md` §4 | 👤: kein Pop-in, keine Horizontkante, nachts nicht heller | ⏳ |

---

## 4. Risiken

| Risiko | Wahrscheinlichkeit | Gegenmaßnahme |
|---|---|---|
| Engine akzeptiert keine Texturen über 256×256 | mittel–hoch | 2.0 klärt das; Optionen: Hook in `gimme.dll` oder HD auf 256² |
| HD-Textur falsch dargestellt (Alpha, Mips, Farbmaske) | mittel | 2.0/2.3 mit Sichtprüfung; Fahrzeug-Alpha nicht interpolieren |
| 32-Bit-Prozess ohne LAA: HD-Satz sprengt 2 GB Adressraum oder interne Speicherbudgets der Engine | unbekannt | 2.0b messen; LAA-Flag nur nach Messung (prüfen, dass der Mod danach noch hakt) |
| Fahrzeugtexturen (`7B`) werden als ARGB4444 geladen → HD-Alpha dort nur 4 Bit (Strecke nicht betroffen) | hoch | `FSH-REPACK.md` §11.3; Test `RaceColorDepth = 32` (§11.4 B), sonst Eingriff in dx11z/Engine |
| `gimme.dll`-Analyse ohne Symbole dauert lange | hoch | nur gezielt (3C.2, 3.7) |
| Neues Mod-Release überschreibt eigene dx11z-Änderungen | mittel | eigene Änderungen im Fork `nfs5_enhanced_LHedit` (Git), Upstream per Merge; alte Patch-Kette `work/patches/dx11z/` (`MOD.md` §6) |

## 5. Definition of Done

Vom Nutzer auf Win11 bestätigt:
1. Startet und läuft **stabil unter Windows 11**.
2. Zeigt **hochauflösende Texturen** nach `HD-TEXTUREN.md`, ohne Artefakte.
3. Läuft in **HD-Auflösung** mit korrektem HUD und akzeptabler Performance.

---

## 6. Offene Punkte für 👤

1. **B1–B3:** Freigabe für 3C.2 (`gimme.dll`-Analyse) oder Fallback (DDS fixieren + `.scn`-Priorisierung).
2. **2.1:** Kunstentscheidung (`HD-TEXTUREN.md`).
3. **Belege B1–B3, B5, B6:** Screenshots liegen nur beim Nutzer; bitte nach `doc/screenshots/` legen.
4. **Ghidra-Lücken:** an den 11 Adressen aus `GHIDRA.md` §3 Funktionen anlegen und neu exportieren (vor 3C.2 sinnvoll).
5. ~~`coastal.fsh` unbekannter Stand~~ → geklärt: stammt aus den 512er-Versuchen des Nutzers, gesichert als `coastal.fsh.usertry`; Basis ist das Original `coastal - Kopie.fsh`.
6. ~~B10 eingrenzen~~ → Ursache `AlphaEdgeFix = 2` gefunden, Fix 3C.16 installiert.
7. `RaceColorDepth` nach Teil B wieder auf `0` setzen.
8. ~~8.2~~ → Verdeck animiert, Taste `F` (INI), Maus später.
9. **8.3d:** Boxster ✅ (v3o). Nächstes: 964/993/996 Cabrio und Targas testen; danach Release-Aufräumen (Verdeck-Daten ohne Menü-Umweg, Measure = 0, README).
10. ~~8.3e~~ → Scheiben-Animation fertig (v5e).

## 7. Nächste Schritte

1. ✅ B5 und B13 behoben (ASI-Hooks `SkyCullPlanes`, `WorldCullVolume`; ASI MD5 `fffd41ab…`).
2. 👤 Bei Gelegenheit B2-Gegenprobe (Map-Lücke, DDS > 1) mit dem neuen Hook.
3. **Phase 2** vorbereiten: 2.1 Kunstentscheidung (👤); Pipeline 2.2 inkl. Atlas-Lücken (B7) und Fahrzeug-Masken.
4. Weiter offen: 2.0b Lauf 2 (HD-Speicher), Alpha-Inhalt-Test, B9 (Engine), 3C.2 (B1–B3) nach Freigabe, B6.
5. **Phase 9:** 9.7 eigene Lichtkarte ✅; Fahrzeug-Seite des Lichts wandert in die Mod LFSP (`LICHT.md` §8.2a); 9.8 Baujahr ✅, 9.9 Lichtfarbe ✅, 9.10 Fernlicht ✅ (9.10f Glow ✅, B23 ✅ upstream).
