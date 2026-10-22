# KAMERA.md – Kamera im Rennen (Phase 11.1 Cinematic, 11.2 Action)

> Themen-Dokument: Herleitung und Umsetzung. Status und Aufgaben stehen in `PLAN.md` (11.1, 11.2).
> Dekompilate liegen lokal in `work/re/cam1.c … cam13.c` (nicht versioniert). Adressen gelten für `Porsche.exe` v3.5.

## 1. Kameratypen und Kamerataste

**Typen** (Namenstabelle der Skriptsprache ab `0x5D6470`, Paare Name/Wert):

| Wert | Name | Wert | Name | Wert | Name |
|---|---|---|---|---|---|
| 0 | BumperCam | 7 | HeliCam2 | 14 | AnimCam |
| 1 | BumperCam2 | 8 | ShotgunCam2 | 15 | ReverseCam1 |
| 2 | TailCam0 | 9 | ShotgunCam3 | 17 | CopCam1 |
| 3 | TailCam1 | 10 | CircleCam | 18 | CopCam2 |
| 4 | TailCam2 | 11 | SplineCam | 19 | CopCam3 |
| 5 | HeliCam0 | 12 | **TVCam** | 20 | DirectorsCam |
| 6 | HeliCam1 | 13 | BlimpCam | 21 | LastCam |

- **Je Typ** steht ab `0x5CF610` ein Eintrag zu 16 Byte: Versatz zum Auto (x rechts, y oben, z vorn; TailCam0 = 0 / 1,15 / −3,3, HeliCam2 = 0 / 4,9 / −6,7) und Flags (`0x5CF61C`), die beim Umschalten in das Flag-Byte des Kamera-Zustands kopiert werden.
- **Liste der Kamerataste:** Skriptvariable `car[N].camera` (Kennung `0x149`), vier Typen je Kamera ab `0x658294`, Abstand `0x580`. Das Menü setzt sie vor jedem Rennen aus dem Profil (`FUN_004eeda0`, Profil `+0x674/+0x67C/+0x680/+0x678`). `fe.txt` enthält den Vorgabewert `BumperCam2, BumperCam, TailCam0, HeliCam2`.
- **Kamerataste C** (`I_CYCLECAMERA`, Kennung `0x206`) → `FUN_00465a60(Kamera)`: Platz `+0xBE` um eins weiter, Typ = Liste[Platz & 3]. In der Wiederholung (Flag-Bit 7) gilt stattdessen die Liste `0x5CF5BC` = 0, 2, 5.
- **Kamera direkt setzen:** `FUN_00465790(Kamera, Typ)` (setzt auch FOV und Zoom-Felder zurück).

## 2. Kamera-Zustand und Ablauf

**Kamera-Zustand** je Kamera ab `0x627E80`, Abstand `0x10C` (Kamera 0 = Spieler):

| Offset | Inhalt |
|---|---|
| `+0x00` / `+0x04` | Zielauto / zweites Auto (Cop-Kameras) |
| `+0x08` | Versatz zum Auto (Verfolger, geglättet) |
| `+0x24` | **Kameraposition** (Welt) |
| `+0x30` | Höhe über Grund (TVCam) |
| `+0x38` / `+0x58` | FOV-Ziel / FOV (halber Winkel in Grad, Vorgabe 37,5) |
| `+0x5C` | Near-Plane-Anteil |
| `+0x7C` | Drehung 3×3 (Zeilen = Kameraachsen in Welt) |
| `+0xBC` | **Typ** (Wort) |
| `+0xBE` | Platz in der Liste der Kamerataste |
| `+0xC0` | Flags (Bit 3 = auf das Auto ausrichten, Bit 4 = Bodenkontakt prüfen, Bit 7 = Wiederholung) |

**Auto-Felder:** `+0x330` Position, `+0x33C` Geschwindigkeit, `+0x364/+0x370/+0x37C` Achsen rechts/oben/vorn, `+0x10E4` Kamera-Element der zuletzt durchfahrenen Zone. Autos: `0x5E8C00[i]`, Anzahl `0x5E8BFC`, Spieler `0x65741C`.

