# SICHTWEITE.md – Größere Sichtweite und Dunst (Phase 13)

> Themen-Dokument zum Auftrag `plan_pvs_haze_1.md` (👤 2026-10-21, „Erweiterte Offline-PVS + atmosphärischer Dunst“).
> Status und Aufgaben stehen in `PLAN.md` Phase 13, belegte Fakten in `knowledge.md`.

## 1. Abgleich des Auftrags mit dem Projektstand

Der Auftrag geht von Annahmen aus, die für NFS5 und dieses Projekt nicht zutreffen. Wie im Auftrag selbst vorgesehen („bei Widerspruch gilt der Projektplan“), sind die Abweichungen hier festgehalten:

| Annahme im Auftrag | Stand im Projekt | Folge |
|---|---|---|
| Proxy-DLL `dinput.dll` + Treiber `opengl3.dll`, Konfig `opengl3.ini` | Der Loader `dinput.dll` lädt die ASIs `nfs5_enhanced.asi` und LFSP. Als Grafiktreiber dient `dx11z.dll` (Direct3D 11). Neue Schalter kommen nach `lfsp.ini` | Teil B wird in dx11z umgesetzt, die Schalter kommen nach `lfsp.ini` |
| Strecke = Blöcke mit gespeicherter Sichtbarkeits-/Nachbarliste (wie NFS4-FRD) | Bisher ist keine solche Liste bekannt. Die Engine wählt zur Laufzeit aus, Kette in §2 | Eine „PVS“ im Sinne des Auftrags gibt es so nicht. Ihre Rolle übernehmen die 2D-**Verdeckungswände** der Karte |
| Sichtweite ~150–200 m | Sichtweite = `700 × DDS` (`0x44F1A0`). DDS 3 ist stabil, ab DDS 4 ändert sich das Verhalten (B2) | Das Ziel 400–600 m liegt im heutigen Bereich. Es fehlt nicht an Reichweite, sondern an der **Korrektheit** der Auswahl (B20/B21) |
| „Löcher in Felswänden“ | entspricht B21 (Loch durch den 2D-Wandtest, behoben in der Nähe ≤ `TrackOcclusionRange`) und B20 (ferne Insel durch eine Lücke zwischen Wänden, gemildert mit `TrackOcclusionEdge`) | Teil A ist im Kern die saubere Lösung für B20/B21 |
| Engine transformiert im Treiber, Eye-Space im Vertex-Shader | Die Engine liefert transformierte Vertices (`XYZRHW`). Eye-Tiefe = `1/rhw` (so misst schon `DistanceKey`) | Für den Dunst `1/rhw` verwenden, keine Matrizen nötig |

## 2. Auswahl der Strecken-Geometrie zur Laufzeit (belegt)

Quellen: `BUGS.md` §2, §4.3, §16–§18.

1. **Sichtweite** `0x6197CC` = `700 × max(1, DDS)` (`FUN_0044f1a0`). Far-Clip = Sichtweite × `FarClipMultiplier`, höchstens 20 000 (`0x4677FC`, Reset `0x45D501`).
2. **Chunk-Auswahl** `FUN_0047f550` (aus `FUN_0047fb40`, Frame `FUN_00468ae0`): 2D-Sichtkeil aus Sichtweite und Halb-FOV (Hor+-Fix `WorldCullVolume`). Markiert die Verdeckungswände im Umkreis als aktiv (`FUN_004873b0`, Wände bei `map+0x2C`, je 0x1C Byte: Punkt `+4/+8`, Richtung `+0xC/+0x10`, Byte 0 = Art).
3. **Objekt-Test** `FUN_0047f890`: Kugel gegen das EAGL-Sichtfeld (`FUN_00539480`), dann **Wandtest** `FUN_00488b50`: Linie Objektmitte → Kamera in der Draufsicht gegen jede aktive Wand. Kreuzt sie eine, fällt das Objekt weg. Höhe zählt dabei nicht. Danach folgt die Entfernung: Flag `0x400000` bis 600 m, `0x20` bis 700 m, sonst Abstand − Radius < Sichtweite.
4. **Detailstufe je Abschnitt** `FUN_00450de0`: Die erste Stufe mit Reichweite² > Entfernung wird genommen. LFSP `TrackDetail = 1` erzwingt Stufe 0, `TrackRange = 1.5` streckt die Reichweite.
5. Die EAGL-Pipeline schneidet die Dreiecke auf der CPU. Sie verliert dabei nachweislich nichts (B21, H-B21i widerlegt).

## 2a. Herkunft und Format der Verdeckungswände (13.2 ✅)

Quelle: Ghidra-Export `Porsche.exe.c`, Dateien `GameData/Track/*.crp`, entpackt mit `work/track/qfs.py`.

