# VERKEHR.md – Verkehr: Anzahl und Vielfalt (Phase 12)

> Themen-Dokument: Herleitung und Messung. Status und Aufgaben stehen in `PLAN.md`.
> **Stand:** 2026-10-18 · Quelle: Ghidra (`work/re/t1_menu.c`, `t3_dens.c`, `t4_traf.c`, `t6_aitraf.c`–`t9_car.c`, lokal).

## 1. Ablauf vom Menü bis zur Simulation

1. **Menü, Rennaufbau `FUN_004ECF00`** (thiscall, `param_1` = Schreibzeiger der Befehlsliste). Er schreibt die
   Befehle für das Rennen (`id, wert` bzw. `id, slot, wert`, Helfer `FUN_004EDB20`/`FUN_004EDC00`). Diese Liste
   landet nach dem Start als Text in `Game CD/fe_out.txt` (`numcars`, `traffic_density`, `car[N].*`).
2. Je Auto ruft er `FUN_004EEDA0(obj, slot, cursor, typ, name, spielart, …)` auf. **Typ 4 = Verkehr** (Name
   `"Traffic"`, `PTR_005DA6C0`), Typ 8 = Polizei. Die Datei kommt aus `obj + 0x38` (`car[N].filename`, Befehl `0x141`),
   `obj + 0x83` = Spec-Name (`0x13D`).
3. **Spiel, `FUN_004B4DA0`** liest die Befehlsliste in die Globals (`NUMCARS` = `0x658080`, Auto-Beschreibung ab
   `0x65808C`, Abstand `0x580`). `FUN_00411130` legt je Auto ein `Car_tObj` an (Liste `0x5E8B34`).
4. **KI-Aufbau `FUN_00406520`:** je Auto entscheidet das Flag in `car + 0x52C`: `0x04` Mensch, `0x08` Gegner,
   **`0x10` Verkehr (`AIHigh_Traffic`, vtable `0x5B2588`, Konstruktor `FUN_00406490`)**, `0x20` Polizei, sonst `AIHigh_None`.
   `FUN_004068F0` lädt danach `trafcfg.dat` (Fahrphysik-Parameter der KI, keine Anzahl).

## 2. Anzahl

- Einzelrennen (Spielart 0/1): Anzahl = Wert des Menüpunkts **`Track1.Traffic`** (`FUN_004FC0B0`, float → int).
  Turnier/Karriere: feste Tabellen `0x5DB438` / `0x5DB448` / `0x5DB458` je Stufe oder Anzahl der belegten
  Verkehrs-Einträge eines Events (`+0x382`, 7 Plätze). Spielart 6–8: fest 5.
- **Grenze 9 Autos gesamt:** `FUN_004ECF00` kürzt Verkehr auf `9 − Spieler − Gegner − Polizei`. Gegner und Spieler
  zusammen höchstens 8. Die Simulationsliste `0x5E8B34` hat Platz für 9 Zeiger (danach folgt der Zähler `0x5E8B58`).
  Mehr als 9 Autos heißt also: Spiel-Tabellen verlegen (Umfang offen, §5).
- Menüelement `Track1.Traffic` (angelegt in `FUN_004F9790`, Optionstabelle `0x5B9628` = Text-IDs 403/402/401/400 =
  Kein/Leicht/Mittel/Schwer) liefert im Einzelrennen **0/2/5/7** Autos. Turnier/Karriere: Tabellen `0x5DB438`,
  `0x5DB448`, `0x5DB458` = {0, 2, 4, 8}.
