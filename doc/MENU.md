# MENU.md – Bedienung im Frontend (Phase 10)

> Themen-Dokument: Herleitung und Umsetzung der Menü-Komfortfunktionen. Status und Aufgaben stehen in `PLAN.md`, kurze Fakten in `knowledge.md`.
> Quellen: Ghidra-Export `porsche.exe`, Einzel-Dekompilate `work/re/10.1_*.c` (lokal), RTTI und vtables aus `Game CD/Porsche.exe`.

## 1. Aufgabe 10.1: Listen mit Mausrad und Pfeiltasten scrollen

Gemeldet 2026-10-17: Listen (Fahrzeugauswahl, Lackauswahl, Strecken) lassen sich nur durch Ziehen der Scrollleiste bewegen. Mausrad und Pfeiltasten wirken nicht.

### 1.1 Aufbau der Menüs

- Die Bildschirme (`FEData/Layouts/*.lay`) setzen sich aus Steuerelement-Typen (`FEData/Controls/*.ctr`) zusammen. Fahrzeug-, Lack- und Streckenauswahl (`dealership.lay`, `paintshop.lay`, `location.lay`, `garage.lay`) verwenden **`NFS5.ComboBoxSlider`**: eine Aufklappliste mit Scrollleiste. 16 Bildschirme verwenden zusätzlich **`NFS5.ListBox`** (laut `ListBox.ctr` eine ComboBox, die immer offen ist).
- Die C++-Klassen dazu stehen per RTTI im Binary: `FEComboBox` (vtable `0x5B5C24`), `FEListBox` (`0x5B611C`). Die Scrollleiste der Liste ist ein `FEEmbossedVerticalSlider_MainFunctionality` (`0x5B6A64`, Objekt in `+0x1B8`).
- Das Frontend gehört zur EA-Laufzeit „FERT“: `EventPump@FERT` (vtable `0x5BB150`) fragt in `FUN_00529660` je Durchlauf die Eingaben ab (Slot 5, `0x529700`) und verteilt die Ereignisse (Slot 9, `0x529990`).

### 1.2 Ereignisse

`0x529700` erzeugt aus dem Zustand von Tastatur und Maus diese Ereignisse (Objekt: `+4` Typ, ab `+8` Daten):

| Typ | Klasse | Daten |
|---|---|---|
| 2 | `KeyPressEvent@FERT` | `+8` Tastencode des Spiels (`FUN_0053b530`: Pfeil hoch `0x4800`, runter `0x5000`, Bild hoch `0x4900`, Bild runter `0x5100`, Pos1 `0x4700`, Ende `0x4F00`; Ziffernblock gleich) |
| 4 / 8 | `MouseButtonEvent@FERT` | gedrückt / losgelassen; `+8` Taste, `+0xC` x, `+0x10` y |
| 7 | `MouseMoveEvent@FERT` | `+8` x, `+0xC` y (aus `0x69E07C`/`0x69E080`) |
| 9 | `MouseWheelEvent@FERT` | `+8` z absolut |
| 10 | `TimerEvent@FERT` | – |

Die Steuerelemente verteilen über `FUN_00525f80` (vtable Slot 3) auf ihre Slots: Typ 2 → `+0x10`, 4 → `+0x14`, 5 → `+0x18`, 6 → `+0x1C`, 7 → `+0x20`, 8 → `+0x24`, 10 → `+0x28`.

### 1.3 Ursache

1. **Mausrad:** Das Spiel liest das Rad durchaus. `FUN_00536e50` meldet für `WM_MOUSEWHEEL` den Handler `0x537020` an. Er addiert Rastungen nach `0x69E0A8`, und `FUN_005372a0(6)` führt daraus die z-Achse `0x69E084` (begrenzt auf 0…100). Der Verteiler `FUN_00525f80` kennt **Typ 9 aber nicht** (Sprungtabelle nur für 2, 4–8, 10). Kein Steuerelement erhält das Rad.
2. **Pfeiltasten:** Der Tasten-Slot `+0x10` ist bei `FEComboBox`, `FEListBox` und den Slidern `0x4D1B40` (`xor al,al; ret 4`, also „nicht behandelt“). Eigene Tasten-Handler haben nur Eingabefelder (`FEInputBox` `0x4CCC20`, `FEChatInputBox` `0x4C3310`) und wenige Bildschirme.

