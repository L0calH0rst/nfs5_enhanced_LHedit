# FENSTER-VERDECK.md – Seitenscheiben und Verdeck im Rennen umschalten

> Themen-Dokument: Herleitung und Machbarkeit für Phase 8. Status und Aufgaben stehen in `PLAN.md`.
> **Quellen:** `Mod-Window Opener/` (Birtz v1.0 auf Basis von Sniper „Windows Down“), `Game CD/GameData/CarModel/*.tpg`, Ghidra-Export `porsche.exe`. Analysiert 2026-10-16.

## 1. Was der vorhandene Patch macht

- `install.exe` / `wopener.exe` kopieren nur `.tpg`-Dateien: `GameData/owindow/*.tpg` (Fenster offen) oder `cwindow/*.tpg` (= Original, Fenster zu) nach `GameData/CarModel/`. Es gibt keinen Code-Eingriff.
- Betroffen sind 11 Fahrzeuge: `356_1`, `356a`, `356b`, `901`, `914`, `944`, `964`, `993`, `996`, `boxster`, `fboxster`.
- Pro offenem Style hängt der Patch in `[styleN]` zwei Einträge an: `geometry = 35` (Seitenscheiben außen) und `geometry = 47` (innen), jeweils mit einem `type`. `count` wird um 2 erhöht.
- Bekannte Fehler laut `Windows Down.txt`: Beim 914 1.7 gehen die Scheiben nicht herunter. Beim 944 verschwindet die äußere Frontscheibe. Der 99er 911 Targa bekommt ein Glasdach.

**`type` je Fahrzeug und Style (Patch-Stand):**

| Fahrzeug | Styles | `type` für 35 / 47 |
|---|---|---|
| `356_1` | Cabrio | 1 / 1 |
| `356a` | alle Cabrio/Speedster (s1–s6, s11, s12) | 3 / 3 (im Original teils schon 35/47 mit anderem Wert, z. B. s6: 2 / 1) |
| `356b` | Roadster, Cabrio (s1–s3, s11–s13) | 1 / 1 |
| `901` | Targa (s3, s4, s6) | 1 / 1 |
| `914` | s1 1.7 / s2, s3 | 3 / 3 bzw. 1 / 1 |
| `944` | cabrioturbo, S2cabrio (s7, s8) | 3 / 3 (Original hat 35/47 schon) |
| `964` | Cab und Targa (s1, s3, s4, s6) | 1 / 1 |
| `993` | s2, s3, s6 | 1 / 1 |
| `996` | s1, s3 | 1 / 1 |
| `boxster`, `fboxster` | s1, s2 / s3 Bat | 1 / 1 bzw. nur 47 = 1 |

## 2. Wie die Engine `[styleN]` auswertet (`porsche.exe`)

- **Parser `FUN_00437b00`** (Auto-Objekt, Style-Nummer): löscht eine Tabelle mit 120 Einträgen (`0x1E0` Byte) am Fahrzeug-Objekt `+0x56C`. Dann setzt er für jeden Eintrag `Tabelle[geometryN] = typeN`. Ein `textureN` landet bei Index `0x3C + N`. Ein späterer Eintrag überschreibt einen früheren, deshalb funktionieren die angehängten Patch-Einträge auch dort, wo das Original 35/47 schon kennt.
- **Geometrie `0x43C1B0`** (Ghidra-Lücke; Render-Schicht, vtable `0x5B36AC` Slot 1, Argument `.crp`): Die Funktion räumt die alte Liste ab (`0x43E0C0`). Dann durchläuft sie alle Teile-Einträge (Kennung `Atri`) der `.crp`. Ein Teil kommt in die Liste, wenn `Tabelle[Teil] == Variante` (Kopf bei Teil `+0x44`: Byte 0 Teil, Byte 1 Variante, Byte 9 Mesh-Animation) und es die Schicht-Id (Schicht `+8`) enthält. Für Teile mit Mesh-Animation setzt sie die Bildanzahl (`FUN_004381b0`) und Bild 0. Teile, die der Style nicht nennt, haben Variante 0. Die Schicht hält bei `+4` das Modell, `+0xC`/`+0x10` die Liste.
- **Textur-Teile `FUN_00447c50`** (erst für die Draw-Liste gehalten, Korrektur 8.3b): filtert mit derselben Tabelle die Bildteile aus `<Auto>D.fsh`/`<Auto>L.fsh` (`+0x90`/`+0x94`). `FUN_00448010` zeichnet sie in die Textur-Seiten. Ein Neuaufbau ändert keine Geometrie.
- **Folgerung:** `type` ist kein Schalter „an/aus“, sondern die **Mesh-Variante** des Teils. Bei Teil 35/47 ist eine Variante die heruntergelassene (oder fehlende) Scheibe. Welche Nummer das ist, hängt vom Fahrzeug ab (1 oder 3). Das erklärt die Patch-Fehler beim 914 1.7 und 944: dort passt die gewählte Variante nicht.
- **Im Rennen** wird die Liste einmal beim Aufbau gebaut: `FUN_00410d60` → `FUN_00443db0(Style = Konfig + 0x514)` → `FUN_00447c50(…, Fahrzeug + 0x56C)`.

**Machbarkeit Fenster:** Live-Umschalten heißt: `Tabelle[35]` und `Tabelle[47]` zwischen Original- und Offen-Wert tauschen und `0x43C1B0` für jede Render-Schicht des Modells erneut aufrufen. Das Plugin merkt sich die Schichten, indem es `0x43C1B0` hakt (§6.5).

## 3. Verdeck (Cabrio)

- Das Menü schaltet das Verdeck über den Frontend-Accessor `Car.Roof` (`FUN_004bfc90`, `FUN_004bffd0`). Dieser ruft **`FUN_004d1800(Wert)`** auf. Der setzt `Konfig + 0x510 = Wert` und `Fahrzeug + 0x594 = (Wert ≥ 0)`. Danach lädt `FUN_00442d50` **alle Textur-Seiten neu** und baut die Draw-Liste neu.
- Das Verdeck ändert also neben der Geometrie wohl auch Texturen (z. B. `cabrio.fsh`, Innenraum). Im Rennen wäre ein Umschalten deshalb teurer: kurzes Ruckeln beim Nachladen ist möglich.
- Ans Rennen geht der Zustand vermutlich als Fahrzeug-Schlüssel `.CABRIODOWN` (String bei `0x5D3CA8`, neben `.BODYSTYLE`, `.PASSENGER`). Der Verweis steht bei `0x4B4A18` in keiner von Ghidra erkannten Funktion (Analyselücke, vgl. `GHIDRA.md` §3). Wo der Wert im Rennen ausgewertet wird, ist **noch offen**.
- **Korrektur aus 8.3:** Beim Umschalten des Verdecks im Menü wurde `FUN_004d1800` **nicht** aufgerufen, und `+0x510` blieb bei -1. `Car.Roof`/`+0x510` ist also nicht der Verdeck-Schalter des Menüs (Bedeutung offen; im Rennen stehen bei Gegnern Werte wie 4, 17, 84, 94). Die Annahme „Verdeck lädt Texturen neu“ ist damit nicht mehr belegt.
- **Animation:** Das Menü animiert das Verdeck (GIF und Video von 👤, 2026-10-16) als Mesh-Animation (§3.1 b).

### 3.1 Animationen des Auto-Modells

Das Modell-Objekt (vtable `0x5B3620`, Größe `0x1038`) hat zwei Arten von Animation:

**a) Kanäle (Bewegung von Teilen):** Menü 50, Rennen 24 Kanäle. `+0x64` → Zeitsteuerung je Kanal (Start, Dauer, von, nach; 16 Byte), `+0x2D0` → Wert je Kanal (12 Byte). `FUN_00438440` aktualisiert sie jedes Bild. Rennen: 1–4 Räder, 5 Lenkung, 11/12 Drehzahl/Tacho, 18–21 Radkopien. Kanal 8/9 startet das Menü per Klick auf das Auto (`FUN_004c0eb0`, Treffertest `FUN_004c0d10`). **Das Verdeck ist kein Kanal:** Beim Umschalten im Menü hat sich in 8.3 keiner bewegt.