- **Laden:** Beim Rennstart legt die Strecken-Ladefunktion das Strecken-Objekt `DAT_00628960` an (`FUN_004874a0`, 0x3C Byte) und lädt die Strecke (`FUN_004876e0`, Fehlertext „Couldn't load track“). `FUN_00488a60` sucht im Container `[obj+4]` den Eintrag mit der Kennung `0x56697320` („Vis “, `FUN_0059f7c0`) und baut daraus die Wände.
- **Container:** `<strecke>.crp` ist QFS-komprimiert, entpackt ein `Trak`-Container (Magic `kraT`). Inhaltsverzeichnis mit Einträgen zu 16 Byte: Kennung, Flags, Anzahl, Versatz. Bei Flag-Bit 1 (`& 2`) zählt der Versatz ab dem Eintrag. alps: Eintrag bei `0x7290`, Flags `0x19C8FA`, 275 Wände, Daten ab `0x5CBEA0`.
- **Wand in der Datei:** 24 Byte = zwei Punkte `(x, y, z)` als float. Die Wände bilden Polygonzüge (Endpunkt = nächster Anfangspunkt). **`y` ist keine Höhe**, sondern immer 0.0 oder 1.0 (beide Punkte gleich) und dient als Art.
- **Wand im Speicher** (`FUN_00487330`, 0x1C Byte): `+0` Byte Art = (`p0.y ≠ 0`), `+4/+8` = `p0.x/p0.z`, `+0xC/+0x10` = `p1 − p0`, `+0x14` = Länge², `+0x18` = 1/Länge². Verwaltung: `+0x28` Anzahl, `+0x2C` Wände, `+0x30` Aktiv-Bytes (`FUN_00488b00` setzt sie über `FUN_004873b0`, Abstand Punkt–Strecke gegen den Kreis aus `FUN_0047f550`).
- **Wirkung der Art** (`FUN_00488b50`, Aufruf `FUN_0047f890` mit `param_3 = (Objekt-Flags & 0x400020) ≠ 0`): Normale Objekte fallen an **jeder** Wand weg. Ferne Objekte (Flags `0x400000`/`0x20`, die bis 600/700 m gezeichnet werden) fallen nur an Wänden der Art 1 weg.
- **Bestand je Strecke** (Wände gesamt / davon Art 1): alps 275/21, autobahn 62/0, canyon 185/0, castle 403/13, coastal 48/1, farmland 80/0, foothills 112/10, forest 175/0, industrial 160/0, monaco1–5 je 97/0, skidpad ohne `Vis `.

**Folge für 13.3:** Die Wände haben keine Höhe, also kann Variante A-a die Höhe nicht aus den Wänden nehmen. Möglich sind dann:
- **A-a′:** eine Höhe je Wand offline aus der Strecken-Geometrie bestimmen (höchster Punkt des Geländes entlang der Wand) und LFSP als Zusatztabelle mitgeben.
- **A-b:** echte Sichtbarkeit offline berechnen.

Beides braucht das Geometrie-Format im `Trak`-Container. Die Wände selbst lassen sich gezielt ändern (verschieben, Art umstellen, Lücken schließen wie bei B20), entweder in der Datei oder von LFSP nach `FUN_00488a60`.

## 2b. Trak-Container und Strecken-Geometrie (13.3a)

Quelle: `GameData/Track/*.crp` (alle 15 Strecken geprüft), `FUN_0059f7c0`/`FUN_0059f870`, Werkzeuge `work/track/trak.py`, `trakgeo.py`, `trak_obj.py`.

**Eintrag (16 Byte):** Kennung (4 Byte, rückwärts gespeichert, z. B. `kraT`), Info, Anzahl, Versatz. Info Bit 1 = Versatz relativ zum Eintrag.
- **Knoten** (`Trak`, `Arti`): Kinder ab `Eintrag + Versatz×16 + (Info >> 5)×16`, Anzahl = Kinder.
- **Blatt:** Daten ab `Eintrag + Versatz` (Byte), Größe = `Info >> 8`, LOD-Stufe = `(Info >> 3) & 3`, Info-Bit 7 = Animations-Frame (`vt` mit Info `0x83`).
- `Trak` (Eintrag 0) überspringt mit `Info >> 5` die Objekt-Liste: Die Einträge 1…N sind die `Arti`-Objekte (alps 1752), dahinter folgen die Kinder von `Trak`: `CmAn`, `Desc`, `RMth`, `SimD`, `SimT`, `Vis `, `mt` (Materialien), `sn`.

**Objekt `Arti`, Blätter:** `Base` (112/136 Byte, u. a. Hüllvolumen, noch nicht entschlüsselt), `Name` (Text, z. B. `RD1136C (1 PRIMARY)`, `CNK…`, `OBJ…`), je LOD-Stufe `vt` (x, y, z, 1.0 als float, 16 Byte), `uv` (u, v, 8 Byte), `df` (Farbe BGRA, 4 Byte), dazu `pr` (Primitiv) je Stufe und Nummer, optional `tr` (4×4-Matrix, Zeilenvektor, Verschiebung in Zeile 3, auch mit Skalierung), `Anim`/`Anqt`, `ef`. Ohne `tr` stehen die Vertices in Weltkoordinaten (Y oben).

**Primitiv `pr`:** Anzahl = Ecken.
| Versatz | Inhalt |
|---|---|
| `+0` | Wort Art, belegt im Clip-Test des Spiels (`FUN_005408f0`, Sprungtabelle `0x540BA4`: Art 1 → `0x540A48`, 2 → `0x540B53`, 3 → `0x540AA0`, 4 → `0x540AF8`): 1 = Dreiecks-Strip (Fenster rückt um 1, mit entarteten Übergängen), 2 = Linien (um 2), 3 = Dreiecksliste (um 3), 4 = Vierecke (um 4; Ecken im Umlauf → 0-1-2, 0-2-3). Bis 2026-10-21 waren im Werkzeug Art 1 und 4 vertauscht → Löcher in Fels-Objekten in Blender (👤-Meldung: Objekte 174, 181, 269, 444) |
| `+2` | Wort Flags |
| `+4` | Material (`0x80000000` + Id) |
| `+8` | Hüllkugel (Mitte, Radius). Enthält alle Ecken (coastal 2765 von 2765) |
| `+0x28`, `+0x2C` | Anzahl Streams, Anzahl Indexlisten |
| `+0x30` | Streams zu 16 Byte: Id, Byte-Versatz im LOD-Blatt, Größe (Wort), **Slot** (Wort: 0 = vt, 1 = unbenutzt, 2 = uv, 3 = df), Wort, Indexliste (Wort, −1 = direkt). Die Bedeutung steht im Slot, nicht in der Id: meist Id 11/10/9, bei manchen Modellen und Wasserflächen 9/8/−1 (korrigiert 2026-10-21 nach 👤-Meldung fehlender Flächen in Blender; seitdem alle 74 300 Primitive aller Strecken vollständig zugeordnet) |
| danach | Indexlisten zu 8 Byte: `01 00`, Name (`Iv`, `Iu`), Versatz; danach je Liste *Anzahl* Byte-Indizes |

Indizierte Primitive (3 Streams, `Iv`/`Iu`) sind der Normalfall (72 614), direkte (4 Streams) gibt es vor allem bei Modellen (Hubschrauber, Kisten). Die früher als „ohne `vt`“ gezählten Primitive (alps 147, coastal 42, z. B. Wasser `WATER_MID2_*`, `BOX01`) haben ihre Streams nur unter anderen Ids; nach Slot zugeordnet sind sie vollständig.

**Werkzeug Blender (`work/track/trak_obj.py`):** Export nach OBJ (ein Objekt je `Arti`, Material je Textur-Id, `v` = 1 − v) plus `.map.json`. Der Import schreibt Positionen (mit `--uv` auch Texturkoordinaten) über die Dreiecks-Zuordnung zurück, vergrößert die Hüllkugeln der Primitive bei Bedarf und packt neu (QFS). Die Topologie bleibt fest. Doppelte Flächen (gleiche drei Vertices, z. B. Rückseiten) verwirft Blender beim Import; der Import erkennt das und ordnet trotzdem zu. Round-Trip ohne Änderung ist byte-genau (alps, coastal, monaco1, castle). 👤 2026-10-21: coastal in Blender bearbeitet, die neu gepackte .crp lädt im Spiel.

**`Base` (112/136 Byte):** `+0x14` Box-Minimum, `+0x20` Box-Maximum (je 3 float), `+0x2C` Mitte (3 float + 1.0), `+0x3C` und `+0x40` Radius (zweimal gleich); Rest offen. Box = Min/Max der Vertices (coastal geprüft).

**Neue Topologie (Import, 13.3b):** Ändert sich die Flächenzahl eines Objekts, baut `trak_obj.py` seine Stufe LOD 0 neu auf:
- neue `vt`/`uv`/`df`-Blätter und Dreieckslisten (Art 3, `Iv`/`Iu`) je Material, höchstens 255 Vertices/UVs und **252 Ecken** je Primitiv (wie in allen Original-Strecken);
- der Kopf des Primitivs wird von einem Original-Primitiv desselben Materials übernommen;
- Hüllkugeln neu, `Base`-Box und Radius werden nur vergrößert;
- neue Blätter und die neue, nach Kennung sortierte Kinderliste (Binärsuche `FUN_0059f5e0`) kommen ans Dateiende, der `Arti`-Eintrag zeigt dorthin; die alten Daten bleiben als ungenutzter Rest;
- Farben aus der OBJ (`v x y z r g b`), sonst vom nächsten Original-Vertex; Alpha immer vom nächsten Original-Vertex.

Nicht neu aufgebaut werden animierte Objekte, Objekte mit Strips/Vierecken oder ohne eigene Vertices, und die gröberen Stufen LOD 1/2 (bleiben alt; mit `TrackDetail = 1` zeichnet das Spiel ohnehin LOD 0). Klonen über `trak_obj.py` geht nur innerhalb eines Objekts; eigenständige neue Objekte siehe §2c. Ein Klon weit weg vom Objekt kann mit ihm zusammen weggecullt werden (Box/Kugel wachsen mit, der Wandtest prüft die Mitte). **Spieltest 1 (👤 2026-10-21) ❌:** Berg unterteilt (114 → 3094 Flächen), Primitive mit bis zu 1014 Ecken → Berg flackert, andere Teile der Strecke verschwinden, Absturz in dx11z (Lesefehler, Aufruf aus dem Clipper `0x54C030`). Ursache: Der Renderer hat feste Puffer je Primitiv (Clip-Codes ab `this+0x44`, 0x3F0 Byte, Vertexzahl `this+4`, `FUN_005408f0`); kein Original-Primitiv hat mehr als 252 Ecken bzw. 255 Vertices. Seitdem teilt der Import bei 252 Ecken. Das größte Original-Objekt hat 39 Primitive und 737 Vertices (LOD 0); darüber warnt der Import (Grenzen dort unbekannt). Geklonte Schilder legte Blender als eigene Objekte `….001` an; der Import hängt solche `Name.NNN`-Objekte seitdem automatisch an ihr Original-Objekt an. **👤-Test 2 (2026-10-21):** unterteilter Tunnelberg (3210 Vertices, 43 Primitive) läuft und sieht deutlich runder aus ✅; Schilder blieben unsichtbar, weil die OBJ noch die getrennten Objekte enthielt (vor dem automatischen Anhängen). **👤-Test 3 (2026-10-21):** Klone in `RIGID`-Objekten (Schilder, Matrix `tr`) sind sichtbar, haben aber **keine eigene Kollision** und hängen fest am Original (bewegen sich mit ihm). Die Kollision kommt also nicht aus der `Base`-Box der Darstellung. Für eigenständige umfahrbare Klone bräuchte es neue Objekte samt Physik-Eintrag (offen); feste Deko besser in ein Strecken-Objekt (`CNK…`) klonen. Material eines anderen Objekts (z. B. Schild in ein Fels-Objekt) übernimmt der Import den Kopf aus jenem Objekt.

Geprüft offline (coastal): Unterteilung 80 → 320 Flächen und Busch-Klon; Geometrie, UVs und Farben kommen beim erneuten Export gleich zurück, alle übrigen 763 Objekte unverändert. Im Spiel noch nicht getestet.

**Offen:** übrige Felder von `Base`, Zuordnung Material-Id → Textur in `<strecke>.fsh`, Primitive ohne `vt`, Art 4 im Code bestätigen.

## 2c. Neue Objekte (13.3c, Prüfung 2026-10-21)

**Wie das Spiel Objekte erfasst** (Ghidra, `work/re/arti_sort.c` lokal):
- Laden: `FUN_004876e0` (`%s.crp`) → `FUN_004879d0`. Diese Funktion zählt die `Arti`-Einträge (`FUN_0059f4d0`, Kennung `0x41727469`), holt den ersten (`FUN_0059f530`) und baut daraus eine Liste aller Objekte.
- Sechs Sortierer (Vtables `0x5B47E0`, `0x5B4718`, `0x5B46B8`, `0x5B477C`, `0x5B4814`, `0x5B4750`; Slot 1 z. B. `0x482970`, `0x4781A0`, `0x4777F0`, `0x47CCE0`, `0x483690`) laufen über die ganze Liste. Sie ordnen jedes Objekt nach Flags aus `Base` (Bits `0x8000`, `0x6000`, `0x2000`) und nach `Anim`/`Anqt` ein und tragen es in die Raumstruktur ein (`FUN_00488db0`, Baum mit 8192 m Kante `FUN_00483800`).
- Eine feste Tabelle mit Objektnummern wurde nicht gefunden. Die frühere Aussage „ein neues `Arti` würde nie gezeichnet“ (§2b) ist damit **nicht belegt**.
- Ein Knoten hat zwei Listen: eine sortierte mit `Info >> 5` Einträgen (bei `Trak` die `Arti`, Binärsuche `FUN_0059f5e0`) und dahinter *Anzahl* Kinder (`FUN_0059f7c0`).

**Testwerkzeug `work/track/trak_clone.py`:**
- Kopiert ein Objekt mit allen Blättern als neues `Arti` am Ende der Liste, mit dem Namen `<Name>+K<n>`. Die bisherigen Nummern bleiben gleich.
- Mit Matrix (`tr`, umfahrbare Schilder) wird nur die Verschiebung geändert, sonst werden Vertices aller Stufen, Hüllkugeln und `Base` verschoben.
- Die `Arti`-Liste und die Kinder von `Trak` wandern ans Dateiende, und `Trak` `Info >> 5` wächst.
- Offline geprüft: Schild 0448 und Busch 0017 auf coastal werden als Objekte 768/769 gelesen und exportiert, und der Round-Trip der übrigen Datei bleibt byte-genau. **👤-Test (2026-10-21) ✅:** Die kopierten Schilder stehen als eigenständige physische Objekte neben den Originalen. Neue `Arti` werden also gezeichnet und bekommen eigene Physik.

**Vorlagen und platzierte Objekte (`Base` +0):** Mit Bit `0x8000` stehen Objekte mit Matrix-Position 0 (Hütchen, `Crate1 RIGID2`, Fahrzeuge …), also Vorlagen, die das Spiel anderswo platziert (vermutlich `.scn`). Ohne `0x8000` sind es platzierte Objekte: Schilder `0x601`, Kisten `CRATE1 RIGID…` `0x711`, Fischer `0x610` (coastal).

**Fremdes Modell (13.3d):** `trak_clone.py … <nummer>:<dx>,<dy>,<dz>:<modell.obj>[:<material>]` kopiert ein platziertes Objekt mit Matrix und ersetzt dessen LOD 0 durch das Modell (Meter, Y oben, Boden y = 0). Die Skalierung der Vorlagen-Matrix wird herausgerechnet, und `Base`-Box/-Mitte/-Radius werden genau auf das Modell gesetzt. Test: `work/track/models/beachball.obj` (UV-Kugel, Ø 1 m, 288 Dreiecke) in zwei Kopien der Kiste 0407 (Material 39 = Kisten-Textur): neben den Kisten am Hafen (+3 m) und 3 m neben dem Schild 0448. Datei `work/track/out/coastal_ball.crp`; offline geprüft (Ø 1 m in Weltkoordinaten). **👤-Test (2026-10-21) ✅:** Ball sichtbar; als Kisten-Kopie zerspringt er beim Anfahren wie eine Kiste, als Schild-Kopie verhält er sich wie ein Schild → das Verhalten (Physik/Zerbrechen) kommt aus der Vorlage, nicht aus der Geometrie. Wo es festgelegt ist (`Base`-Flags `0x711` vs. Schild `0x601`, Name, `SimT`/`SimD`), ist offen.

**Neue Objekte aus Blender (13.3f):** Der OBJ-Import legt jedes Blender-Objekt namens `NEU_<Vorlage>_<Textur>[_<frei>]` als neues `Arti` an (`trak_clone.append_objects`). Ein Objekt `HIT_…` mit gleichem Namensrest gibt die Box in `Base` +0x14 vor (achsparallel in lokalen Koordinaten der Vorlage); ob das Spiel diese Box für die Kollision nutzt, ist noch durch Spieltest zu bestätigen.
- *Vorlage* = Objektnummer eines platzierten Objekts. Mit Matrix (Schild `0x601`, Kiste `0x711`) übernimmt das neue Objekt deren Verhalten; die Mitte der Grundfläche (x/z-Mitte, kleinstes y) wird die Position, Drehung und Skalierung der Vorlage werden herausgerechnet. Ohne Matrix (feste Szenerie, z. B. Busch) bleiben die Vertices in Weltkoordinaten.
- *Textur* = 4-Zeichen-Name in der `.fsh` (neues Material, Bild vorher mit `fsh_add.py`), `mNNN` = vorhandenes Material NNN, `-` = Material der Vorlage. Gleiche Namen teilen sich ein Material.
- Geprüft offline: zwei Bälle (Schild-Vorlage, `bbal` bzw. `m039`) und ein Ball mit Busch-Vorlage; beim erneuten Export liegen alle drei genau an ihrer Blender-Position. Die Funktion erzeugt für den Ball byte-genau die im Spiel getestete Datei.

**Materialien `mt`** (Kinder von `Trak`, Kennung `0x6D74xxxx`, 76 Byte und mehr, coastal 218; Lader `FUN_0044a770`):
- Material-Id im Primitiv = `0x80000000 | Nummer`. Die Tabelle hat *größte Nummer + 1* Plätze.
- `+0x08` Wort: Anzahl Textur-Verweise (je 0x2C Byte).
- `+0x0C`: Wert für den Shader (`FUN_0044bb70`).
- `+0x10`: Shader-Name (`Normal`, `Opaque` …).
- Erster Verweis: `+0x24` = Container-Kennung `sn` (das `Trak`-Kind `sn` enthält den Dateinamen `coastal.fsh`), `+0x28` = 4-Zeichen-Name des Bildes in der `.fsh` (z. B. `crt1` = Material 39).
- Das Spiel lädt die `.fsh` aus `sn` und ordnet die Bilder über (Kennung, Name) zu (`FUN_00537f30`/`FUN_00537f60`).

**Eigene Textur (13.3e):**
- `fsh_add.py` hängt ein PNG/BMP als Typ `0x7D` mit voller Mip-Kette an (`misc[3] = Zusatzstufen << 12`, wie alle 186 Strecken-Texturen); die vorhandenen Einträge bleiben byte-gleich.
- `trak_clone.py … :neu=<name>` legt einen neuen `mt`-Eintrag an: Kopie des Materials der Vorlage, Name an `+0x28` ersetzt, Nummer = größte + 1. Er wird sortiert in die Kinder von `Trak` eingefügt.
- Test: Beachball (256², 6 Streifen, `models/beachball.png`) als `bbal` in der installierten HD-`coastal.fsh` → `work/track/out/coastal.fsh` (unkomprimiert, 91,7 MB). Ball als Schild-Kopie 0448 +3 m mit Material 218 → `work/track/out/coastal_ball2.crp`. **👤-Test (2026-10-21) ✅:** Ball mit eigener Streifen-Textur im Spiel sichtbar.

**Offen:** `ef`-Blätter (Effekte, evtl. mit Weltposition) werden unverändert kopiert; Bedeutung der `Base`-Flags.

**Antwort auf die offenen Fragen des Auftrags, soweit bekannt:**
- *Format und Länge der Sichtbarkeitslisten:* Es gibt keine Liste. Es gibt Verdeckungswände (Chunk `Vis ` in `<strecke>.crp`, §2a) und Chunks.
- *Kamera oder Fahrzeug als Basis:* die Kamera. `OcclusionReject` bekommt die Kameraposition (`[ESP+0x14]`), und der Rückspiegel läuft als eigener Durchgang (B20).
- *Ferne Hänge Teil der Blöcke oder der Skybox:* Das Panorama ist Himmel (`Track/Sky`, 9 Teilflächen je Schicht, `BUGS.md` §4.1). Ob fernes Gelände darin steckt, ist nicht geprüft.
- *LOD-Stufen:* ja, 1–3 je Abschnitt (Reichweiten z. B. 130/600/65535; `BUGS.md` §18).
- *Tiefenpuffer:* offen. Heute läuft Far-Clip bis 20 000 ohne bekannte Z-Fehler. Prüfen, wenn die Sichtweite steigt.

## 2d. Fahrbahn der Physik (`SimD`) und LOD-Stufen der Straße (B22, 2026-10-22)

Quelle: `FUN_00480080` (sucht `SimD`/`SimT` im `Trak`, sonst „Could not find sim data“), `FUN_00480140` (kopiert je Wegpunkt 0x50 Byte); Messwerkzeug `work/track/simd_vs_mesh.py`, Ergebnis `work/track/out/simd_vs_mesh.txt`.

**`SimD`:** ein Wegpunkt je 80 Byte, Abstand ≈ 6 m (coastal 1465, alps 2105, castle 3443).
| Versatz | Inhalt |
|---|---|
| `+0x00` | 0 |
| `+0x04` | Position (3 float), liegt genau auf der Fahrbahnmitte des Mesh |
| `+0x10` | Normale, immer ≈ (0, 1, 0) |
| `+0x1C` | Fahrtrichtung |
| `+0x28` | Seitenvektor (y = 0) |
| `+0x34` | Wort (`0x3C000`, `0x38000` …), `+0x38`/`+0x3C` Wörter (2/2, 2/1 …) |
| `+0x40` | 4 Breiten (z. B. 12/12/6/6, an Engstellen kleiner) |

`SimT` (coastal 4 Wörter) teilt die Wegpunkte in Abschnitte. `.edg` ist dagegen eine Liste von Linienstücken (28 Byte: Punkt, Punkt, Wort), vermutlich die Randbegrenzung. Lader `FUN_00487e80` (Anzahl = (Größe − 4) / 28, +0 Art im unteren Byte, +4/+0x10 Punkte), Aufrufer `FUN_00488090` sortiert in ein Gitter über 8192 m. Werkzeug `edg_obj.py` (Blender: Objekte `NEUEDG_<art>_…`); Art = Wandmaterial nach Sichtung in Blender (👤): 0 Stein/Fels, 16 Metall-Leitplanke, 32 Holz, 96 selten an Häusern; Linien folgen dem Wandfuß (Tunnel: auch oben an der Wand). Richtung: bei 96 % der Stücke liegt der nächste SimD-Wegpunkt links von A→B (x/z); 143 Stücke von 0,45 m hängen 6,5–7,6 m über der Fahrbahn (Lampen im Tunnel). 👤-Test: neue Linien (auch richtig gedreht) ließen das Auto durch, die Kamera prallte ab. Auto: `FUN_00495020` prüft je Ecke der Karosserie die Strecke Mitte→Ecke gegen Linien (Art 2 im Gitter `DAT_00628960+0x24`, `FUN_004740d0`) mit reinem x/z-Schnitt `FUN_00489cc0` (keine Höhe, keine Richtung); nur wenn unter der Ecke eine Bodenfläche gefunden wurde (`+0x28`). Bei mehreren Treffern gewinnt die Linie, deren Höhe der Strecke am nächsten ist. Ohne .edg erzeugt das Spiel die Linien aus dem Mesh (`FUN_004750b0`, Modus 0). Material-Flags `mt`+0: untere 4 Bit Untergrund (1 Straße …, 3 Gras/Erde), 0x10/0x20/0x60 = Wandart, 0xC000 befahrbar.

**Messung (alle Strecken):** Je Wegpunkt und Mitte dazwischen wird quer über die Breite (±100 %, 9 Stellen) die Höhe der Fahrbahn-Objekte `RD…` in LOD 0 verglichen mit (a) der SimD-Ebene durch den Wegpunkt und (b) LOD 1/2 derselben Objekte. Positiv = LOD 0 liegt höher.

| Strecke | LOD 0 − SimD 95 % / max | LOD 0 − LOD 1 95 % / max | LOD 0 − LOD 2 95 % / max |
|---|---|---|---|
| alps | +0,44 / +1,79 m | +0,04 / +0,29 | +0,16 / +0,70 |
| autobahn | +1,06 / +2,91 | +0,02 / +0,04 (1 Ausreißer 27 m, Brücke) | +0,06 / +0,13 |
| canyon | +0,83 / +2,99 | +0,03 / +0,21 | +0,11 / +0,70 |
| castle | +0,60 / +2,48 | +0,03 / +0,24 | +0,13 / +1,05 |
| coastal | +0,62 / +2,43 | +0,03 / +0,27 | +0,11 / +0,93 |
| farmland | +0,03 / +2,48 | +0,08 / +0,53 | +0,11 / +0,64 |
| foothills | +0,23 / +1,48 | +0,03 / +0,15 | +0,12 / +0,48 |
| forest | +0,85 / +2,98 | +0,03 / +0,27 | +0,14 / +0,64 |
| industrial | +0,01 / +0,39 | +0,01 / +0,21 | +0,05 / +0,60 |
| monaco1–5 | +0,01…+0,39 / bis +3,0 | ≤ +0,03 / ≤ +0,15 | +0,06…+0,11 / ≤ +0,66 |

- In der Fahrbahnmitte stimmt SimD auf wenige cm (coastal 95 %: +1 cm). Quer wächst die Abweichung linear mit dem Abstand: **SimD kennt keine Querneigung**, das Mesh ist in Kurven bis etwa 15–17° geneigt (coastal Wegpunkt 553: 0,27 m je m).
- Wenn die Räder nur die SimD-Ebene sähen, schwebten oder sänken Autos in geneigten Kurven um über 1 m. Das ist im Spiel nicht zu sehen; die Radhöhe kommt also wohl aus dem Mesh oder einer anderen Quelle (offen, Ghidra: Leser von Wegpunkt +0x10 suchen).
- LOD 1 liegt fast überall unter 5 cm an LOD 0, LOD 2 bei 5–17 % der Messpunkte über 5 cm, an einzelnen Stellen bis 1 m.

**Folgerung für B22:** SimD an LOD 0 anzupassen hilft nur in Längsrichtung; Querneigung und Wölbung kann eine Ebene je 6 m nicht abbilden, und KI/Spurwahl hängen an denselben Daten. Wenn die Spuren aus einer anderen Fläche (SimD-Ebene oder gröbere LOD) ihre Höhe bekommen, liegt LOD 0 bis zu Dezimeter darüber.

## 3. Nebel heute (Bestand, B1 des Auftrags)

Quelle: `nfs5_enhanced-1.2.0-mod-sourcecode/drivers/dx11/src/driver.cpp` (SetState), `shaders.h` (Pixel-Shader).

- dx11z übernimmt die Thrash-Zustände: `0x0E` Dichte, `0x0F` Farbe (RGB), `0x14`/`0x69` **Nebeltabelle** (256 Byte, gespeichert invertiert), `0x15` Modus (0 aus, 1 an, 2 linear, 4 exp, 8 exp²).
- Bei aktiver Tabelle schreibt `PushVertex` den Nebelwert je Vertex in das Specular-Alpha. Index ist `round(z × 255)` mit der **Bildschirm-Tiefe** z, nicht der Entfernung. Der Shader mischt dann `lerp(FogColor, Farbe, fog)` (Modi exp/exp²/linear über die Tiefe, Tabelle über `specular.a`).
- Spiel: `FUN_0044e890` fragt nur ab, ob Modus 1 möglich ist, und stellt den alten Wert zurück. `FUN_004690f0` (Aufräumen beim Verlassen des Rennens, ruft u. a. `FUN_0047f190`) schaltet den Nebel mit `0x15, 0` aus. Feste Aufrufe mit Tabelle, Farbe oder Dichte gibt es im Export nicht. Das Spiel setzt sie also über eine Zustands-Variable oder eine Liste → **Werte zur Laufzeit loggen** (13.1).
- **Wichtig für größere Sichtweite:** Hängt die Tabelle an der Bildschirm-Tiefe z und ändert der Mod den Far-Clip, verschiebt sich der Nebel. Ob das Spiel die Tabelle aus dem Far-Clip neu berechnet, ist offen (13.1).

## 4. Angepasster Weg

### Teil A – korrekte Auswahl statt neuer PVS-Dateien

Die Idee des Auftrags (Sichtbarkeit offline aus echter 3D-Geometrie berechnen, konservativ vereinigen) bleibt. Das Ergebnis ersetzt aber nicht eine Liste in der Streckendatei, sondern **die Entscheidung des 2D-Wandtests**. LFSP hängt schon an genau dieser Stelle (`OcclusionReject`, `0x47F9AE`).

- **A-a (Laufzeit, ohne Offline-Werkzeug):** Wandtest mit Höhe. Eine Wand verdeckt nur, wenn sie auf der Sichtlinie höher ist als die Linie Kamera → Objekt (oberer Kugelrand). Dazu braucht man die Höhe der Wand. Die Wand hat im Format bisher nur 2D-Daten → zuerst klären, ob Byte 0 „Art“ oder die Quelldatei eine Höhe liefert (13.2).
- **A-b (Offline, wie im Auftrag):** Je Chunk oder Abschnitt der Ideallinie eine Tabelle „Objekt-Ids sichtbar trotz Wand“ bzw. „verdeckt ohne Wand“ aus einem ID-Buffer-Rendering der ganzen Strecke. LFSP liest die Tabelle beim Laden und fragt sie in `OcclusionReject` ab. Voraussetzung: Strecken-Geometrie (`.crp`) und Objekt-Ids lesbar → Parser, Round-Trip nur für die gelesenen Teile nötig, weil nichts zurückgeschrieben wird.
- Erst wenn A-a/A-b stehen, die Sichtweite (DDS) weiter anheben. Bei DDS > 3 dann B2 (feste Tabellen) angehen.

### Teil B – Dunst in dx11z

- Entfernung je Pixel aus `1/rhw` (interpoliert, wie `ProbeBatch`), am Rand korrigiert über die Bildposition (echter Abstand statt Tiefe).
- Modell wie im Auftrag: `f = max(1 − exp(−(d·ρ)²), smoothstep(0,8·D, D, d))`. `D` = Sichtweite `0x6197CC` (LFSP liefert sie an dx11z wie die Licht-Daten). Farbe und Dichte kommen aus den Nebelwerten des Spiels (13.1), ohne Nebel des Spiels aus der Himmelsfarbe am Horizont.
- Himmel ausnehmen: Kandidat sind die Draws ohne Tiefen-Schreiben (`zfunc 8`, B21-Log). Das muss noch bestätigt werden.
- Schalter in `lfsp.ini [Haze]`: `Enable` (0 = Originalbild), `Density`, `EdgeStart` (0.8), `HeightFalloff`.

## 5. Offene Punkte

- Wie setzt das Spiel Nebeltabelle, Farbe und Dichte, und hängen sie von Strecke, Wetter oder Nacht ab? (13.1)
- Strecken-Geometrie: offene Teile siehe §2b.
- Welche Draws sind der Himmel? (13.1)

**Kollision des Autos (Code, 13.3g):** Das Auto prüft Wände nicht an den `.edg`-Linien. Die Linien (Gitter-Art 2) nutzen nur die Kamera (`FUN_004649e0`) und das Auseinanderschieben zweier Autos (`FUN_004980d0` → `FUN_00495020`). Das Auto prallt an Hindernissen der Gitter-Art 0 ab: `FUN_00498de0` sucht im Umkreis von 5 m (`FUN_00473870`, Gitter `DAT_00628960+0x24`) nach `WTreeElem_CylinderObject` (Vtable `0x5b490c`, `FUN_00498aa0`) und `WTreeElem_BoxObject` (`0x5b4938`, `FUN_00498690` → `FUN_00496610`). Erzeugt werden sie in `FUN_004750b0`: `Base` +0x44 ist ein 4-Zeichen-Kollisionscode (rückwärts gespeichert), den das Spiel in `DAT_00628ba0` nachschlägt (`FUN_0047ed90`). Eintrag +0x10 = 1 Zylinder, 2 Box, +0x14 = Material. Ohne Code entstehen aus dem Mesh nur Boden-Flächen. coastal: `PILR` (Pfeiler) an den platzierten Kulissen-Objekten 20/21 CAUTIONSIGN und 655 TRUNKBIG; `SIGN`, `BBOX`, `FIS1` … an den Physik-Objekten. Woher die Tabelle die Codes und Maße hat, ist offen.