### 1.4 Wie die Scrollleiste die Liste bewegt

- Die Liste hat zwei Accessoren: Daten `+0x144` (Einträge, Auswahl) und Scroll `+0x1BC` („`<Name>.Sliderbar`“, aktueller Wert = oberste sichtbare Zeile). `+0x1DC` = Scrollleiste vorhanden (Daten höher als die Box `+0x1C0`, `FUN_004c53a0`), `+0x1DD` = FEListBox (immer offen), `+0x10C` Bit `0x40` = aufgeklappt.
- Accessor-Slots (`FEPositionableAccessor`, vtable `0x5B52E4`): `+0x24` Anzahl, `+0x28` aktuell, `+0x2C` setzen (`0x4BC240`, begrenzt auf 0…Anzahl−1), `+0x4C` verschieben um n, `+0x64` Gesamthöhe (Standard 14 Pixel je Eintrag). Alle Varianten der Lese-Slots nehmen keine Argumente (geprüft an den `ret` aller Accessor-vtables).
- Pfeil-Knopf der Leiste (`FUN_004db040`, Zustand 1/2): Scroll-Accessor `+0x4C(±1)`, danach Slider `+0x2C` (`FUN_004db220`, Knopf nachführen). Ziehen (`FUN_004db9b0`) setzt den Accessor mit `+0x2C`.
- Hover-Prüfung der Liste (Ende von `0x4C6F60`): Teil „selectionbox“ `+0x178`, zugeklappte Box `+0x17C` (je `FUN_004e2970(Teil, Rechteck, x, y)` mit dem Rechteck aus `FUN_004c8670`) oder Scrollleiste unter der Maus (`FUN_004dba50`).

### 1.5 Umsetzung (LFSP v7g–v7i, `[Menu]` in `lfsp.ini`)

**Umzug 2026-10-21:** Die Menü-Listen liegen jetzt in nfs5_enhanced (LHedit, Subsystem `MenuScroll`, `src/subsystems/menu/`, Commit `54cb3b8`), Schalter `[Menu]` in `nfs5_enhanced.ini` mit denselben Namen und Vorgaben. Gleiche Hooks und Adressen wie unten. LFSP v7s enthält den Teil nicht mehr. Gebaut, im Spiel noch nicht getestet.

| Stelle | Art | Zweck |
|---|---|---|
| `0x537020` | Inline-Hook (6 Byte, stdcall `ret 0x18`) | Rad-Ausschlag (`WHEEL_DELTA`-Einheiten) zusätzlich für LFSP zählen; das Spiel arbeitet unverändert weiter |
| `0x5B5C24`/`0x5B611C` Slot 8 | vtable | nach der Mausbewegung des Spiels merken: Liste unter der Maus, aufgeklappte Liste |
| `0x5B5C24`/`0x5B611C` Slot 0 | vtable | beim Abbau die gemerkten Zeiger löschen |
| `0x5BB150` Slot 5 | vtable | nach der Eingabe-Abfrage die Rastungen auf die Liste unter der Maus anwenden (sonst auf die aufgeklappte); `WheelRows` Zeilen je Rastung |
| `0x5BB150` Slot 9 | vtable | Tastendruck (Typ 2) an die aufgeklappte Liste oder an die offene Liste unter der Maus; verwendet LFSP die Taste, sieht das Menü sie nicht |

Verschieben wie der Pfeil-Knopf: Scroll-Accessor `+0x2C(neu)`, Slider `+0x2C()`, danach `FUN_004d6140` (Mausbewegung nachschicken, damit die Markierung unter der Maus stimmt). Der Scroll-Accessor zählt nur die möglichen obersten Zeilen (Einträge − letzte Seite + 1). Darum reicht die oberste Zeile wie beim Ziehen der Leiste bis Anzahl − 1, und das Spiel zeigt dort den letzten Eintrag unten. Eine Seite (Bild hoch/runter) = Einträge, die ab der obersten Zeile in die Box (`+0x1C0`) passen, gerechnet mit der Höhe jedes Eintrags (Daten-Accessor `+0x68(Index)`, bei allen Varianten 1 Argument).