**b) Mesh-Animationen mit Einzelbildern:** 8 Slots ab `+0x568`, je `0x14` Byte: Bild (int), nächstes Bild, Anteil (float), höchstes Bild (float), Änderungs-Flag. Setzen: `FUN_004380d0(Modell, Slot, Wert)` mit Wert = Bild + Anteil, begrenzt auf 0…max.
- **Slot 1 = Verdeck.** Das Menü (`FUN_004e93b0`) ruft `FUN_004d1990(Auto, 1, 0…255)` auf. Das rechnet in `Wert = max · v / 255` um. Das Video vom 2026-10-16 zeigt die Faltanimation über etwa 2 s.
- Slot 2 = Heckspoiler (im Rennen über 80 km/h aus, unter 50 km/h ein, 2000 ms, Zeitsteuerung Kanal 23). Slots 4–7 setzt `FUN_00438440` im Rennen ständig (u. a. Lichter, Nadeln).
- **Im Rennen fasst die Engine Slot 1 nicht an.** Ein Plugin kann die Verdeck-Animation deshalb selbst abspielen, indem es `FUN_004380d0(Modell, 1, Wert)` Bild für Bild aufruft. Im Menü klappt das (8.3b ✅).
- **Im Rennen hat Slot 1 kein Bild (`max = 0`, 8.3b).** Die Bildanzahl setzt `0x43C1B0` nur für Teile, die die Varianten-Tabelle auswählt. Das animierte Verdeck-Teil hat im Rennen also eine andere Variante als im Menü. v3 sucht im Teile-Katalog die Varianten mit Animation 1, schaltet sie ein und baut neu auf.

## 4. Mausklick auf die Scheibe

Im Rennen ist der Mauszeiger aus, und die Engine kennt keine Auswahl von Fahrzeugteilen per Klick. Nötig wären: Zeiger einblenden, Klickposition gegen die Scheiben-Geometrie prüfen (Strahl gegen Dreiecke). dx11z sieht nur Bildschirm-Vertices ohne Teil-Nummer (`knowledge.md` §3), die Prüfung gehört also in den Engine-Hook. Aufwand deutlich höher als die Taste. Vorschlag: erst Taste, Maus als Erweiterung.

## 5. Offene Fragen

1. ~~Spielerauto im Rennen?~~ Fahrzeug-Flags `+0x52C`: Spieler `0x105`, Gegner `0xB` (8.3). Das Plugin nimmt Bit `0x100` als Spieler-Merkmal (Annahme).
2. Ist `FUN_00447c50` im Rennen gefahrlos neu aufrufbar (alte Liste freigeben, Thread)?
3. Wo wird `.CABRIODOWN` im Rennen gelesen, und was ändert sich außer der Geometrie?
4. ~~Animiert das Menü das Verdeck?~~ Ja, Mesh-Animation Slot 1 (§3.1 b).
5. Ist die Teile-Liste (`+0x98`, `+0x47C`) im Rennen aktiv? `FUN_00441a10` (Laden) setzt `+0x47C = 1`, im Rennen aber `0`, wenn ein Flag des Modells gesetzt ist. Bei `0` wirkt der Neuaufbau nicht, und die Varianten-Auswahl liegt woanders.

## 5a. Motorgeräusch bei offenem Verdeck/Fenster (Machbarkeit, 2026-10-22)

Das Spiel kennt keinen Klang-Unterschied offen/geschlossen: Die Verdeck-Felder `Konfig + 0x510` / `Fahrzeug + 0x594` liest kein Sound-Code (nur Menü `FUN_004d1800`, Aufbau `FUN_00437b00`, Grafik `FUN_00443f30`).

Lautstärke-Weg des Motors (`FUN_004aa050`, aus `FUN_004a7660` je gehörtem Auto):
- `local_8` = Lautstärke nach Abstand (siehe `KAMERA.md`, Cinematic).
- Innenansicht (`[0x627F43] & 8`) **und** `FUN_00443ee0(Modell) ≠ 0` → `local_8 / 2`. `FUN_00443ee0` liefert 1, wenn `Modell + 0x2BC` (Byte) ≠ 0 oder `Konfig + 0x50C` ≠ 0; Bedeutung offen (Nachbarfeld von `+0x510`, evtl. „geschlossene Kabine“). Dasselbe Ergebnis wählt den Mix für `FUN_004ad640` (`local_c`).
- Ausgabe: `FUN_004ac780(Platz, Lautstärke, Tonhöhe, …)` an `0x4AA92C`, Lautstärke = `([0x5D1748] · [0x5D14C8] · 0x55 >> 7) · local_8 >> 14` (Gesamt- und Motor-Regler). `FUN_004ac780` beginnt mit `55 8B EC 83 EC 1C` (hookbar), Platz ≥ 2 wird verworfen.

Machbar in LFSP: Hook auf `FUN_004ac780`, im Rennen bei Innenansicht des eigenen Autos die Lautstärke mit einem Faktor aus Verdeck (`SlotValue / SlotMax` von Slot 1, max = offen) und Fenstern (`ModelState::shiftApplied` bzw. `windowsOpen`) anheben, begrenzt auf 127. **Umgesetzt (2026-10-22):** Hook `FUN_004ac780`, Platz 0, im Rennen, Innenansicht (Typ ≤ 1), nicht in Cinematic: Lautstärke × (1 + Verdeck · `EngineRoofBoost` + Fenster · `EngineWindowBoost`), Vorgaben 0,6 und 0,25, höchstens 127. Mit `Measure` schreibt LFSP jede Sekunde `Motor: Verdeck …, Fenster … -> Lautstaerke …` ins Log. 👤 noch nicht getestet. Offen: was `FUN_00443ee0` bedeutet (Log im Rennen mit Cabrio/Coupé), ob Reifen/Wind (`FUN_004a9c10` Kanäle 0/0x18/0x1A) mit angehoben werden sollen.

## 6. Plugin LFSP (`lfsp.asi`, bis v6a `nfsp_carparts.asi`)

**Umbenennung (2026-10-17, ab v6b):** Das Plugin heißt jetzt **LFSP – Love for Speed Porsche**, weil weitere Funktionen dazukommen, die nicht nur Fahrzeugteile betreffen. Dateien: `lfsp.asi`, `lfsp.ini`, `lfsp.log`, Geräusche `lfsp_roof_open.wav`, `lfsp_roof_close.wav`, `lfsp_window.wav`. Die Abschnitte bis v6a beschreiben den alten Namen `nfsp_carparts`, ihre Log-Pfade bleiben unverändert.

**Quellen:** `nfs5_enhanced_LHedit/lfsp/` (`lfsp.cpp`, `gen_windows_table.py` → `windows_table.h`), gebaut als CMake-Target `lfsp` mit LHedit (`build-asi/lfsp/lfsp.asi`); Vorgabe-`lfsp.ini` in `nfs5_enhanced_LHedit/config/`. Installiert werden `Game CD/lfsp.asi` und `Game CD/lfsp.ini`; der ASI-Loader lädt das Plugin von selbst. Entfernen: beide Dateien löschen.

**Hooks** (eigene Inline-Hooks; jede Stelle wird vor dem Schreiben gegen ihre Original-Bytes geprüft, nur bei `Porsche.exe` v3.5):

| Adresse | Funktion | Zweck |
|---|---|---|
| `0x441A10` | Modell laden (vtable Slot 1) | Fahrzeugname je Modell merken |
| `0x438440` | Modell-Update je Bild (Slot 9) | Tasten abfragen, Fenster/Verdeck umschalten, Messlog |
| `0x4D1800` | Menü: `Car.Roof` setzen | Aufruf und Wert loggen |

### 6.1 Testlauf 8.3 (v1, 2026-10-16)