**Ablauf:**
1. Je Simulationstakt `FUN_00463c30`: Position nach Typ (0 `FUN_00461d50`, 1 `FUN_00461a30`, 2–4 `FUN_00461e80`, 5–7 `FUN_004623d0`, 8/9/15 `FUN_004618b0`, 10 `FUN_00462980`, 11 `FUN_00463410`, 12 `FUN_00462e30`, 13 inline, 14 `FUN_00462f30`, 17 `FUN_00463a50`, 18/19 `FUN_00463b60`).
2. Je Bild `FUN_004611a0` → `FUN_00464f80(Kamera, out, Spiegel)`: Position aus `+0x24`, Ausrichtung auf das Auto (`FUN_00464330`, nicht für TVCam), Drehung, dann View-Matrix, `view+0x130` = FOV (`+0x58`). Der Rückspiegel ruft dieselbe Funktion mit Spiegel = 1.
3. `FUN_00466f30` gibt die View an die EAGL-Kamera (`SetPerspective 0x5391B0` mit `view+0x130 × 2`).

## 3. Wiederholungs-Kameras (Grundlage für Cinematic)

- Jede Strecke hat `GameData/Track/<strecke>_cameras.scn` (Text): `CAMERA_ELEMENT` (Position, Drehung, Typ 12 TV oder 11 Spline, FOV, Name `c1` …) und `TRIGGER_ELEMENT` (Zone mit Richtung, Verweis auf die Kamera). Alpen: 56 Kameras, Monaco 1: 15.
- Geladen in Vektoren: Kamera-Elemente `[0x628168]`, Trigger `[0x628174]` (Klassen `CCameraElem`, `CTriggerElem`).
- **Regisseur** `FUN_00413b80(anwenden)`: prüft jeden Trigger gegen jedes Auto (`FUN_004704e0`: in der Zone und in Fahrtrichtung); bei neuer Zone wird `car + 0x10E4` gesetzt und, wenn `anwenden` und das Auto `[0x606858]` ist, mit `FUN_00413960(Element)` die Streckenkamera eingestellt (Typ, Position, FOV, Zoom).
- In der Wiederholung läuft er aus `FUN_00463c30` (Modus `[0x5E99F4] ≥ 2`, `DirectorsCam`).

## 4. Umsetzung in LFSP (v6l, Feinschliff v6n–v6s)

Drei Hooks in `nfs5_enhanced_LHedit/lfsp/lfsp.cpp`, Schalter in `lfsp.ini [Camera]`:

| Hook | Adresse | Zweck |
|---|---|---|
| Kamera-Takt | `0x463C30` | nach dem Original: Regisseur mitlaufen lassen (`FUN_00413b80(0)`, im Cinematic `(1)`), Tasten `CinematicKey`/`ActionKey` |
| Kamerataste | `0x465A60` | Cinematic als fünfte Ansicht: von Platz 3 aus Cinematic statt Platz 0; der nächste Druck geht normal auf Platz 0 |
| Kamera-View | `0x464F80` | Action: nach dem Original Position, Drehung und FOV der fertigen Kamera verändern, dann den Rest der View wie das Original neu aufbauen (`0x532A00`, `0x466280`, `0x466220`, `0x532A40`) |

**Cinematic (11.1):** Beim Einschalten wird die Streckenkamera der zuletzt durchfahrenen Zone gesetzt; danach wechselt der Regisseur der Wiederholung bei jeder neuen Zone. Im Log: `Kamera: Cinematic …`. v6l blieb bis zur ersten Zone auf der Verfolgerkamera (zu Rennbeginn ist `car + 0x10E4` = 0, Element 0 setzt keine Kamera). Seit v6n: Setzt die Zonen-Kamera keine Kamera (`CCameraElem + 0x7C` = 0) oder ist sie über 250 m entfernt, nimmt LFSP die nächste Streckenkamera (Position `CCameraElem + 8`). **Absturz v6n beim Kamerawechsel** (👤, `nfs5_enhanced.log`: Lesezugriff auf `0x64` in `lfsp.asi+0x59D8`, aus der Kamerataste `0x415AF7`): `[0x628168]` zeigt auf den Vektor `{Anfang, Ende, Kapazität}`, v6n las den Vektor selbst als Liste der Elemente. Behoben in v6o.

**Action (11.2):** nur für die Typen in `ActionCams` (Vorgabe 2–4 = Verfolger), nicht im Rückspiegel und nicht in der Wiederholung. Fahrdaten aus dem Auto; Ableitungen nur, wenn sich die Simulationswerte ändern (Bildrate ≠ Simulationstakt):

