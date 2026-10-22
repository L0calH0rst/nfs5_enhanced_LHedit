# INI-artige Textformate (Aufgabe 1.4)

**Status:** dokumentiert · **Datum:** 2026-10-14
**Beispiele:** `Game CD/GameData/Track/*.map`, `Game CD/GameData/CarModel/*.clr`, `*.tpg`,
`Game CD/FEData/Layouts/*.lay`, `Game CD/FEData/Controls/*.ctr`.

> Alle diese Formate sind **Windows-INI-artiger Klartext** (CRLF-Zeilenenden, `[Abschnitt]`-Köpfe,
> `key=value`-Paare). Sie sind mit einem simplen Parser les-/schreibbar – kein Binär-RE nötig.
> Werte sind oft kommaseparierte Float-/Hex-Tupel.

---

## 1. `.map` — Strecken-Textur-Mapping

Beispiel: `GameData/Track/coastal.map` (Beleuchtung/Textur je Strecken-Slice).

```ini
[header]
count=24
tint=1.000000,0.251656,0.251656,0.251656        ; RGBA-Tönung
ambient=1.000000,0.993378,0.993378,0.993378
interior=1.000000,0.980132,0.980132,0.980132
lightdecal=1.000000,1.059603,1.059603,1.059603

[section0]
startslice=0
endslice=0
texture1=542722899                                ; 32-bit Textur-Name (Hash)
clamp1=0
clamp2=1
tint1=1.000000,1.000000,1.000000,1.000000
tint2=1.000000,1.000000,1.000000,1.000000
texture1.open=827601497
texture1.covered=827601497
texture2.open=827601497
texture2.covered=827601497
texture3.open=844378713
texture3.covered=844378713
texturesize=6.000000
aspect=1.000000
```

| Schlüssel | Bedeutung |
|---|---|
| `[header]` `count` | Anzahl `[sectionN]`-Blöcke |
| `tint/ambient/interior/lightdecal` | RGBA-Farb-Tupel |
| `[sectionN]` `startslice/endslice` | Slice-Bereich der Strecke |
| `texture1/2/3` (+ `.open`/`.covered`) | Textur-Referenzen als **32-bit Hash** (Dezimal) |
| `clamp1/clamp2`, `tint1/tint2` | Clamping + Tönung |
| `texturesize`, `aspect` | Textur-Skalierung |

> ⚠️ Offen: Die Hash-Funktion der Texturnamen (`542722899` → FSH-Ressourcenname) ist noch
> nicht bestimmt. Für HD-Texturen ist das relevant, falls `.map`-Referenzen angepasst werden müssen.

## 2. `.clr` — Fahrzeug-Lackfarben

Beispiel: `GameData/CarModel/356a.clr` (Farbvarianten pro Lack-Set).

```ini
[section1]
stripepackage=2
color1.h=51      ; HSV
color1.s=13
color1.v=255
color2.h=0
color2.s=255
color2.v=255
color3.h=94
color3.s=0
color3.v=0
color1.r=252     ; RGB (redundant zu HSV)
color1.g=255
color1.b=242
```

Jeder `[sectionN]` = eine Farbvariante mit `color1..4` jeweils als **HSV** (`.h/.s/.v`) **und**
**RGB** (`.r/.g/.b`). `stripepackage` wählt das Streifen-/Dekor-Paket.

## 3. `.tpg` — Fahrzeug-Textur-Seiten

Beispiel: `GameData/CarModel/356a.tpg` (verknüpft die `*.fsh`-Texturen des Autos).

```ini
[header]
numfiles=10
numtpages=11
file1=exterior.fsh
file2=interior.fsh
file3=extint.fsh
file4=glass.fsh
file5=intglass.fsh
file6=shadow.fsh
file7=cabrio.fsh
file8=price.fsh
file9=suit.fsh
file10=head.fsh
numstyles=12
drivercampos=-0.289064,0.397706,-0.249983
drivercamrotstyle1=1.549999,0.000000,0.000000

[damagezones]
width=2.145454
length=4.393939
frontwidth=21.118011
frontlength=50.931675

[tpage1.details]
width=256
height=256
mirrortpage=2
```

- `[header]` listet die `*.fsh`-Dateien (`fileN`) und Fahrer-Kameraposition/-rotation je Style.
- `[damagezones]` = Fahrzeug-Schadens-Hitbox.
- `[tpageN.details]` = Textur-Seiten `N=1..11` (Größe, `mirrortpage` für gespiegelte Seiten).

## 4. `.lay` — UI-Layout (Frontend)

Beispiel: `FEData/Layouts/accountlogin.lay`.

```ini
[section.0]
id=REAL.BLACK_640x480
name=ID_SCREEN
size=640,480
color=0xff000000           ; AARRGGBB
screen=ID_MOVIEOVAL
room=ID_NONE
callbacklist=ID_ACCOUNTLOGINSCREEN
musicfile=menu*

[section.1]
id=NFS5.GLINTYLINE
name=ID_GLINTYLINE
state=normal
pos=320,409
size=1,40
color=0x80000000           ; 0x80… = halbtransparent
transon=BOTTOM
transoff=SAME
horizontal=0

[section.3]
id=NFS5.WORDWRAPCONTROL
name=ID_WordWrapControl
pos=182,309
size=278,36
localID=10011
align=1
fontPathName=Univers8b.ffn
foreColor=0xcc000000
stringID=10011
```

| Feld | Bedeutung |
|---|---|
| `[section.N]` `id` | Steuerelement-/Screen-Typ (z. B. `NFS5.GLINTYLINE`) |
| `name` | Text-ID (`ID_…`) |
| `pos`, `size` | Position/Größe in Pixel |
| `color`, `foreColor` | `AARRGGBB`-Hex (Alpha im hohen Byte) |
| `state`, `transon/transoff` | Zustand + Übergangsanimation |
| `fontPathName` | Font-Datei (`.ffn`) |
| `screen`, `room`, `callbacklist` | Verlinkung zu Screens/Räumen |

> Für HD/HUD-Skalierung (Phase 3A) sind `pos/size` dieser `.lay`-Dateien relevant.

## 5. `.ctr` — UI-Steuerelement-Definitionen

Beispiel: `FEData/Controls/*.ctr`. INI-artig; Dateien beginnen oft mit einem
Kommentar-Block aus `;;;`-Zeilen (Autor/Beschreibung). 73 von 78 Dateien sind nicht leer;
einige (z. B. `3DTrack.Ctr`) sind bewusst leere Platzhalter.

> Struktur im Detail noch nicht ausgewertet (geringe Relevanz für HD-Texturen/Auflösung);
> dokumentiert, sobald Phase 3A/3B es erfordert.

---

## Gemeinsames Parsing-Modell

```
Datei =  (KommentarZeilen)* ( "[Abschnitt]"\n key=value* )+
key=value : name = Zahl | "x,y,z,…" | 0xAARRGGBB | Text
Zeilenende: CRLF · Kommentar: ';' oder ';;'
```