Logs: `work/logs/8.3-carparts/` (`RebuildMode = 1` und `2`). Ergebnis: keine sichtbare Änderung, kein Fehler.
- **Ursache 1:** Die Style-Nummer am Fahrzeug (`+0x568`) zählt **ab 0**, die `.tpg` ab 1 (Beleg: `550` meldet Style 0, `550.tpg` hat kein `[style0]`). Das 356 Cabrio (`[style2]`) lief als 1, das Plugin fand keinen Eintrag `356_1.1` und schaltete das Spielerauto nie um.
- Umgeschaltet wurden nur zwei Gegner (901, 356B), auf die 👤 nicht geachtet hat. Neuaufbau ohne Absturz: Teile-Liste aktiv (`+0x47C = 1`), 3 Gruppen. Ob der Neuaufbau die Scheiben sichtbar ändert, ist **noch offen**.
- Spieler-Merkmal: Fahrzeug-Flags Spieler `0x105`, Gegner `0xB`.
- Verdeck: siehe §3 (Korrektur) und §3.1.

### 6.2 Version 2 (8.3b)

- Style + 1 für den `[Windows]`-Schlüssel.
- Im Rennen wirken die Tasten nur auf das eigene Auto (Flag `0x100`; `ApplyTo = 1` = alle). Im Menü auf das gezeigte Auto.
- **Verdeck-Taste (`RoofKey`, Standard `V`):** spielt Mesh-Animation Slot 1 von der aktuellen Stellung zum anderen Ende ab (Dauer `RoofTime`, Standard 2000 ms für den ganzen Weg, weich ein- und ausgeblendet). Erneutes Drücken während der Animation kehrt sie um.
- Messlog: statt der Kanäle die 8 Mesh-Animationen (Menü alle, Rennen Slot 1 und 2), je höchstens alle 100 ms.
- Build MD5 `23e0fd7c…`. Bei der Übergabe lief das Spiel, deshalb liegt die DLL als `Game CD/nfsp_carparts.asi.v2` bereit; die INI ist schon neu.

**INI:** `WindowKey`, `RoofKey` (Buchstabe/Ziffer, `F1`…`F12` oder `0x..`), `RoofTime`, `ApplyTo`, `Measure`, `RebuildMode`, Fenster-Tabelle mit 38 Einträgen `Fahrzeug.N` (N wie `[styleN]`), erzeugt aus `owindow` vs. `cwindow`; seit v6u fest eingebaut (`windows_table.h`), ein `[Windows]`-Abschnitt in der INI ersetzt nur noch einzelne Einträge.

### 6.3 Testlauf 8.3b (👤)

1. Spiel beenden. In `Game CD` die alte `nfsp_carparts.asi` löschen und `nfsp_carparts.asi.v2` in `nfsp_carparts.asi` umbenennen.
2. Spiel starten, das 356 Cabrio aufrufen. Im Menü das Verdeck einmal mit dem Menü-Knopf umschalten, dann **V** drücken (Verdeck sollte sich bewegen), dann **F** (Seitenscheiben).
3. Rennen mit dem 356 Cabrio starten. **V** drücken, warten, nochmal **V**. Dann **F**, nochmal **F**. Jeweils in Außen- und Cockpitansicht schauen.
4. Spiel beenden, `Game CD/nfsp_carparts.log` nach `work/logs/8.3b-carparts/` kopieren.

**Rückmeldung:** Bewegt sich das Verdeck (Menü/Rennen)? Sieht es dabei richtig aus (Texturen, Innenraum, Lücken)? Ändern sich die Scheiben? Absturz oder Grafikfehler?

**Ergebnis 8.3b** (Log `work/logs/8.3-carparts/nfsp_carparts.log`, Video 2026-10-16 16:54): Verdeck per `V` im Menü ✅. Im Rennen ❌, weil Slot 1 `max = 0` hat (§3.1). Fenster per `F` weder im Menü noch im Rennen. Das Plugin schaltete die richtigen Einträge (`356_1` `[style2]`, Spieler `0x105`), baute aber nur die Textur-Teile neu auf (§2, Korrektur).

### 6.4 Version 3 (8.3c)

- Neuer Hook auf `0x43C1B0` (Geometrie je Render-Schicht). Das Plugin merkt sich Schicht und `.crp` je Modell und liest einmal je Fahrzeug den **Teile-Katalog** (alle Teil/Variante-Paare mit Mesh-Animation) ins Log.
- **Fenster:** Nach dem Tabellenwechsel werden alle Schichten des Modells neu aufgebaut. Die Textur-Teile nur noch mit `RebuildMode = 2`.
- **Verdeck im Rennen:** Hat Slot 1 kein Bild, schaltet `V` erst die Katalog-Varianten mit Animation 1 ein, baut neu auf und spielt dann die Animation ab.
- Build MD5 `817656c0…`. Das Spiel lief bei der Übergabe, die DLL liegt als `Game CD/nfsp_carparts.asi.v3` bereit.

### 6.5 Testlauf 8.3c (👤)

1. Spiel beenden. In `Game CD` die alte `nfsp_carparts.asi` löschen und `nfsp_carparts.asi.v3` in `nfsp_carparts.asi` umbenennen.
2. Im Menü beim 356 Cabrio **F** drücken (Scheiben), nochmal **F**.
3. Rennen mit dem 356 Cabrio: **F**, nochmal **F**. Dann **V**, warten, nochmal **V**. Jeweils Außen- und Cockpitansicht.
4. Spiel beenden, `Game CD/nfsp_carparts.log` nach `work/logs/8.3c-carparts/` kopieren.

**Rückmeldung:** Scheiben im Menü und Rennen? Verdeck im Rennen, und sieht es richtig aus (doppelte Teile, Lücken, Innenraum)? Absturz? Bei einem Absturz bitte trotzdem das Log sichern.

**Ergebnis 8.3c (v3):** Beim ersten **F** im Menü bricht das Spiel mit „R6025 pure virtual function call“ ab (Log `work/logs/8.3c-carparts/nfsp_carparts_purecall.log`; das Log endet beim Umschalten, also im Neuaufbau). Das 356_1 hat im Menü 8 Render-Schichten (Id 1–7, Id 7 zweimal; die zweite entsteht später). Vermutung: Eine gemerkte Schicht war schon gelöscht. Ein gelöschtes C++-Objekt verweist auf die Basis-vtable, deren Methoden „pure virtual“ sind.

**v3b** (MD5 `a68ccaa4…`, installiert): Vor jedem Neuaufbau prüft das Plugin, ob die Schicht noch dieselbe vtable und dasselbe Modell hat. Sonst entfernt es sie und schreibt einen Logeintrag. Das Log nennt jetzt die vtable jeder Schicht und jeden einzelnen Neuaufbau. Test wie §6.5, Log nach `work/logs/8.3c-carparts/`.

**Ergebnis 8.3c (v3b, Log `work/logs/8.3c-carparts/nfsp_carparts_v3b.log`):** Kein Absturz mehr. **Fenster per F im Menü und im Rennen ✅** (Neuaufbau aller 7 Schichten über `0x43C1B0`). Verdeck im Menü ✅, im Rennen ❌: Der Teile-Katalog war falsch gelesen (Werte wie `0/192`). Teil und Variante stehen nicht im `Atri`-Eintrag selbst, sondern in dessen Unter-Eintrag `Base` (`0x59F7C0`, Daten `+0x44`/`+0x45`, wie `0x43C282`). Die Mesh-Animation steht nicht am Teil, sondern am Kopf des gebauten Listeneintrags (Schicht `+0xC`, je `0x54` Byte; `+0` Teil-Eintrag, `+0x44` → Kopf, Byte 9 = Animation).

**v3c** (MD5 `fbfc37f2…`, installiert): Nach jedem Aufbau durchsucht das Plugin die gebaute Liste und merkt sich je Fahrzeug die Teile mit Mesh-Animation (Log: „Animiertes Teil“). Im Menü ist das Verdeck-Teil dabei. Im Rennen schaltet `V` genau diese Variante ein, baut neu auf und spielt die Animation ab. **Voraussetzung:** Das Auto wurde in derselben Spielsitzung im Menü angezeigt (bisher nur im Speicher). Der Katalog liest jetzt `Base`.