| Effekt | Messgröße | Wirkung (Vorgabe) |
|---|---|---|
| Kurve | Querbeschleunigung = Längsgeschwindigkeit × Gierrate | Kamera nach außen (`ActionSide` 1,0 m bei 1 g), Auto bleibt zur Innenseite im Bild; Blick um `ActionLook` 7° in die Kurve (v6l 4°, 👤 zu schwach) |
| Rollen | wie Kurve | Kamera rollt zur Kurveninnenseite (`ActionRoll` 2,5° bei 1 g, v6o) |
| Schwung | Querbeschleunigung, allmählich aufgebaut | v6r: Der Kurven-Effekt baut sich über `ActionBuildUp` (1,2 s) auf und wird mit längerem Lenken stärker; Abbau 0,6 s (Tastatur lenkt nur ganz oder gar nicht, 👤: Einlenken zu heftig). Darauf Federn für Seitversatz, Blick und Rollen. Einlenken: 4 rad/s, Dämpfung 1 − 0,3 · `ActionOvershoot` (Vorgabe 0,5), Anstoß 0,3 · `ActionOvershoot` × (Querbeschleunigung − träge Querbeschleunigung, 0,5 s), nur solange die Kurve stärker wird. Zurück zur Mitte: 2,2 rad/s, kritisch gedämpft. Höhenfeder: Dämpfung 0,85 − 0,3 · `ActionOvershoot`. Verlauf: v6o/v6p gummiartig (Anstoß 0,6 auch beim Aufrichten, Dämpfung 0,5), v6q gedämpfter, v6r mit Aufbau |
| Grundhöhe | – | `ActionHeight` +0,25 m über der Kamera des Spiels (v6n, 👤 Wunsch) |
| Bremsen/Aufprall | Verzögerung über 2 m/s² (voll bei 12) | Kamera rückt um bis zu `ActionBrake` 0,8 m heran, schnell hin, langsam zurück |
| Sprung/Kuppe | Fahrzeughöhe und Steiggeschwindigkeit | Kamerahöhe als gedämpfte Feder (7 rad/s, Dämpfung 0,55), höchstens 0,25 m tiefer und 0,8 m höher. v6l folgte nur der Höhe: bergauf hing die Kamera bis 0,8 m zu tief, der Blick nach vorn war verdeckt (👤 Screenshot, Log `Hoehe -0.80` über Sekunden). Seit v6n folgt die Feder auch der Steiggeschwindigkeit; gleichmäßige Steigungen ergeben keinen Versatz. Zittern ab ≈ 200 km/h (👤, v6s–v6x): Der Versatz wurde gegen die nur im Simulationstakt gestufte Fahrzeughöhe gerechnet und sprang je Schritt um Steiggeschwindigkeit × Schrittzeit. Seit v6y wird die Höhe zwischen den Schritten mit der Steiggeschwindigkeit fortgeschrieben |
| Tempo | Geschwindigkeit (ab 8 m/s, voll bei 60 m/s) | FOV bis `ActionFov` +20 % weiter (v6l–v6r: +12 %, linear bis 70 m/s, 👤 zu wenig); Seitversatz, Blick und Rollen bis `ActionSpeedBoost` +35 % stärker (ab 10 m/s, v6s) |
| Unebener Grund | Hochpass der Vertikalbeschleunigung, Schwelle 4 | v6y: leichtes Wackeln erst über der Schwelle (voll bei 40), höchstens 4 cm, ≈ 4–7 Hz, Ausschlag mit 0,3 s geglättet. Bis v6x 6 cm bei 7–13 Hz ohne Schwelle: offroad und bei hohem Tempo (Rauschen) wirkte das bei hohen FPS wie Unschärfe (👤) |
| Aufprall/Landung | Geschwindigkeitssprung zwischen zwei Simulationswerten über 3 m/s (voll bei 12 m/s) | Stoß-Wackeln bis 12 cm und 0,8° Rollen, ≈ 5–8 Hz, klingt mit 0,35 s ab (`ActionImpact`). v6o rechnete Beschleunigung = Sprung / Zeit seit dem letzten neuen Wert und löste ab 25 m/s² aus; Rauschen im Stand und beim Bremsen ergab so ständig Stöße (👤 „zu stark und zu häufig“, Log: `Stoss 0.64` bei `v 0.0`). Seit v6p zählt nur der Sprung selbst, eine Vollbremsung ändert ≈ 0,2 m/s je Schritt; Log `Kamera: Stoss …` je Ereignis. Versetzt das Spiel das Auto um über 20 m (Neustart nach Unfall), beginnen die Federn neu |