**v7g → v7h (👤 2026-10-18, Screenshot Autoliste):** Rad wirkt, erreicht aber das Listenende nicht. v7g schätzte die Zeilenhöhe als Gesamthöhe / Einträge: Log `Zeilen 81, sichtbar 9, Zeilenhoehe 17`, sichtbar sind aber gut 11 Zeilen à 14 Pixel. `FEMergedCarListAccessor` (`0x4F4D40`) gibt Zeile 0 zusätzlich 13 Pixel, außerdem zählt die Gesamthöhe mehr als die Zeilen. Deshalb blieb das Rad 3 Zeilen vor dem Ende stehen (oben 72). v7h summierte die Höhen je Eintrag von unten.

**v7h → v7i (👤 2026-10-18):** v7h scrollte noch weniger weit (Log: `Zeilen 81, sichtbar 11, unterste oberste 70, Box 160`; Ziehen der Leiste ergab `oben 80`). Der Scroll-Accessor hat die letzte Seite schon abgezogen, beide Versionen zogen sie ein zweites Mal ab. v7i begrenzt nur auf Anzahl − 1.

Tasten: Pfeil hoch/runter 1 Zeile, Bild hoch/runter 1 Seite, Pos1/Ende Anfang/Ende. Die Pfeiltasten scrollen die Ansicht, die Auswahl ändert sich wie bisher nur per Klick. Hat die Liste keine Scrollleiste, geht die Taste unverändert ans Menü.

Prüfungen gegen falsche Zeiger: Jeder gemerkte Zeiger muss noch eine der beiden vtables tragen, sichtbar sein (`FUN_004c7e40`) und offen sein. Die Scrollleiste muss die vtable `0x5B6A64` haben, sonst schreibt LFSP einmal eine Warnung ins Log und scrollt nicht. Mit `Measure = 1` steht jede Bewegung im Log (`Menue-Liste …: oben a -> b (Positionen, Eintraege, sichtbar, Box)`).

### 1.6 Zugeklappte Liste: Rad wählt den Eintrag (10.1d, LFSP v7j)

Wunsch 👤 2026-10-18: Mit der Maus auf die zugeklappte Liste zeigen, Rad nach unten = nächster Eintrag, nach oben = vorheriger. `[Menu] WheelSelect = 1`.

- Ziel: zuletzt gemerkte Liste, zugeklappt, keine FEListBox, aufklappbar (`+0x110` Bit `0x40`, Bedingung von `FUN_004c7d70`), sichtbar, und die Maus steht auf der zugeklappten Box (`+0x17C`). Ist eine Liste aufgeklappt, scrollt das Rad weiter diese.
- Bei zugeklappten Listen zählt in der Mausbewegung nur noch diese Box als „unter der Maus“, nicht die unsichtbare Aufklappfläche.
- Auswählen wie ein Klick auf einen Eintrag (`0x4C6AF0`): Daten-Accessor `+0x2C(Index)`, ein Eintrag je Rastung, am Anfang und Ende kein Umlauf. Farblisten (`+0x1DE`): Den Eintrag „eigene Farbe“ (`FEColourAccessor`, `FUN_004b94b0`) überspringt das Rad, ein Klick darauf öffnet den Farbwähler. Klick-Geräusch und Umschalt-Modus (`+0x1E0`) bildet LFSP nicht nach.
- Log mit `Measure = 1`: `Menue-Liste … zugeklappt: Auswahl a -> b (Eintraege n)`.

### 1.7 Test (👤)

v7g: Mausrad wirkt in der Autoliste, Ende nicht erreichbar → v7h. v7h: noch weniger weit → v7i. ✅ v7i: funktioniert (👤 2026-10-18). v7j (Rad auf zugeklappter Liste) offen. Erwartung: In Fahrzeug-, Lack- und Streckenauswahl die Liste aufklappen. Das Mausrad über der Liste bewegt sie um 3 Zeilen je Rastung, die Pfeiltasten um eine Zeile, Bild hoch/runter um eine Seite. Ein Klick wählt wie bisher.