**Ergebnis 8.3c (v3c, Log `work/logs/8.3c-carparts/nfsp_carparts_v3c.log`, Video 17:27):** Verdeck öffnet und schließt im Rennen ✅, aber ein **starres Verdeck liegt darunter**. Animiertes Verdeck des 356_1 = Teil 37 Variante 1 (Mesh-Animation 1, 5 Bilder). Im Rennen steht Teil 37 auf 0 (obwohl `[style2]` 37 = 1 setzt), dafür zeigt Teil 7 das starre Dach. Die `.tpg` beschreibt es eigens: `[file1.top]` 7 → 0 (offen) und `[file1.top1]` 7 → 3 (geschlossen). Weitere Mesh-Animationen am 356_1: Teil 43 (4, 5), Teil 9 (6, Fahrerkopf), Teil 0 (3).

**v3d** (MD5 `ed99acb8…`): Beim Einschalten des animierten Teils setzt das Plugin zusätzlich alle Werte aus den `[fileN.top]`-Abschnitten der `.tpg` (`Game CD/GameData/CarModel/<Auto>.tpg`). Das starre Dach ist danach aus. Das Spiel lief, die DLL liegt als `Game CD/nfsp_carparts.asi.v3d` bereit.

**Ergebnis 8.3c (v3d, Log `work/logs/8.3c-carparts/nfsp_carparts_v3d.log`):** Verdeck weiter doppelt (beweglich + starr). Das Fenster-Umschalten setzt das Verdeck auf die Startstellung zurück. Startet das Rennen mit offenem Verdeck, erscheint mit der Animation zusätzlich ein starres Verdeck.
- **Teil 7 Variante 0 ist das zusammengelegte Verdeck hinter den Sitzen**, nicht das Dach (`[file1.top]` = offen). v3d hat es also zusätzlich eingeblendet. Richtig ist `[file1.top1]` (7 → 3 = nichts).
- Das **starre geschlossene Dach im Rennen ist ein anderes Teil**, noch unbekannt.
- Der Neuaufbau (`0x43C1B0`) setzt jede Mesh-Animation auf Bild 0. Daher springt das Verdeck beim Fenster-Umschalten zurück.

**v3e** (MD5 `30288bc5…`, installiert): Das Plugin nimmt `[fileN.top1]` statt `[fileN.top]`. Es merkt sich vor jedem Neuaufbau die Stellung aller 8 Mesh-Animationen und stellt sie danach wieder her. Zur Suche nach dem starren Dach schreibt es zusätzlich die Varianten-Tabelle („Tabelle …“) und die gebauten Teile der Schicht 1 („Teile Schicht 1 …“) für das Menü-Auto und das eigene Auto ins Log, beim Aufbau und nach jedem Neuaufbau.

**Ergebnis 8.3c (v3e, Log `work/logs/8.3c-carparts/nfsp_carparts_v3e_auf.log`, Start mit offenem Verdeck):** Geschlossener Start: wie v3d, starres Verdeck. Offener Start: `V` lässt ein geschlossenes Verdeck erscheinen und spielt die Öffnung ab. `F` lässt wieder ein starres Verdeck erscheinen, ein weiteres `V` ebenso.
- Varianten-Tabelle 356_1 `[style2]`: **Menü** `7=3 9=1 24=1 25=1 37=1 43=1 54=1`, **Rennen** `7=3 9=4 24=1 25=1 41=2 43=2 54=1 56=1`.
- **Teil 41 ist das starre Verdeck:** `[style2]` setzt 41 = 1 (geschlossen), das Rennen bei offenem Start 41 = 2 (offen/zusammengelegt). Das Menü schaltet 41 ganz aus (0) und zeigt stattdessen das animierte Teil 37. Teil 56 (Animation 4/5) gibt es nur im Rennen (Cockpit); 9 und 43 unterscheiden sich in der Variante (Fahrer, Innenraum).
- Mesh-Animation 1, Bild 0 = geschlossen, Bild 5 = offen.
- Die Animationsstellung wird nach dem Neuaufbau in den Daten wiederhergestellt (Log: Bild 4 + 1,0). Laut 👤 erscheint nach `F` trotzdem wieder ein starres Verdeck. Ob das Teil 41 ist oder das nicht neu verformte Teil 37, ist offen.

**v3f** (MD5 `72714f3e…`, installiert):
- Das Plugin merkt sich die Varianten-Tabelle des Menü-Autos je Auto und Style. Beim Einschalten des animierten Verdecks schaltet es jedes Teil aus, das im Menü 0 und im Rennen an ist und selbst keine Mesh-Animation hat (356_1: Teil 41).
- Weicht die Rennen-Variante eines solchen Teils vom `[styleN]`-Wert ab, ist das Rennen offen gestartet, und die Animation beginnt bei „offen“ (Bild max).
- Nach jedem Neuaufbau setzt das Plugin die Verdeck-Stellung drei Bilder lang erneut, jeweils mit einer kleinen Änderung, damit die Engine das Teil neu verformt.

**Ergebnis 8.3c (v3f, Video 18:08, Log `work/logs/8.3c-carparts/nfsp_carparts_v3f.log`):** ✅ Verdeck öffnet und schließt im Rennen ohne starres Doppel-Dach, Fenster unabhängig davon. Rest: Nach dem **Öffnen** zeigt das Verdeck für ein einziges Bild (bei ~240 fps) die geschlossene Fassung.
- Vermutete Ursache: Am Ende setzt das Plugin genau das letzte Bild (Wert = max = 5,0). Die Engine zerlegt das in Bild 4 + Anteil 1,0 mit Folgebild 5 und greift an dieser Grenze einmal auf Bild 0 (geschlossen). Beim Schließen (Ziel 0) gibt es die Grenze nicht.

**v3g** (MD5 `f44e61a0…`, installiert): Die Animation endet bei max − 0,001 statt bei max, ebenso die Startstellung „offen“. Optisch gleich, die Grenze wird nicht erreicht.

### 6.6 Boxster und Verallgemeinerung (8.3d)

**Ergebnis Boxster (v3g, Log `work/logs/8.3d-boxster/nfsp_carparts_v3g_boxster.log`):** Wieder ein starres Verdeck. Ursache: Die Regel „Werte aus `[fileN.top1]`“ war 356-spezifisch. Beim Boxster setzt `top1` 7 → 0, und **7/0 ist dort ein festes Dach** (nach dem Neuaufbau in der Teileliste).
- Menü Boxster `[style1]`: `7=3 37=1 42=1`. Rennen: `6=3 7=3 9=4 41=2 43=2 56=1` (6, 9, 43, 56 animiert). Die Verdeck-Animation des Boxster hat 7 Bilder.

**Vergleich aller Cabrio-`.tpg`** (356_1, 356a, 356b, 944, 964, 993, 996, boxster, fboxster, cop356, gt3):
- Teil **37** = animiertes Verdeck, Teil **41** = starres Verdeck (Varianten je Auto, z. B. 356b Cabrio 1, Roadster 3, 356a 2 oder 6).
- Teil **7** hat je Auto eine andere Bedeutung: `[fileN.top]`/`top1`/`top2` setzen es mal auf 0, mal auf 1–3, beim 996/gt3 schalten sie Teil 1 bzw. 37. Diese Abschnitte taugen nicht als allgemeine Regel.
- Zusatzteile wie **42** (964, 993, 996, Boxster) stehen im Menü auf 1, im Rennen auf 0.

**v3h** (MD5 `26d60c20…`, installiert): Die `top`-Abschnitte werden nicht mehr benutzt. Beim Einschalten des animierten Verdecks übernimmt das Plugin für **alle nicht animierten Geometrie-Teile (< 0x3C) die Menü-Werte**, in beide Richtungen. Ausgenommen sind die Seitenscheiben 35/47. Für den Boxster heißt das 41 → 0 und 42 → 1, für den 356 41 → 0. Startstellung: Steht Teil 41 im Rennen auf einer anderen Variante als der geschlossenen (`[styleN]`-Wert, sonst 1), startet die Animation offen. Für den Boxster ist das eine Annahme (dort ohne `[style1]`-Wert, Rennen 41 = 2).