**Cockpit-FOV (v6s):** Für die Innenansichten (Typ 0/1, BumperCam/BumperCam2) setzt LFSP nach dem Original `view + 0x130` = `CockpitFov` / 2 (Vorgabe 85° seit v6u; das Spiel nimmt 37,5, also 75°); fester Wert, der FOV-Zuwachs mit dem Tempo gehört nur zur Action-Kamera (👤). Nicht in Cinematic und nicht im Rückspiegel. Die Hor+-Verbreiterung von `nfs5_enhanced` kommt danach noch hinzu.

**Motorgeräusch in Cinematic (`CinematicEngineSound`, Vorgabe an):** Die Fahrzeug-Geräusche hören am Ort der Kamera (`[0x655E50]`); `FUN_004a7660` reicht den Abstand (16.16) an den Motor `FUN_004aa050` und an die Ereignis-/Reifengeräusche `FUN_004a93c0` weiter. Der Motor nimmt bei Typ 12 (TV) nur in der Wiederholung (`[0x5E99F4] ≥ 2`) die weit reichende Kurve 40000 / (Abstand + 15), sonst 2048 / (Abstand + 2); die Reifen bei Typ 12 immer 8192 / (Abstand + 1). Cinematic im Rennen hat Modus < 2, daher war der Motor viel leiser als die Reifen (👤). LFSP ersetzt den Sprung `jl` an `0x4AA083` (`7C 1A`) durch `90 90`; Typ 12 gibt es im Rennen nur durch Cinematic. Welche Funktion der Motor ist, folgt aus dem Aufbau (Drehzahl-/Gasfelder, Dauerton je Auto), 👤 noch nicht im Spiel bestätigt.

**Mindest-FOV Außenansichten (`ReplayMinFov`, Vorgabe 30°):** Der Zoom der Streckenkameras (`FUN_00462b80`) rechnet etwa FOV = FOV aus der `.scn` × Zielabstand / Abstand zum Auto, ohne Untergrenze; weit entfernte Autos füllen dann einen sehr engen Ausschnitt (👤 „man erkennt nichts“). Erste Fassung: LFSP hob im Hook `0x464F80` nur `view + 0x130` auf `ReplayMinFov` / 2. 👤 Screenshot: Die Streckenkameras stehen oft in oder hinter Felsen/Böschungen; das enge FOV hatte das verborgen, das weite zeigte die Geometrie neben der Kamera. Zwischenstand (verworfen, 👤: Schalter `ReplayMinFovDolly` wieder entfernt, fest bleibt „nur FOV weiter“ mit korrigierter Near-Plane): Ist das FOV enger, fuhr die Kamera auf der Linie zum Auto heran (Abstand × tan(FOV/2) / tan(Min/2), mindestens 4 m bzw. der alte Abstand), danach View neu aufbauen (`RebuildView`) und FOV = Mindestwert. Das Auto bleibt so groß wie beim Zoom, die freie Sichtlinie wird genutzt. Gilt für Typen > 1, auch in der Wiederholung (nicht im Menü, nicht im Rückspiegel). 👤 noch nicht getestet. **Ursache der Schnitte (👤 Screenshots: Felsen abgeschnitten, Auto und Straße halb weg, darunter Wasser/Himmel): die Near-Plane.** `FUN_00462b80` setzt beim Zoom neben dem FOV auch `+0x5C` (`[0x627EDC]`) = (37,5 − FOV/2) / (FOV/2), mindestens 0,2 (sonst 0,5); `FUN_00464f80` schreibt das nach `view + 0x18`, `FUN_00466f30` gibt es als Near an `SetPerspective`. Bei FOV/2 = 3° liegt sie bei ~11,5 m. LFSP setzt sie jetzt mit dem Mindest-FOV auf den Wert des Spiels für dieses FOV (30° → 1,5 m) und ruft `FUN_004670f0(view)` (fastcall) wie das Original.

**Offen:** Schaden als Wackel-Quelle (Offset im Auto noch nicht bekannt); Vorzeichen und Stärken bestätigt erst der Testlauf (Log einmal je Sekunde: `Kamera: Typ … quer … laengs …`).