- **12.2a (LFSP v7q):** Hook `FUN_004FC0B0` (cdecl, Prolog `81 EC D8 00 00 00`; ECX wird über einen Stub
  durchgereicht). Nur beim Aufruf im Rennaufbau (`0x4ED2B5`, Rücksprung `0x4ED2BA`) und Spielart 0–2 wird 2/5/7 auf
  `lfsp.ini [Traffic] Light/Medium/Heavy` umgerechnet (Vorgabe 2/5/**8**, höchstens 8). Die 9er-Grenze des Spiels
  greift danach unverändert. Der Hook wird nur installiert, wenn die Werte von 2/5/7 abweichen.
- Test (👤 2026-10-18, versehentlich v7p ohne 12.2a): Schwer + 7 Gegner → Spiel kürzt auf 1 Verkehrsauto,
  NUMCARS 9, Rennen stabil.
- `TRAFFIC_DENSITY` (`0x657420`) wird in der Simulation nur als „Verkehr an/aus“ gelesen (`FUN_00415FC0`, `FUN_00410D60`).
- `0x65807C` + Liste an `BWorld + 0x34` sind **bewegliche Streckenobjekte** (Hülle, Masse, Materialtyp), kein Verkehr.

## 3. Modellwahl und Farbe

- Modell-IDs aus dem Strecken-Datensatz `+0x7C` (7 × short). Das i-te Verkehrsauto bekommt **starr `ID[i % 7]`**,
  aufgelöst über `FUN_004F4400` in der Modellliste `*0x6919FC` (unbekannte ID → erstes Modell).
- Farbe: vier Zufallsbytes (`FUN_00538040`) – schon zufällig, auch Alpha.
  - Das Format ist ARGB. Belege: Spielerauto `car[0].colorconfig = -16759914, …` = `0xFF004596` (Dunkelblau);
    beim Verkehr sind alle vier Werte gleich.
  - Die Verkehrsschleife übergibt die Farbe an `FUN_004EE3B0` (thiscall, `ret 0x1C`; Aufruf `0x4ED542`).
  - **LFSP v8b (12.8):** leitet diesen einen Aufruf um. `[Traffic] Colours` = Liste `RRGGBB:Gewicht`; die Vorgabe sind
    Weiß, Schwarz, Silber und Grau mit zusammen 70 %, dazu Rot, Blau, Grün, Beige, Braun und Gelb. Die Helligkeit
    streut um ±6 %. Leer = Zufallsfarbe des Spiels. Gilt auch für Porsches im Verkehr.
  - **v8d:** `[Traffic] ColourRandom` (Vorgabe 10) = Anteil in Prozent, der die Zufallsfarbe des Spiels behält.
- Modelldateien im Spiel: `GameData/CarModel/eas1…eas11`, `eden1…eden5`.
- **Datenquellen im Menü (ohne Code änderbar):**
  - `FEData/Data/nfs5.trk`: 15 Strecken-Datensätze à `0x4A8` Byte; Name `+0x08`, **7 Verkehrs-IDs `+0x7C`**,
    Polizei-Modelle `+0x8A` (5 × short), Anzahl Polizei-Einträge `+0x94`. Im Speicher identisch (Strecke `@…+0x8`).
  - `FEData/Data/nfs5.car`: Fahrzeugmodelle, für Verkehr je Eintrag Anzeigename, Dateiname und **eigene
    Motorstimme** (z. B. EAS10 → `Ton2`, EAS3 → `Tbus`, Eden4 → `Sbus`).
- Menüstufen Verkehr: `IDS_NOTRAFFIC`/`IDS_LIGHT`/`IDS_MEDIUM`/`IDS_HEAVY` (Text 403–400), in `fe_out.txt` als
  `tracktraffic0` = 0…3. **Leicht = 2, Mittel = 5, Schwer = 7 Verkehrsautos** (1 Spieler, keine Gegner; v7n/v7o-Logs 2026-10-18).
- `traffic_density` bleibt in `fe_out.txt` 0, auch mit Verkehr; Verkehrsautos stehen als `car[N]` mit `class = 4`,
  `drivername = Traffic`, `type = 11`, Zufallsfarbe in `colour`/`colorconfig`.

| Strecke | Verkehrsmodelle (+0x7C) | verschiedene |
|---|---|---|
| coastal | EAS10 EAS3 EAS1 Eden4 Eden1 Eden4 Eden4 | 5 |
| Forest | EAS11 EAS10 EAS9 Eden3 Eden5 EAS11 EAS10 | 5 |
| foothills | EAS6 EAS2 EAS8 EAS7 EAS6 EAS2 EAS8 | 4 |
| alps | EAS11 EAS4 Eden1 Eden3 EAS4 Eden3 EAS11 | 4 |
| AutoBahn | Eden4 EAS3 EAS2 EAS7 EAS3 Eden4 Eden5 | 5 |
| castle | Eden3 EAS6 EAS2 Eden5 EAS2 EAS6 Eden3 | 4 |
| farmland | EAS9 EAS10 Eden1 EAS8 EAS10 EAS9 EAS7 | 5 |
| industrial | EAS2 Eden2 EAS6 EAS10 Eden1 EAS2 Eden2 | 5 |
| monaco1–5 | je 4–6 aus EAS1/3/4/6/7/8/10/11, Eden1–5 | 4–6 |
| canyon | Eden4 EAS11 EAS9 Eden5 Eden4 EAS11 EAS9 | 4 |
| skidpad | – | 0 |

`EAS5` (ID 186) steht auf keiner Strecke. **`EAS4` ist der Schneepflug** (Teil `plowh` in `eas4.crp`), vorgesehen
nur auf alps (2×) und monaco2 (1×).

## 4. Messbuild (LFSP v7k, `lfsp.ini [Traffic] Measure = 1`)

Hooks: `0x4ECF00` (Rennaufbau, Gesamtzahl der Autos),
`0x4EEDA0` (je Auto Slot, Typ, Datei), `0x4F4400` (Modell-Lookup), `0x4068F0` (Simulationsautos mit Flags).
Beim ersten Rennaufbau wird zusätzlich die ganze Modellliste geloggt (ID + erste Zeichenkette im Datensatz).
Zeilen beginnen mit `Verkehr:`. Das Spiel verhält sich unverändert.

**v7k stürzte beim Laden des Rennens ab, v7l beim Öffnen der Streckenoptionen** (👤 2026-10-18). Ursache:
`FUN_004ECF00` und `FUN_004FC0B0` enden mit einfachem `ret` (cdecl, Aufrufer räumt auf); Ghidra zeigt beide als
`__thiscall`. Die Hooks räumten wie thiscall Argumente ab (`ret 4` bzw. `ret 0x14`) und zerstörten den Stapel.
(Die erste Vermutung „FPU-Stapel durch `__ftol`“ war falsch.) **v7m:** kein Hook mehr auf `FUN_004FC0B0`;
Rennaufbau über einen Assembler-Stub als cdecl, ECX wird unverändert ans Original gereicht. Geprüft: `FUN_004EEDA0`
endet mit `ret 0x20` (thiscall, 8 Argumente ✓), `FUN_004F4400` und `FUN_004068F0` mit `ret` (cdecl ✓).

**v7m lief** (👤 2026-10-18, coastal, Schwer): Rennen geladen, `fe_out.txt` wie oben. Das `lfsp.log` dieses
Rennens war beim Senden schon vom nächsten Spielstart überschrieben → Log nach dem Rennen vor dem Neustart sichern.

Erste Messung (coastal, Einzelrennen, v7k): 16 Fahrzeugmodelle, ID 186–202 ohne 194: `EAS1`–`EAS11`,
`Eden 1 Traffic`, `Eden2`–`Eden5`. Strecke coastal: Verkehrsmodelle 201 193 191 189 196 189 189
(`EAS10`, `EAS3`, `EAS1`, `Eden4`, `Eden 1 Traffic`, `Eden4`, `Eden4`) → nur 5 der 16 Modelle, `Eden4` dreifach.

## 4a. Zufällige Verkehrsmodelle (12.3a, LFSP v7n)

Hook `FUN_004F4400` erkennt die Verkehrsschleife am Rücksprung `0x4ED4E0` (Aufruf `0x4ED4DB`; der zweite Aufrufer
`0x4ED3F4` ist die Verkehrsliste eines Turnier-Events und bleibt unverändert). Dort liefert LFSP statt `ID[i % 7]`
ein Modell aus einem gemischten Beutel (jedes Modell einmal, dann neu mischen) → keine Doppelten, solange der Pool
reicht. `lfsp.ini [Traffic]`: `RandomModels` (1/0), `Models` = Namensliste (leer = alle 16, Namen wie in `nfs5.car`).
Name und Motorstimme kommen aus dem Modell-Datensatz, passen also zusammen.

Test v7n (👤 2026-10-18, coastal, Mittel und 2× Schwer): Mischung wirkt (z. B. EAS6 EAS5 EAS11 Eden2 EAS4 EAS9 EAS8),
aber der **Schneepflug `EAS4` fuhr auf coastal**. **v7o:** `TrackOnly` (Vorgabe `EAS4`) = Modelle nur auf Strecken,
deren `nfs5.trk`-Eintrag sie selbst enthält. Der Strecken-Datensatz kommt aus ESI an der Aufrufstelle (`0x4ED4D5`:
`mov dx,[esi+edx*2+0x7C]`), gesichert von einem Assembler-Stub vor dem Hook. Der Beutel wird je Strecke neu gefüllt.
✅ 👤 2026-10-18 (v7o, Leicht und Schwer): „sieht alles richtig aus“.

## 4c. Überholen und Ausweichen der Verkehrs-KI

`AIHigh_Traffic::Update` = `FUN_00406020` (vtable `0x5B2588`, Zustand `+0x0C`). Ablauf:
- Zustand 1 wartet (`FUN_0040DE10`, §4d), Zustand 2 fährt (`FUN_0040B5D0`, vtable `0x5B2668`).
- In Zustand 2 prüft `FUN_00405ED0`, ob ein Auto der Liste `0x5E8AC4` (Polizei, Typ-Bits `0x18`) innerhalb von 75 m
  steht. Dann Zufall 0–9: **0 = anhalten**, **1–7 = Spurwechsel** (`FUN_0040CB40`), 8–9 = weiterfahren.
- *Korrektur 2026-10-22:* Das ist nur die Reaktion auf Polizei und Straßensperren, **nicht** das Überholen von Verkehr.

**Überholen** entsteht in der Spurwahl des Fahrzustands. `FUN_0040B5A0` ruft `FUN_00401060` und `FUN_004012B0` auf:
- Es gibt 14 Spuren, 0–6 links und 7–13 rechts (Spurbreiten des Abschnitts `+0x48`/`+0x4C`, Anzahl `+0x38`/`+0x3C`).
  Die aktuelle Spur steht in `car + 0x1020`.
- Für die Spur links, die eigene und die rechte gibt es je eine Wertung (`0x5E66A4`/`A8`/`AC`). Einzelne Regeln ziehen
  davon ab (`FUN_00401A00`, `401F20`, `402200`, `4024F0`, `4026B0`, `401760`, `401B70`, `401C60`). `FUN_004012B0`
  nimmt die beste Spur, `FUN_00402A40` setzt daraus die Querablage `car + 0xEB4`.
- `FUN_00401A00` ist die Regel für die Gegenfahrbahn. Bei `car + 0xEB0` = `0x5E753C` (fest 1) liegt sie auf den Spuren
  < 7, sonst auf den Spuren ≥ 7. Abzug: **−12 für Verkehr** (Flag `0x10`), −4 für andere, nur wenn Verkehr oder Polizei
  vorhanden ist (Wert an `0x401A21`).
- Ein langsameres Auto davor wiegt mehr als −12. Deshalb schert der Verkehr aus, auch ohne schneller zu sein, und fährt
  neben dem anderen her; kommt Gegenverkehr, kracht es (👤 2026-10-22). Mit 15 Verkehrsautos passiert das öfter.
  Wahrscheinlich auch die Ursache von 12.5.

**LFSP v8a:** `[Traffic] Overtake = 0` (Vorgabe) setzt den Abzug auf −1000. Verkehr wählt die Gegenfahrbahn nicht mehr
und kehrt von dort zurück. Auf Straßen mit mehreren Spuren je Richtung bleibt Überholen möglich. `Overtake = 1` = Spiel.
Offen: Ob der Verkehr hinter einem langsameren Auto sauber abbremst, statt aufzufahren.

## 4d. Einsetzen der Verkehrsautos (12.4g)

👤 2026-10-22 (MaxCars 16, Schwer = 15): Verkehrsautos setzen zu viert direkt hinter- und nebeneinander ein.

Ein Verkehrsauto, das aus dem Bild fällt, wechselt in den Wartezustand `FUN_0040DE10` (vtable `0x5B2710`, aufgerufen
aus `AIHigh_Traffic::Update` an `0x4060D0`, `0x40619E`, `0x406411` sowie aus `FUN_00403E40`). Ablauf:
- Wartezeit `car + 0xEE8` = zufällig 1 … `T[N]`, mit `N` = Zahl der Verkehrsautos `0x5E8E24` und Tabelle
  `T` = `0x5CC4A0`: 25, 50, 75, 100, 200, 400, 500, 800, 900, 1000, 1100, 1200 (12 Einträge). Busse `SBUS`/`TBUS`: × 5.
- Die Zeit läuft in `0x40E140` ab, um `0x5E6654` je Takt.
- Bei abgelaufener Zeit prüft `0x40E060`, ob weniger als `A` Verkehrsautos fahren (`N` − Wartende `0x5E747C`).
  `A` = Tabelle `0x5CC480` [`0x657418` + 4 × (`0x657404` = 1)]: 5, 5, 5, 5, 1, 1, 1, 1. Auf zwei Strecken
  (`canyon`, `industrial`) zusätzlich gesperrte Abschnitte.

Folgen:
- **Pulks ab 12 Verkehrsautos:** Hinter `T` liegen zwei Floats `255.0` und dann `0xA`, `0x5`, `0x7`, `0xC` …
  Mit `N` = 15 ist die Wartezeit 1 … 12 Takte. Alle wartenden Autos sind fast sofort bereit und setzen nacheinander
  an derselben Stelle ein.
- **Gleichzeitig fahren im Spiel höchstens 5 Verkehrsautos**, egal wie viele es gibt. Mehr Verkehrsautos füllen
  nur die Lücken schneller.

**LFSP v7y:**
- Bei MaxCars > 9 zeigt der Operand von `IMUL EAX,[ECX*4+0x5CC4A0]` (`0x40DEA0`) auf eine eigene Tabelle:
  die 12 Werte des Spiels, danach +100 je Auto (1300 … 1700). Mit 15 Autos werden das 1 … 1600 Takte,
  so dicht wie das Spiel mit 8 Autos (900/8 ≈ 1600/15).
- `[Traffic] Active` (Vorgabe 5) ersetzt die 5er-Einträge von `A` (`MOV EDI,[EDX*4+0x5CC480]` an `0x40E09C`).
  Das ist ungetestet: Ob die KI mehr als 5 gleichzeitig fahrende Verkehrsautos verträgt, muss der Test zeigen.
- Offen: Was `0x657418` und `0x657404` sind, Vermutung Schwierigkeit bzw. geteilter Bildschirm.

## 4e. Porsches im Verkehr (12.6)

Wunsch 👤 2026-10-22: Spielerfahrzeuge auch als Verkehr.

**Herleitung:** `FUN_004F3E40` sortiert alle Fahrzeuge aus `nfs5.car` (Vektor `*0x6919E8`, ein Datensatz je Fahrzeug,
ID `+0`, Name `+2`) nach der Art bei `+0x1F4`:
- 0 = Porsche: in `*0x6919EC` (Händler, abhängig von `+0x204`), `*0x6919F0` (nur wenn die `.crp` vorhanden ist),
  `*0x6919F4` und `*0x691A00` (Jahr `+0x1F8`/`+0x202` passt zur Ära).
- 1 = Verkehr: in `*0x6919FC`, Suche `FUN_004F4400`.
- 2 = Polizei: in `*0x6919F8`, Suche `FUN_004F43A0`.

Im Rennaufbau (`FUN_004ECF00`) gehen Polizei und Verkehr denselben Weg: `FUN_004EE3B0` (Auto-Objekt `0x760` aus Datensatz +
Farben), dann `FUN_004EEDA0` mit Klasse 8 (Polizei) bzw. 4 (Verkehr). Die Polizei fährt also schon Porsche-Datensätze
über diesen Weg. Die Klasse, nicht das Modell, bestimmt die KI (`AIHigh_Traffic`).

**LFSP v7z:** `[Traffic] Porsches` = Anteil in Prozent (Vorgabe 0). An der Verkehrsschleife (`0x4ED4E0`) ersetzt der Hook
das Modell mit dieser Wahrscheinlichkeit durch einen Porsche aus `*0x6919F0` (Art 0, Beutel ohne Doppelte). Die Farbe
bleibt die Zufallsfarbe des Verkehrs. Log: `Verkehr: Porsche '<Name>' (ID n) als Verkehrsauto`.

Offen bzw. zu prüfen:
- Ladezeit und Speicher, weil Porsche-Modelle größer sind als Verkehrsmodelle.
- Ob die Zufallsfarbe zu Porsche-Lacken passt.
- Fahrverhalten der Verkehrs-KI mit Porsche-Fahrdaten.
- Ära-Filter (`+0x1F8`/`+0x202`), falls Oldtimer und Neuwagen nicht gemischt werden sollen.

## 4b. Verkehr auf der falschen Straßenseite (12.5, verworfen: vermutlich Ausweichmanöver, 👤 2026-10-18)

👤 2026-10-18 (v7n, Screenshot): blaues Verkehrsauto fuhr auf der falschen Straßenseite. Im Spiel gilt Rechtsverkehr;
Verkehr fährt in beide Richtungen. Die Spur hängt nicht am Modell: die Startspur rechnet `FUN_00410580` aus
Startplatz (`0x658200[idx·0x160] & 1`) und Fahrtrichtung (`car + 0xEAC`). Offen: Gegenverkehr auf seiner rechten Spur
(normal) oder echter Geisterfahrer; erst beim Start oder nach Kollision; auch mit `RandomModels = 0`?

## 5. Offene Fragen (Messung)

1. Wie viele Verkehrsautos ergeben Leicht und Mittel? (Schwer = 7)
2. ~~Modell-IDs je Strecke~~ → §3 (`nfs5.trk`).
3. ~~Mehr Modelle als je Strecke?~~ Ja: 16 Modelle, je Strecke 4–6 → §4a.
4. ~~Wie viele Tabellen hängen an der 9er-Grenze?~~ → §6.

## 6. Machbarkeit: mehr als 9 Autos (12.4)

Wunsch 👤: mehr als 9 Fahrzeuge, später auch mehr Gegner. Quelle: Operanden-Liste aller Code-Zugriffe auf
globale Daten (`GhidraNFSP/DumpGlobalRefs.java` → `work/re/globalrefs.csv`, 32 786 Zugriffe; `imm89.csv` für
Konstanten 8/9).

### 6.1 Was an der Grenze hängt

| Bereich | Adresse | Größe | Zugriffe / Funktionen | Bemerkung |
|---|---|---|---|---|
| Auto-Listen der Simulation (Zähler + 9 Zeiger, hintereinander) | `0x5E8AC4–0x5E8E50` | 9 je Liste, ≥ 10 Listen (`0x5E8AC4`, `…AEC`, `…B10`, `…B34`, `…B60`, `…B88`, `…BAC`, `…BD4`, `…C00`, `…E2C`) | 423 / 107 | Kern; `0x5E8B34` = alle Autos, `0x5E8E2C` = Rennteilnehmer (84 Zugriffe) |
| KI-Objekte je Auto | `0x5E6774` | 9 | 23 / 11 | |
| Auto-Beschreibungen (aus der Befehlsliste) | `0x65808C`, Abstand `0x580` | 9 (Block endet genau bei `0x65B20C`, Löschen `FUN_004B4DA0` mit Länge `0x3E24`) | 106 / 16 | gut verlegbar: liegt am Blockende |
| Rangliste/Fortschritt der Rennteilnehmer | `0x608B38`, Abstand `0x10` | **8** | 35 / 9 | betrifft Gegner, nicht Verkehr |
| weitere 8er-/9er-Tabellen (Kandidaten) | `0x608AF4`, `0x608B14`, `0x60FDA8`, `0x690AB0` (Menü), `0x691D98` (Menü) | 8/9 | je 2–10 | noch zuzuordnen |
| feste Grenzen im Code | 30 der 125 Funktionen mit Auto-Tabellen enthalten `8`/`9` als Konstante | – | – | viele sind andere Bedeutungen (Spielart 9, Matrix-Kopie mit 9 Dwords); einzeln prüfen |
| Menü | `FUN_004ECF00`: Gegner ≤ 8 − Spieler, Summe ≤ 9 | – | – | 2 Vergleiche |
| Auto-Index | `car + 0` & `0x7F`, `car + 0x520` | 7 Bit | – | reicht bis 127 |
| Startaufstellung | `FUN_00410580` | Formel aus Position und Anzahl, keine Tabelle | – | unkritisch |

Die Auto-Objekte selbst (`Car_tObj` 0x1254 Byte, Teile, Specs) holt das Spiel vom Heap; daran hängt keine Grenze.

### 6.2 Weg: „Limit-Adjuster“ in LFSP

Bekannte Technik aus der Modding-Szene: Tabellen beim Start in größeren eigenen Speicher verlegen und jede
absolute Adresse im Code umschreiben (Ghidra liefert alle Stellen; LFSP prüft jede Stelle vor dem Schreiben gegen ihre
Bytes, wie bei den Hooks). Dazu feste Grenzen (8/9) im Code anheben.

- Verlegen statt erweitern, weil hinter jeder Liste sofort die nächste liegt.
- Die 9er-Listen werden nur über `[idx*4 + basis]` bzw. `mov reg, basis` angesprochen, nicht über einen gemeinsamen
  Zeiger → jede Liste einzeln verlegbar.
- Neue Obergrenze als INI-Wert (`[Traffic] MaxCars`, Vorschlag 16).

### 6.3 Einschätzung

- **Machbar: ja**, aber das größte Vorhaben bisher. Nur statisch ist nicht sicher, dass alle Tabellen gefunden sind;
  fehlende zeigen sich als Absturz oder Fehlverhalten ab Auto 10 → Vorgehen in Stufen mit Testfahrt.
- **Verkehr > 9** (Stufe B) braucht die Simulations-Listen, KI-Zeiger, Beschreibungen und die Menügrenze.
  Verkehr zählt nicht in Rangliste, HUD-Position und Ergebnis.
- **Gegner > 7** (Stufe C) zusätzlich: 8er-Ranglisten (`0x608B38` u. a.), Positionsanzeige, Ergebnisliste und
  Rekorde im Menü (`gimme.dll`/FE-Daten), Aufholhilfe, Spielstände. Mehrspieler bleibt bei 8.
- Unbekannt, erst im Test sichtbar: Ton (Motorstimmen je Auto), Wiederholung (Replay-Format je Auto), Leistung bei
  vielen Autos (KI, Physik, Zeichnen).

### 6.4 Stufen

| Stufe | Inhalt | Test 👤 |
|---|---|---|
| A | Verkehr bis zur 9er-Grenze frei wählbar (nur Menü) | Strecke mit 8 Verkehrsautos |
| B | Limit-Adjuster: Simulations-Listen, KI-Zeiger, Beschreibungen auf `MaxCars`, Menügrenze; zuerst nur Verkehr | 12, dann 16 Autos; Rennen, Wiederholung, Neustart |
| C | Ranglisten, HUD, Ergebnis und Menü für mehr Gegner | 1 Spieler + 11 Gegner |

### 6.5 Inventar für Stufe B (nur Verkehr), Stand 2026-10-21

Werkzeuge: `GhidraNFSP/DumpListing.java` (ganze Disassembly → `work/re/listing.tsv`), `DecompileAll.java`
(→ `work/re/all.c`), `DumpGlobalRefs.java` (Operanden in einem Adressbereich → `globalrefs.csv`), Rohsuche aller
Dwords in `.text`/`.data`/`.rdata` (deckt Code außerhalb von Ghidra-Befehlen und Datenzeiger auf).

**Listen-Block `0x5E8AB8–0x5E8E50`** (aus `FUN_0040F160`, legt jedes `Car_tObj` in Listen ab):

| Liste | Zähler | Inhalt | wächst mit Verkehr |
|---|---|---|---|
| `0x5E8B34` | `0x5E8B58` | alle Autos (Kopien `0x5E8B88`, `0x5E8BAC`, `FUN_004110A0`) | **ja** |
| `0x5E8AEC` | `0x5E8E28` | alle KI-gesteuerten (Gegner, Verkehr, Polizei) | **ja** |
| `0x5E8B60` | `0x5E8E24` | Verkehr | **ja** |
| `0x5E8C00` / `0x5E8B10` | `0x5E8BFC` / `0x5E8AB8` | Spieler | nein |
| `0x5E8E2C` | `0x5E8AE8` | Rennteilnehmer | nein (≤ 8) |
| `0x5E8BD4` | `0x5E8BF8` | Gegner | nein |
| `0x5E8AC4` | `0x5E8ABC` | Polizei | nein |
| `0x5E8C24` | `0x5E8B5C` | Streckenobjekte (128) | nein |

Alle Zugriffe sind `[idx*4 + liste]` oder `mov reg, liste`; kein Schleifenende als Adresse; Rohsuche: alle 447
Vorkommen liegen in Ghidra-Befehlen, keine Datenzeiger. `car + 0x520` = laufender Index aller Objekte (`0x5E8BD0`),
für Autos = Platz in `0x5E8B34`.

**Weitere Tabellen pro Auto:**

| Tabelle | Größe | Verweise | Bemerkung |
|---|---|---|---|
| KI-Objekte `0x5E6774` | 9 × 4 | 23 | `FUN_00406520` u. a. |
| Auto-Beschreibungen `0x65808C` | 9 × `0x580`, Ende des Rennzustands-Blocks `0x6573E8–0x65B20C` | 106 Code (+ Befehlstabelle `0x5D1E40`, deren Auto-Einträge der Code ab `0x4B0FF5` mit Direktwerten füllt) | Rennzustand wird gelöscht (`PUSH 0x3E24` in `FUN_004B4DA0`, `FUN_004B6A50`) und für Wiederholung/Neustart nach `0x5E9A20` kopiert und zurück (`MOV ECX,0xF89; REP MOVSD` bei `0x413DB6`, `0x413E7A`, `0x413FF1`, `0x414687`); die Wiederholungsdatei schreibt `0x1CE28` Byte ab `0x5E9A20` |
| Ton je Auto `0x6563C8` | **8** × 4 (gelöscht mit `MOV ECX,8` bei `0x4A876E`) | `[idx*4+…]` in `FUN_004AA050` | Achtung: `CMP ESI,0x6563C8` in `FUN_004A88A0`/`FUN_004AB…` ist das **Ende** der Tabelle `0x656180` und bleibt |
| Menü-Grenze | `CMP ECX,9` `0x4ED389`, `MOV EAX,9` `0x4ED38E` | – | Gegner-Grenze 8 (`0x4ED00D`/`0x4ED012`) erst Stufe C |

**Geprüft und nicht betroffen:** Ranglisten `0x608B38` (8, nur Rennteilnehmer), Ton-Kanäle `0x65686C` (6 = 4 + 2
Spieler), Ton-Stimmen `0x655F50`, `0x5E67C0`/`0x5E6B08` (5 Plätze `% 5`), Steuerung `0x5E9400…` (je Spieler),
Netzwerk `0x608B14`/`0x606AF0` (je Netzwerkspieler bzw. Objekt `& 0x7F`), Konstanten 8/9 in 30 Funktionen
(Fallunterscheidungen, Spielart 9, 9-Dword-Matrizen, 8 Gänge/Räder, Kamera-Zähler `% 9` in `FUN_00414850`).
Menü-Befehlspuffer `0x6E20` Byte (`FUN_004ED5D0`), ≈ 1 KB je Auto → reicht für ~24 Autos.

**Fremde Leser fester Adressen:** `nfs5_enhanced` Interpolation (`0x5E8B34` mit Kapazität 9, `interpolation.cpp`),
Breitbild (`0x657A48…`, Rennzustand), dx11z (`0x5E8B34`, `NUMCARS` auf 9 gekürzt, Spielerindex `0x65741C`),
LFSP selbst (`0x5E8B34`, Kameraliste `0x658294` = Beschreibung 0).

### 6.6 Umfang Stufe B1

`lfsp.ini [Cars] MaxCars` (9 = aus). Verlegt werden nur Tabellen, die mit Verkehr wachsen; der Rennzustand bleibt
an seinem Platz (Breitbild des Mods, Spielerindex), nur die Beschreibungen ziehen aus:

1. Listen `0x5E8B34`, `0x5E8B88`, `0x5E8BAC`, `0x5E8AEC`, `0x5E8B60` → je `MaxCars` Plätze.
2. KI `0x5E6774`, Ton `0x6563C8` (+ Löschlänge) → `MaxCars`.
3. Beschreibungen `0x65808C…0x65B20C` → `MaxCars × 0x580`; beim Löschen des Rennzustands mit löschen; an den 4
   Kopierstellen zusätzlich in einen eigenen Puffer sichern bzw. zurückholen (Wiederholung im Speicher, Neustart).
   **Gespeicherte Wiederholungsdateien** enthalten nur die ersten 9 Beschreibungen (Grenze von B1).
4. Menü-Grenze 9 → `MaxCars`, `[Traffic] Heavy` bis `MaxCars − 1`.
5. Export `LfspCarList` für dx11z und `nfs5_enhanced` (Interpolation), die die Liste aller Autos selbst lesen.

Erzeugt wird die Liste der Code-Stellen offline (`nfs5_enhanced_LHedit/lfsp/gen_carlimit_table.py` → `carlimit_table.h`);
LFSP prüft jede Stelle gegen den erwarteten Wert und schreibt erst, wenn **alle** passen.

### 6.7 Umsetzung 12.4a (LFSP v7t)

- `carlimit_table.h`: **221 Stellen** (alle Autos 62, Kopien 9 + 6, KI-Liste 6, Verkehrsliste 1, KI-Objekte 23,
  Ton 8, Beschreibungen 106). Ausgelassen: 7 × `CMP reg,0x6563C8/0x6563CC` (Ende der Tabelle `0x656180`).
  Rohsuche über `.text`: kein unerfasstes Vorkommen außer Bytes relativer Sprünge/Aufrufe.
- Sonderstellen: Menügrenze `0x4ED38B`/`0x4ED38F` → `MaxCars`; Löschlänge Ton `0x4A876F` → `MaxCars`;
  `MOV ECX,0xF89` bei `0x413E7A`, `0x413FF1`, `0x414687` → `CALL DescSaveStub`; `0x413DC5` (12 Byte) →
  `CALL DescRestoreStub` (führt `MOV [0x5E99FC],0` / `REP MOVSD` selbst aus); `PUSH 0x3E24` bei `0x4B4DAA`,
  `0x4B6AB3` → `CALL DescClearStub` (legt `0x3E24` unter die Rücksprungadresse). Stubs sichern Register und Flags.
- Befehlstabelle `0x5D1E40…0x5D4000`: beim Einbau schon eingetragene Beschreibungs-Adressen werden umgerechnet.
- Hook `FUN_0040F160` (cdecl, Prolog `53 55 8B 6C 24 0C`): spiegelt die ersten 9 der verlegten Liste aller Autos
  nach `0x5E8B34`, damit dx11z und die Interpolation von nfs5_enhanced (lesen dort höchstens 9) weiterlaufen.
  Autos ab Nr. 10 werden dort nicht interpoliert bzw. nicht beleuchtet, bis beide `LfspCarList` nutzen.
- Export `LfspCarList(void *** list)` → Kapazität, `*list` = aktuelle Liste aller Autos.
- `[Traffic] Light/Medium/Heavy` jetzt bis `MaxCars − 1`.
- Wird `MaxCars > 9` gesetzt, ändert LFSP sonst nichts am Spiel, wenn eine Stelle nicht passt (Log
  „Autos: … Abweichungen, MaxCars bleibt 9“).

**Test 12.4a (👤 2026-10-21, MaxCars 12, Schwer = 11):** Verlegung aktiv (221 Stellen), 12 Autos im Rennen, Absturz
nach den ersten Bildern: `0x406111` in `AIHigh_Traffic::Update` (`FUN_00406020`), Zustandsobjekt `this + 8` = NULL.
Ursache: **Pool der KI-Zustände** mit 10 Plätzen — `FUN_0040B530` vergibt Zustände (0xA0 Byte) ab `0x5E6E38` mit
Belegt-Flags ab `0x5E7484` (Schleifenende `CMP EAX,0x5E74AC`), Freigabe `FUN_0040B570` u. a. (`CMP reg,0x5E7478`),
Löschen `FUN_0040B490`/`FUN_0040B4B0` (`MOV ECX,0xA`). Bei 12 KI-Autos ist der Pool leer → NULL. Er stand in der
Kandidatenliste (10 Einträge), wurde aber verworfen, weil er nicht über den Auto-Index angesprochen wird.

**12.4b (LFSP v7u):** Pool verlegt (`AI_STATES`, `AI_STATE_USED`, je **2 × MaxCars** Plätze wegen Zustandswechsel
mit Neuanlage vor Freigabe); Generator kennt jetzt Elementgröße, Faktor und **Schleifenenden** (`CMP reg,Ende` →
neues Ende; `0x5E7478` und `0x5E74AC` sind daneben eigene Variablen und bleiben). 242 Stellen.
Nachsuche: alle Schleifen über feste Adressbereiche in KI/Simulation (`0x401000–0x420000`, `0x498000–0x4A2000`) —
übrig nur `0x5E66BC` (24 Plätze, Ideallinien-Datensätze je Strecke, nicht je Auto), Steuerung, Netzwerk,
5er-Plätze, 8er-Rangliste.

**Test 12.4b (👤 2026-10-21, v7u, MaxCars 12):** Rennen läuft. Absturz beim **Beenden über das ESC-Menü**:
`0x40AAA5` (`FUN_0040AAA0`, aus der Abbau-Schleife `FUN_00406B40` über die Liste aller Autos), Auto = NULL.
Ursache: `FUN_004B68F0` (Rennende) kopiert für jedes Auto bis `NUMCARS` ein **Ergebnis** (0x96 Dwords = 0x258 Byte)
nach `0x5E75A0 + i × 0x258`; die Tabelle hat 9 Plätze und endet genau an `0x5E8AB8` → Platz 10–12 überschrieb die
Zähler des Listen-Blocks (u. a. `0x5E8B58`). Die Schleifensuche hatte die Stelle übersehen, weil der Decompiler
`NUMCARS` dort in einem Komma-Ausdruck liest.

Außerdem fehlte `.data` unterhalb `0x5D0000` im Inventar (`globalrefs.csv` begann bei `0x5D0000`): neu
`globalrefs_low.csv` (`0x5B2000–0x5D0000`). Dort pro Autoplatz beschrieben nur `0x5CD158` (Blinkfarbe der
Kartenpunkte, `FUN_00433110`); der Rest sind konstante Tabellen (KI-Zustände, Detailstufen).

**12.4c (LFSP v7v):** zusätzlich `RESULTS` `0x5E75A0` (9 × 0x258), `BLINK_TIME` `0x60FDA8`, `BLINK_COLOR`
`0x5CD158` (je 9 × 4). Generator: liest mehrere Verweislisten, nur Code aus `.text`/`PACODE`, Sprungziele zählen
nicht; jedes `CMP reg,Bereichsanfang` gilt als Ende der Tabelle davor (hier `0x4068C5`: Kehrwerte `0x5E7554`).
**260 Stellen.** Gegenprobe auf Befehlsebene: alle indizierten Zugriffe in den 37 Funktionen, die `NUMCARS` oder
`0x5E8B58` lesen — außerhalb der verlegten Bereiche nur bekannte, nicht wachsende Tabellen.

**Test 12.4c (👤 2026-10-21, v7v):** Absturz einige Sekunden nach Rennstart in `FUN_004A93C0` (Ton), `0x4A9747`:
`MOV EDI,[ESI*4 + 0x5E8AEC]` mit `ESI = 0x12`. **Gefaltete Basis:** gemeint ist `liste_aller_autos[ESI − 0x12]`
(Spieler-Autos 0/1 für die Ton-Kanäle 0x12/0x13); der Compiler hat daraus `0x5E8B34 − 0x48 = 0x5E8AEC` gemacht,
und das ist zufällig der Anfang der KI-Liste. Der Generator ordnete die Stelle deshalb der KI-Liste zu.

**12.4d (LFSP v7w):** Generator kennt feste Zuordnungen `FOLDED` (Operand-Adresse → Bereich): `0x4A9593`,
`0x4A9743`, `0x4A9813` → Liste aller Autos. LFSP rechnet jede Stelle relativ zum zugeordneten Bereich (Abstand darf
negativ sein). Suche nach weiteren gefalteten Basen: indizierte Zugriffe mit Konstante bis 0x40 Elemente vor
einem verlegten Bereich oder mit Indexprüfung außerhalb 0…8 — außer diesen drei nur eigene Tabellen
(`0x5E8B10`, `0x5E8AC4`, Kehrwerte `0x5E7550`, Ton-Slots `0x656180` mit verschobenem Index).

✅ **Test 12.4d (👤 2026-10-21, v7w, MaxCars 12, Schwer = 11, 0 Gegner):** zwei Rennen mit 12 Autos (Log:
„260 Code-Stellen, 11 Sonderstellen“, NUMCARS 12), Zieleinfahrt und Abbruch über das ESC-Menü ohne Absturz.
Offen: 16 Autos, Wiederholung/Neustart gezielt prüfen, dx11z/Interpolation für Autos ab Nr. 10 (`LfspCarList`),
dann Stufe C (Gegner).

**12.4e (LFSP v7x): gespeicherte Wiederholungen.** Die Datei ist `0x1CE28` Byte ab `0x5E9A20` (Rennzustands-Kopie
mit 9 Beschreibungen + Aufzeichnung); `FUN_004146F0` liest sie in voller Dateigröße dorthin zurück, eine
Verlängerung der Datei ginge also über den Puffer hinaus. Stattdessen Begleitdatei
`lfsp_replays\<FNV-1a der ersten 0x3E24 Byte>.bin` neben `lfsp.asi` (Kopf `LFSP`, MaxCars, dann Beschreibungen ab
Auto 10 aus dem Schattenpuffer). Hooks: `FUN_0059E170` (Datei schreiben, cdecl `(name, puffer, länge)`, Prolog
`8B 44 24 04 81 EC 04 01 00 00`; wirkt nur bei Puffer `0x5E9A20`) und `FUN_004146F0` (Wiederholung laden, cdecl,
Prolog `83 EC 54 A1 60 B3 65 00`). Beim Laden mit `NUMCARS > 9` ohne passende Begleitdatei kürzt LFSP `NUMCARS` in
der Kopie auf 9 (sonst leere Beschreibungen).

✅ **Test 12.4e (👤 2026-10-21, v7x, MaxCars 16, Schwer = 15):** Rennen mit 16 Autos, Wiederholung im Rennen,
Wiederholung speichern und aus dem Menü laden, Neustart – alles ohne Absturz; in `lfsp_replays\` liegen zwei
Begleitdateien. Offen: dx11z/Interpolation für Autos ab Nr. 10 (`LfspCarList`), Stufe C (Gegner).

**12.4f: dx11z und nfs5_enhanced lesen die verlegte Liste.** Beide lasen die Liste aller Autos fest an `0x5E8B34`
mit 9 Plätzen; LFSP spiegelt dorthin nur die ersten 9. Autos ab Nr. 10 hatten deshalb keine Interpolation
(ruckeln bei mehr Bildern als Simulationsschritten) und kein Scheinwerfer-/Rücklicht-Leuchten. Die zweite Liste
`0x5E8C24` (Zähler `0x5E8B5C`, in nfs5_enhanced `CAR_LIST_TRAFFIC`) ist **nicht** der Verkehr: `FUN_0040F160`
trägt dort nur Objekte mit Flag `0x80` ein, die gerade nicht in der Liste aller Autos stehen.
- dx11z: `RacerList`/`RacerCar`/`RacerCount` in `driver.cpp` fragen `LfspCarList` (6 Lesestellen, `NUMCARS`
  `0x658080` begrenzt auf die Kapazität statt auf 9). Ohne LFSP bleibt es bei `0x5E8B34` und 9 Plätzen.
- nfs5_enhanced: `Interpolation::CaptureAll` erfasst die Rennautos über `LfspCarList`. Im Log steht einmal
  „Interpolation racer list from LFSP found“.
- Der Spiegel in LFSP bleibt für ältere dx11z/nfs5_enhanced.
- Grenze: dx11z zeichnet das Scheinwerferlicht von höchstens 7 anderen Autos (`MAX_CAR_LIGHTS`) und höchstens
  16 Rücklicht-Scheine (`MAX_REAR_LIGHTS`), jeweils die ersten in der Liste.
- Für Stufe C vormerken: die Polizei-Liste `0x5E8AC4` (Zähler `0x5E8ABC`) hat 9 Plätze und ist nicht verlegt.