**Ergebnis v3h (Boxster, Log `work/logs/8.3d-boxster/nfsp_carparts_v3h.log`):** Kein starres Verdeck mehr ✅ (41 → 0, Start „geschlossen“ richtig erkannt). Zwei neue Fehler:
- **Verdeck öffnen und gleichzeitig F → nach der Animation wieder zu.** Ursache: Nach jedem Neuaufbau setzt das Plugin die Stellung drei Bilder lang erneut. Während einer Animation wird das aufgeschoben, danach aber mit dem alten Wert vom Beginn ausgeführt. Im Log beginnt deshalb auch das zweite `V` bei Bild 0.
- **Heckspoiler fährt nicht mehr sichtbar aus.** Die Spoiler-Animation (Teil 6/3, Animation 2, 4 Bilder) läuft laut Log weiter. Aber v3h hat Teil **42** (nur im Menü an) eingeschaltet, offenbar ein starres Heck, das den Spoiler verdeckt.

**Verdeck-Geräusch im Menü:** Klickt man im Menü auf das Verdeck, ruft `FUN_004c0eb0` (Fall 0) den Accessor `Car.Roof` und **`FUN_004aadb0(0xD)`** auf (Frontend-Geräusch Nr. 13, Bank `0x656510`, Aus-Flag `0x657C94`). Andere Fälle: 0x32/0x34/0x30 (Türen, Hauben). Ob die Frontend-Bank im Rennen geladen ist, ist offen.

**v3i** (MD5 `6c189a10…`, installiert):
- Die Animation schreibt ihre aktuelle Stellung laufend in den Merker, ein Neuaufbau während der Animation stellt also den richtigen Wert her. Beim Start einer Animation wird das Nachsetzen abgebrochen.
- Menü-Werte werden nur noch zum **Ausschalten** übernommen (Menü 0, Rennen an, nicht animiert): 41 ja, 42 nicht mehr.
- `V` spielt Frontend-Geräusch 13 ab (`RoofSound = 1`, `RoofSoundId = 13`). Im Rennen nur, wenn die Bank-Kennung ≠ 0 ist. `RoofSound = 2` erzwingt es (Test, Absturz möglich). Das Log nennt Bank und Aus-Flag.

**Ergebnis v3i (Log `work/logs/8.3d-boxster/nfsp_carparts_v3i.log`):** Verdeck im Rennen ✅ (auch mit F während der Animation). Kein Geräusch im Rennen. Im Menü: `V` schließt mit Geräusch, am Ende springt das Verdeck aber auf den Menü-Stand zurück (das Menü steuert Slot 1 selbst). Nach jedem `F` ist der Heckspoiler unsichtbar, bis er sich das nächste Mal bewegt.
- Geräusch: Die vermeintliche Bank-Kennung `0x656510` ist auch im Menü 0. Die Prüfung war also wertlos, und im Rennen wurde nie etwas abgespielt.
- Spoiler: Der Neuaufbau setzt jede Mesh-Animation auf Bild 0. Ein eingefahrener Spoiler hat den Wert 0, also gibt es keine Änderung. Die Engine verformt ein Teil nur bei einer Änderung neu, deshalb bleibt es leer, bis der Spoiler sich wieder bewegt.

**Verdeck-Geräusch aus `GameData/Sounds/fesfx.bnk`:** EA-Soundbank `BNKl` Version 4, 54 Einträge. Eintrag i: PT-Header bei `0x10 + 4·i + Offset[i]`. Nr. 13: PT-Version 1, ohne Kanal-/Codec-/Raten-Tag, also mono, EA-XA v1, 22050 Hz. 51 677 Samples = 2,34 s, Daten ab `209496`, 27 692 Byte (= 15 Byte je 28 Samples). `work/patches/carparts/extract_bnk_sound.py` dekodiert das (EA-XA v1: Kopfbyte = Koeffizient/Shift, 14 Byte Nibbles) zu WAV. Ergebnis ohne Clipping, etwa 2 s Hüllkurve, dann Ausklang. Dass es das Verdeck-Geräusch ist, muss 👤 per Gehör bestätigen.

**v3j/v3k** (v3k MD5 `cac2c055…`, installiert):
- Nach jedem Neuaufbau wird **jede** animierte Mesh (Spoiler usw.) sofort und drei Bilder lang kurz verstellt und auf ihren aktuellen Wert zurückgesetzt.
- `V` wirkt nur im Rennen (`RoofKeyInMenu = 0`). Im Menü gibt es den eigenen Knopf.
- Geräusch im Rennen: Das Plugin lädt `Game CD/nfsp_carparts_roof.wav` (aus `fesfx.bnk` Nr. 13, mit `extract_bnk_sound.py` erzeugt, nicht versioniert) und spielt sie mit `PlaySound` asynchron ab. Lautstärke über `RoofVolume` (%), unabhängig von der Spiel-Lautstärke. `RoofSound = 2` versucht das Menü-Geräusch auch im Rennen (Test).

**Geräusch Nr. 13 ist nicht das Verdeck** (👤, 2026-10-16). `extract_bnk_sound.py` kann jetzt alle Einträge ausgeben (`… fesfx.bnk alle <Ordner>`) und Stereo. EA-XA v1 stereo = je 28 Samples ein 15-Byte-Rahmen für L, dann einer für R (aus den Daten abgelesen; geprüft über die Datengröße von Nr. 1: 29 792 Byte ≙ 27 805 Samples, Header 27 789). `fesfx.bnk`: 24 belegte Einträge (1–7, 9–15, 21–25, 49–53). 1–10 stereo 44,1/32 kHz, Rest mono 22,05/32 kHz. Nr. 24 nutzt dieselben Daten wie Nr. 22. Ausgabe nach `work/extract/fesfx/` (nicht versioniert). Weitere Bänke zum Durchsuchen: `lilfesfx.bnk`, `SND_coll.bnk`, `LilSND_coll.bnk`.

**Ergebnis v3k (Log `work/logs/8.3d-boxster/nfsp_carparts.log`):** Spoiler verschwindet nach `F` weiterhin, bis er sich bewegt. Im Log stimmen die Spoiler-Werte (Animation 2, max 4).
- Morph-Ablauf: `FUN_004383c0` (vtable Slot 7) → **`FUN_00438030`**. Ist ein Änderungs-Flag (`Modell + 0x578 + Slot·0x14`) gesetzt, ruft er für jede Render-Schicht (`Modell + 0xB0…0xB4`) **`FUN_0043e120`** auf und löscht danach alle Flags. `FUN_0043e120` interpoliert je animiertem Listeneintrag zwischen den Bildern (`FUN_004380b0` = Anteil) in den Ausgabepuffer `Eintrag + 0x40`.
- Warum ein Spoiler auf Wert 0 nach dem Neuaufbau unsichtbar bleibt, ist statisch nicht geklärt. Belegt ist nur: Eine echte Bewegung behebt es. Das Anstoßen in v3j/v3k lag **vor** dem Engine-Update im selben Bild, und die Engine setzt den Spoiler danach selbst zurück.
- 👤 Hörtest: fesfx Nr. **14 passt zum Öffnen**, Nr. **15 zum Schließen**. Das Menü nutzt laut 👤 nur 14.

**v3l** (MD5 `2103d28b…`, liegt als `nfsp_carparts.asi.v3l` bereit, Spiel lief):
- Nach einem Neuaufbau verstellt das Plugin **nach** dem Engine-Update jede animierte Mesh für ein Bild um eine Viertel-Stufe. Im nächsten Bild stellt es sie zurück, wenn die Engine sie nicht selbst gesetzt hat.
- Geräusche über **MCI** (Typ `mpegvideo`: WAV und MP3, mehrere gleichzeitig, Lautstärke per `setaudio`): `RoofSoundOpen` (`nfsp_carparts_roof_open.wav` = fesfx 14), `RoofSoundClose` (`…_roof_close.wav` = fesfx 15), `WindowSound` (`nfsp_carparts_window.mp3`, von 👤), `RoofVolume`, `WindowVolume`. Das Frontend-Geräusch und `PlaySound` sind entfernt.

**Ergebnis v3l (Log `work/logs/8.3d-boxster/nfsp_carparts_v3l.log`):** Verdeck, Fenster und Geräusche im Rennen ✅. Der Spoiler verschwindet nach `F` weiterhin, bis er sich bewegt. Auch das Verstellen nach dem Engine-Update hilft nicht.

**Neuer Ansatz:** Das Spiel baut Geometrie nie durch Neubefüllen bestehender Schichten um. Beim Laden (`0x4420A3`) und bei einem Verdeck-Wechsel im Menü (`FUN_004d1800`) ruft es **`FUN_004425b0`** auf. Diese Funktion zerstört alle Render-Schichten in `Modell + 0xB0…0xB4` (Destruktor), fragt über vtable `+0x40` die Anzahl ab, legt jede Schicht über die Fabrik vtable `+0x44` (`0x43ADB0`) neu an und baut sie mit Schicht-vtable Slot 1 (`0x43C1B0`, `.crp` = `Modell + 0xEC`). Danach berechnet sie die Hüllbox (`FUN_00445090`). Vermutung: Neu befüllte Schichten behalten einen Zustand, den nur frische Objekte richtig haben.

**v3m** (MD5 `dc4971f0…`, installiert): `RebuildMethod = 2` (Standard) ruft `FUN_004425b0` auf. Der Aufbau-Hook trägt die neuen Schichten ein, Animationsstellungen werden wie bisher gesichert und wiederhergestellt. `RebuildMethod = 1` ist der bisherige Weg.

**Ergebnis v3m (Log `work/logs/8.3d-boxster/nfsp_carparts_v3m.log`):** Verdeck und Fenster bewegen sich nicht mehr, kein Geräusch, kein Absturz. Der Spoiler fehlt trotzdem manchmal.
- `FUN_004425b0` ruft vor dem Neuanlegen vtable `+0x4C` = **`FUN_0043ae50`** auf. Diese Funktion setzt die Varianten-Tabelle aus `[styleN]` und den Fahrzeugdaten (`Konfig + 0x32C`) neu und löscht alle Mesh-Animationen (max = 0). Damit sind die Plugin-Werte weg: nach jedem `F` dieselbe Teileliste, Verdeck-Slot max 0.
- Fenster-Geräusch: 👤 hat die MP3 durch `nfsp_carparts_window.wav` ersetzt, die INI nannte noch `.mp3`, MCI meldete Fehler 263.
- **Spoiler, neue Erklärung:** Er verschwindet auch beim Neuaufbau durch das Spiel selbst. `FUN_0043e120` kopiert je Listeneintrag einen Merker (Eintrag `+8`/`+0xC`: zuletzt gelesener Daten-Eintrag und dessen Schlüssel = Bildnummer·16 + Schicht-Id). Gesucht wird nur, wenn sich der Schlüssel ändert. Vermutung: Nach dem Aufbau passt der Merker zum Schlüssel von Bild 0, zeigt aber nicht auf gültige Daten. Erst ein Wechsel der **ganzen Bildnummer** (echte Spoiler-Bewegung) erneuert ihn. Das Anstoßen um 0,01 bzw. 0,25 blieb im selben Bild.

**v3n** (MD5 `21a60056…`, installiert): `RebuildMethod = 1` wieder Standard. Nach einem Neuaufbau wird jede Mesh-Animation nach dem Engine-Update für ein Bild um **ein ganzes Bild** verstellt (+1, am Ende −1) und danach zurückgestellt. Geräuschdateien: Fehlt die genannte Datei, wird die andere Endung (`.wav`/`.mp3`) versucht. Standard `WindowSound = nfsp_carparts_window.wav`.

**Ergebnis v3n (Log `work/logs/8.3d-boxster/nfsp_carparts_v3n.log`):** Verdeck und Fenster mit Geräusch ✅. Der Spoiler bleibt nur beim ersten `F`, danach verschwindet er wieder.
- Das Verstellen „um ein ganzes Bild“ wurde von der Engine gerundet: `FUN_004380d0` rechnet Bild = ROUND(Wert − 0,5) (x87, zur geraden Zahl), Anteil = Rest. Aus 0 + 1 wurde „Bild 0 + 1,000“, aus 4 − 1 „Bild 2 + 1,000“. Die Merker-Vermutung ist damit nicht belegt: Auch mit Wechsel der Bildnummer (ausgefahren) verschwand der Spoiler.
- Neuer Befund: Die Engine setzt den ausgefahrenen Spoiler auf genau **Bild 4 bei 4 Bildern** (0–3). Für ein Bild ohne Daten schreibt `FUN_0043e120` nichts in den Ausgabepuffer (Eintrag `+0x40`) und lässt die letzte gültige Form stehen. Nach einem Neuaufbau ist der Puffer frisch.

**v3o** (MD5 `1d519f84…`, installiert): Vor dem Neuaufbau jeder Schicht sichert das Plugin die verformten Puffer aller animierten Teile und schreibt sie danach in die neuen Puffer zurück. Zuordnung über den Teil-Eintrag, Größe = Vertexanzahl · 16. Die Vertexanzahl wird wie in `FUN_0043e120` bestimmt: Unter-Eintrag mit Schlüssel `dword[0x5E5084] & 0xFFFF0000 | Schicht-Id`, drittes Dword. Messlog „Pose vorher/nachher/naechstes Bild“: Puffer, Vertexanzahl, erster Vertex und Wert jeder Animation der Schicht 1 außer dem Verdeck.

**Ergebnis v3o (Log `work/logs/8.3d-boxster/nfsp_carparts_v3o.log`): ✅ Boxster komplett.** Spoiler bleibt bei jedem `F` sichtbar (eingefahren und ausgefahren). Verdeck mit Animation und Geräuschen, Fenster mit Geräusch, kein Doppel-Dach. Ursache des Spoiler-Fehlers war also der frische, nie gefüllte Ausgabepuffer (Bild = max hat keine Daten).

### 6.7 Scheiben animieren (8.3e)

Ziel: Beim Öffnen fährt die Scheibe nach unten und verschwindet, beim Schließen umgekehrt. Die Scheiben-Teile 35/47 haben keine Mesh-Animation, nur die Varianten „zu“ und „offen“.

Befunde aus Ghidra:
- Die Zeichenfunktion einer Render-Schicht **`FUN_0043c830`** nimmt die Vertex-Positionen eines Listeneintrags aus **Eintrag `+0x40`**, wenn dort ein Puffer steht. Sie setzt ihn als Strom `0x76740001` ('vt', Größe Anzahl · 16). Ohne Puffer kommen die Positionen aus der `.crp`.
- `0x43C1B0` legt den Puffer über **`0x59EFF0`** (Speicher der Engine, cdecl) an: für animierte Teile (Kopf-Byte 9 ≠ 0) und – Korrektur nach v4a – auch für Teile mit Kopf-Byte 10 Bit 8, wenn `dword[0x6197C4] > 0` (vermutlich Schadensmodell; `0x43C58B`). Dort ist es eine Kopie der Ruhelage aus 'vt'. Die Scheiben (Kopf-Byte 10 = `0x1A`/`0x1B`) haben ihn also schon. `FUN_0043e0c0` gibt beim Abräumen jeden Puffer `+0x40` mit **`0x59F050`** frei.
- `FUN_0043e120` (Morph) fasst nur Einträge mit Animation an. Ein eigener Puffer an einer Scheibe bleibt also unberührt.
- Schlüssel-Vorlage `dword[0x5E5084]` = `'  tv'` → Unter-Eintrag `0x7674xxxx` = Strom 'vt' der Schicht xxxx (Bild 0 = Ruhelage).
- Nebenbefund: Teile mit eigener Matrix (Unter-Eintrag `'tr'`) können über Kopf-Byte 4 eine Verschiebung je Modell bekommen (`Modell + 0x6D0 + i · 0xC`, `FUN_00438210`), Kopf-Byte 7 eine Drehung. Für Scheiben ohne `'tr'` greift das nicht.

Ansatz: einen eigenen Puffer je Scheiben-Eintrag anlegen (Speicher der Engine, damit das Abräumen ihn freigeben darf), mit den Ruhelage-Positionen füllen und je Bild verschieben. Der Puffer gehört zur Schicht dieses Modells. Gegner mit demselben Auto teilen nur die `.crp` und sollten unverändert bleiben.

**Messbuild v4a** (MD5 `c853f769…`, installiert): Taste `WindowTestKey` (Standard `G`) verschiebt die Scheiben des Spielerautos einmal um die halbe eigene Höhe nach unten, ein zweiter Druck stellt sie zurück. Die Achse „oben“ ist die kleinste Ausdehnung des Autos (Schicht 1), „unten“ zeigt zur Wagenmitte. Beides ist über `WindowAxis` und `WindowDownSign` festlegbar. Messlog „Scheiben-Test“: Ausmaße von Auto und Scheiben, je Eintrag Schicht, Teil, Kopf-Bytes, Vertexanzahl und Puffer, dazu die Einträge anderer Modelle mit demselben Auto. Ein Neuaufbau (`F`) verwirft die Verschiebung.

Zu prüfen 👤: Wandern die Scheiben sichtbar nach unten (und in die richtige Richtung)? Bleiben Gegner mit demselben Auto unverändert? Wie sieht es in der Cockpitansicht aus?

**Ergebnis v4a (👤, Boxster):** keine sichtbare Bewegung, weder am eigenen Auto noch an Gegnern. Laut Log hatten alle Scheiben-Einträge schon einen Puffer (siehe Korrektur oben), und v4a überspringt besetzte Puffer. Jedes Modell hat eigene Puffer, die Atri-Einträge teilen sich alle Boxster. Außerdem wählte die Automatik die falsche Achse (x): Breite 1,77 ist kleiner als die Höhe 1,80 (mit Rädern). Die Scheiben-Teile tragen Kopf-Byte 7 = 6/7, sie hängen also an der Tür (links/rechts, Drehung über `FUN_00443f10`). Schicht 1/2: Türkoordinaten; Schichten 3–6: Wagenkoordinaten (y 0,80…1,13 = Dachhöhe). In beiden ist **y = oben**, Scheibenhöhe 0,326. Bei offenen Scheiben (Variante 1) gibt es in Schicht 1 keine Scheiben-Einträge.

**v4b** (MD5 `9e1ead15…`, installiert): `G` fährt die Scheiben in `WindowTime` (1000 ms, weich) um `WindowDepth` Scheibenhöhen (1,0) nach unten, ein zweites `G` wieder hoch. Das Plugin verschiebt den vorhandenen Puffer, einen eigenen legt es nur an, wenn keiner da ist. Es addiert nur die Differenz zum bisherigen Weg, damit Verformungen durch Schaden erhalten bleiben. Achse fest `WindowAxis = 1`, `WindowDownSign = -1`.

**Ergebnis v4b (👤, 356):** Die Animation läuft einwandfrei, fährt die Scheiben aber nach **oben**. Ursache: `GetPrivateProfileIntA` liefert `UINT`, `-1` kommt als `0xFFFFFFFF` an. Der Vergleich `< 0` war nie wahr, also galt +1 (Log: „unten +1“). Damit ist belegt: **unten = −y**.

**v4c** (MD5 `1b202e2e…`, installiert): liest `WindowDownSign` vorzeichenrichtig, sonst unverändert.

**Ergebnis v4c (👤, 356):** Die Scheiben fahren sauber nach unten, und zwar nur am eigenen Auto, die Gegner bleiben unverändert. Wunsch: Beim Öffnen leicht nach außen wandern, damit es aussieht, als glitte die Scheibe in die Tür.

**v4d** (MD5 `68e66776…`, installiert): Je Scheibe bestimmt das Plugin aus den Vertices eine Ausgleichsgerade Querachse (x) über Hochachse (y), also die Neigung der Scheibe. Beim Absenken folgt die Scheibe dieser Neigung (`WindowTilt` = Anteil, 1,0 = wie gemessen, 0 = senkrecht). Oben nach innen geneigte Scheiben wandern so automatisch nach außen, passend für jedes Auto. Messlog: Neigung und Querbereich je Eintrag.

**Ergebnis v4d (👤):** „wirkt richtig Premium“. Feinschliff: etwas tiefer als eine Scheibenhöhe. Die Scheibe ein Stück oben aus der Tür ragen zu lassen wäre realistischer, bräuchte aber Werte je Auto. Darauf wird bewusst verzichtet.

**v4e** (MD5 `12133fdb…`, installiert): Standard `WindowDepth = 1.05`, sonst unverändert.

**v5a** (MD5 `af343508…`, installiert): Die Animation hängt an `F`, die Testtaste `G` ist entfernt.
- **Öffnen:** Die Scheiben fahren in `WindowTime` nach unten. Unten angekommen schaltet das Plugin auf die Variante „offen“ und baut neu auf, das verwirft die Verschiebung.
- **Schließen:** Das Plugin schaltet auf „zu“ und baut neu auf. Noch vor dem ersten Zeichnen setzt es die frischen Puffer ganz nach unten und fährt sie dann hoch.
- **`F` während einer Bewegung:** kehrt sie um.
- **Sonst:** `WindowAnimate = 0` schaltet wie früher ohne Bewegung um. Fehlen Scheiben-Einträge, schaltet das Plugin ebenfalls sofort um. Ein Neuaufbau durch das Verdeck während einer Bewegung bricht sie ab.

**Ergebnis v5a (👤, Boxster):** Scheiben mit `F` ✅ in allen Fällen. Fehler: Nach mehrmaligem schnellem `V` blieb das Verdeck halb offen stehen. Ein weiteres `V` ließ es weiterlaufen. Ursache laut Log: Eine Umkehr zielte auf das Startbild der laufenden Animation statt auf das andere Ende. Nach mehreren Umkehrungen lag das Ziel mitten im Weg (0,97 → 3,59 bei max 7, „fertig bei 3,59“).

**v5b** (MD5 `28a3bdc2…`, installiert): Eine Umkehr zielt immer auf das andere Ende (0 oder max).

**Ergebnis v5b (👤):** Das Verdeck bleibt nicht mehr hängen ✅. Neu: Je nach Modell ist die Scheibe kurz vor dem Ende des Absenkens bzw. am Anfang des Hochfahrens durch das Türblech zu sehen. Die Tür wird unter der Brüstung schmaler (Einzug zum Schweller), die Scheibe fährt aber gerade auf ihrer Neigung weiter. Erwogen: Drehen (bräuchte Werte je Auto), Ausblenden (keine Transparenz je Teil bekannt), Verkleinern (unecht). Gewählt: Kappen an der Brüstungslinie.

**v5c** (MD5 `ad49fe27…`, installiert): Je Scheibe wird die Brüstungslinie bestimmt, als Ausgleichsgerade durch die untersten Vertices (unteres Viertel der Höhe) über der Längsachse (z). Kein Vertex sinkt tiefer als diese Linie, die Unterkante bleibt also stehen und die Oberkante fährt in den Schlitz. Am Ende ist die Scheibe eine Linie auf der Brüstung. Die Querbewegung folgt dem tatsächlichen Weg jedes Vertex. Der Puffer wird jetzt aus den beim Sammeln gesicherten Positionen berechnet statt schrittweise addiert. Weg 0 ergibt exakt diese Positionen. Schaden, der während der einen Sekunde entsteht, geht dabei verloren.

**Ergebnis v5c (👤):** Funktioniert. Wünsche:
- Wieder sichtbares Herunterfahren, kombiniert mit dem Stauchen.
- Beim 356B Coupé (Style 4 „CoupeGSCar2“) lief die Verdeck-Animation. Das Coupé teilt sich das Modell mit dem Cabrio.
- Bei Nicht-Cabrios gingen die Scheiben nicht, weil `[Windows]` keinen Eintrag hat (Log „kein Eintrag 356b.4“).

`.tpg`-Befund: Nur Verdeck-Styles nennen das animierte Teil 37 in `[styleN]`, Cabrios mit Variante 1, Roadster/Speedster mit 2 oder 3. Coupés, Targas und Hardtops nennen es nicht. Ausnahme: `gt3.tpg` nennt 37=1, dort ist Teil 37 vermutlich kein Verdeck.

**v5d** (MD5 `7fe7be3a…`, installiert):
- `WindowSink` (Standard 0,4): Die Kappungslinie liegt um so viele Scheibenhöhen unter der Brüstung. Bis dahin fährt die Scheibe als Ganzes, darunter wird sie gestaucht. Der Gesamtweg wächst entsprechend (`WindowDepth + WindowSink`).
- Verdeck: `V` wirkt nur, wenn `[styleN]` ein animiertes Verdeck-Teil mit genau der animierten Variante nennt. Sonst steht im Log „Style ohne Verdeck“.
- Scheiben ohne offene Variante (kein `[Windows]`-Eintrag): Die Scheiben fahren nur in die Tür und bleiben dort gestaucht, ohne Varianten-Wechsel. Nach einem Neuaufbau (Verdeck, Spiel) senkt das Plugin sie im nächsten Bild wieder ab.

**Ergebnis v5d (👤):** Größtenteils gut. Bei einigen Coupés fährt nur die innere Scheibe herunter, die äußere bleibt sichtbar geschlossen. Das liegt am Modell (Außenscheibe offenbar nicht Teil 35) und lässt sich nicht global lösen. Der Nutzer stellt `WindowSink = 0.3` ein. Wunsch: Die Scheibe soll auf dem Weg nach unten leicht nach vorn wandern.

**v5e** (MD5 `5f1e9e8b…`, installiert): `WindowForward` (Standard 0,1 Scheibenhöhen, an der untersten Stellung der Oberkante) verschiebt jeden Vertex im Verhältnis zu seinem tatsächlichen Weg auf der Längsachse. Gekappte Vertices wandern nicht weiter. „Vorn“ = −z (`WindowForwardSign = -1`), abgeleitet aus dem Heckspoiler des Boxsters bei z +1,94.

**Ergebnis v5e (👤):** passt. Der Nutzer setzt `WindowSink = 0.33`, `WindowForward = 0.15`, `RoofTime = 2200` und die Lautstärken auf 50 % und gliedert die INI in Grund-, Experten- und Debug-Einstellungen. Diese Werte und diese Gliederung sind jetzt Standard: Code-Vorgaben, `gen_windows_ini.py` und `nfsp_carparts.ini` (MD5 der ASI `d677373b…`).

### 6.7 Version 6: Fahrzeug-Seite des Lichts (9.8)

Ab v6 übernimmt die ASI auch die fahrzeugbezogenen Teile des Lichts (Aufteilung: `LICHT.md` §8.2a).

**v6a** (MD5 `32bf955d…`, noch als `nfsp_carparts.asi`): liest im Rennen das Baujahr jedes Autos aus `carSpecs` (`car + 0x760`) und schreibt es ins Log (`Baujahr: …`); Export `NfspPlayerCarYear()` (ab v6b `LfspPlayerCarYear()`). Fenster und Verdeck unverändert gegenüber v5e.

**Ergebnis v6a (👤):** Baujahre im Rennen richtig (356 B, 959, GT3; `LICHT.md` §8.2).

**v6b** (MD5 `8272ecf3…`, installiert): nur Umbenennung in LFSP (Dateien siehe §6). Alte Dateien `nfsp_carparts.asi`/`.ini` liegen in `Game CD/_backup/` (`*.v6a`), die Geräusche sind umbenannt. Nicht beide ASIs gleichzeitig in den Spielordner legen, sonst hooken zwei Plugins dieselben Stellen.

**v6c** (MD5 `49ab6404…`, installiert): Abschnitt `[Lights]` (Lichtfarbe nach Baujahr, 9.9) und Export `LfspHeadlightTint`; Herleitung `LICHT.md` §8.3a. `gen_windows_ini.py` schreibt den Abschnitt mit.

**v6d** (MD5 `8962d3c5…`, installiert, 👤 getestet): Export `LfspHeadlightTintForCar(car, rgb)` für die Gegner-Scheinwerfer (9.11, `LICHT.md` §8.4a).

**INI ab 9.11b:** `lfsp.ini [Lights]` enthält auch die Licht-Schalter, die `dx11z.dll` auswertet (`Headlights`, `HeadlightsDay…`, `HeadlightsMap`, `HeadlightsOpponents…`), siehe `LICHT.md` §8.2a. `gen_windows_ini.py` schreibt sie mit.

**v6e** (MD5 `520c9734…`, installiert; v6d in `Game CD/_backup/`): Fernlicht-Taste `[Lights] HeadlightsHighKey` (Standard K) mit Überblenden, Export `LfspHighBeam()` (9.10, `LICHT.md` §8.4b).

**v6h** (MD5 `63b374af…`, installiert; enthält auch v6g der B19-Sitzung, `TrackDetail`): Lichthupe setzt bei gedrückter Fernlicht-Taste den Lichtschalter des Spiels (`car + 0xD87` Bit 0), damit Klappscheinwerfer öffnen; `[Lights] HeadlightsFlashSwitch` (`LICHT.md` §9.4).

**v6i** (MD5 `dde76b64…`, installiert, 👤 getestet; enthält v6g der B19-Sitzung): Lichthupe öffnet Klappscheinwerfer über den Klappen-Kanal statt über den Lichtschalter; `[Lights] HeadlightsFlashPopUps` (`LICHT.md` §9.4).

**v6k** (MD5 `f6603e4d…`, installiert; enthält v6j der B19-Sitzung): HUD-Instrumente folgen dem Lichtschalter, `[Lights] InstrumentsFollowLights` (11.3, `LICHT.md` §10).

**v6k** stürzte beim Rennstart ab (Instrumente, `LICHT.md` §10.3). **v6m** (MD5 `b4d3f393…`, installiert; enthält v6j der B19-Sitzung und v6l der Kamera-Sitzung): Bildnummern-Fix für 11.3; `InstrumentsFollowLights` standardmäßig 0.

**Ergebnis 11.3b (👤, mit v6r):** Instrumente folgen dem Lichtschalter ohne Absturz; `InstrumentsFollowLights` jetzt standardmäßig 1.

**v6u** (MD5 `1353e6e5…`, installiert): INI aufgeräumt. Die Vorlage `lfsp.ini` übernimmt die Werte des Nutzers (`WindowTime` 1200, `RoofVolume` 40, Baujahr-Stützpunkte 1950/1975/1995/1997, `CockpitFov` 85, `CinematicKey` leer), Code-Vorgaben gleich. `ActionImpact` steht bei den Action-Stärken. Die Fenster-Tabelle ist fest eingebaut; `gen_windows_ini.py` ist durch `gen_windows_table.py` ersetzt, die Vorlage wird von Hand gepflegt.

**v6v** (MD5 `12842abe…`, installiert, v6u in `Game CD/_backup/`): Leuchten-Abfrage `0x43D8C4`/`0x43DAC9` merkt sich Lage und Farbe jeder Leuchte, Export `LfspModelLamps` für den Blinker-Schein von dx11z (11.5d, `LICHT.md` §9.6).

**v6x** (MD5 `5c1700ba…`, installiert, v6w in `Game CD/_backup/`): Blinker hinten im Takt der vorderen (`0x43943A`/`0x439472`, `[Lights] SignalsInSync`, 11.5f, `LICHT.md` §9.6).

**v7g** (MD5 `97b8f868…`, installiert, v7f in `Game CD/_backup/`): Menü-Listen mit Mausrad und Pfeiltasten scrollen (`[Menu] Scroll`, `WheelRows`; 10.1, `MENU.md` §1.5).

**v7h** (MD5 `5fef2a3b…`, installiert, v7g in `Game CD/_backup/`): Scroll-Grenze nach unten aus der Höhe jedes Listeneintrags; v7g blieb in der Autoliste 3 Zeilen vor dem Ende stehen (`MENU.md` §1.5).

**v7i** (MD5 `caff8158…`, installiert, v7h in `Game CD/_backup/`): Scroll-Grenze = Anzahl − 1 des Scroll-Accessors wie beim Ziehen der Leiste; v7g/v7h zogen die letzte Seite doppelt ab (`MENU.md` §1.5).

**v7j** (MD5 `4ff6880a…`, installiert, v7i in `Game CD/_backup/`): Mausrad über einer zugeklappten Liste wählt den nächsten/vorherigen Eintrag (`[Menu] WheelSelect`, 10.2, `MENU.md` §1.6).
