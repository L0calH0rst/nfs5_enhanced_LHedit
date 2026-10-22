# MOD.md – `nfs5_enhanced` und eigene dx11z-Patches

> Themen-Dokument: Analyse des Mods `nfs5_enhanced` v1.1.0 und Übersicht über unsere Änderungen an `dx11z.dll`. Status und Aufgaben stehen in `PLAN.md`.
> **Quelle:** `nfs5_enhanced-1.1.0-mod-sourcecode/` (per `.gitignore` nicht versioniert), Release `nfs5_enhanced_v1.1.0.zip`. Analysiert 2026-10-13.

## 1. Was der Mod ist

ASI-Plugin, keine Reimplementation. Er hakt `Porsche.exe`/`gimme.dll` **im Speicher** und ändert keine Spieldatei auf der Platte. Technik: SafetyHook + Zydis, Konfiguration über `nfs5_enhanced.ini`.

Quellbaum:
- `src/core/`, `src/subsystems/`
- `src/game/addresses.h` + `structs.h`: fertige Hook-Karte, sehr wertvoll fürs Reverse Engineering
- `drivers/dx11/`: der DX11-Renderer `dx11z.dll` (neu in 1.1.0)

**Versions-Hinweis:** Der Log-String kommt aus einer hartkodierten Konstante in `src/main.cpp` (`"1.0.0"`). Ein 1.1.0-Build loggt also „NFS5 Enhanced 1.0.0 starting“. `version.rc` sagt 0.1.0, `CHANGELOG.md` 1.1.0. Installiert ist der 1.1.0-Build. Das Versions-Durcheinander ist kosmetisch und wird nicht weiter verfolgt.

## 2. Voraussetzungen

- USA-Release und offizieller **v3.5-Build** von `Porsche.exe`. Der Mod prüft **PE-Header-Felder** (Image Base, TimeDateStamp, SizeOfImage), **keine MD5**. Passt der Header nicht, hakt der Mod bewusst nicht.
- Ultimate ASI Loader als `dinput.dll`, `dx11z.dll` im Ordner `Drivers/`.
- DDrawCompat wird nicht mehr benötigt (nur für den alten DX7-Pfad).
- `NFS5 Combined Patch`, `NoCDcrack.exe` und SilentPatchNFS90s sind überflüssig und würden kollidieren.

## 3. Was der Mod abdeckt

| Projektziel | Mod-Funktion |
|---|---|
| Win11-Start | Fixes für Win10/11-Startabsturz, „DirectX 0“-Fallback, CPU-Affinität, Speicherlecks |
| No-CD / DRM | CD-Prüfung im Speicher übersprungen |
| HD-Auflösung | alle Modi ab 640×480, Widescreen „Hor+“, scharfe Menüs, HUD-/Text-Skalierung |
| Textur-Qualität | `TextureDetail = 3`: Das Original lädt auf CPUs > 2,1 GHz alle Texturen in 16 Bit und halber Größe; der Mod erzwingt die höchste Stufe |
| Kantenglättung / Filterung | MSAA bis 8×, Anisotropie bis 16× |
| Sonstiges | Interpolation, Frame-Limiter, Sichtweite bis ×10, QPC-Spieluhr, FullCarDetail |

## 4. DX11-Renderer `dx11z.dll`

| Funktion | Beschreibung |
|---|---|
| Pipeline | Fixed-Function wird in Shadern nachgebildet (Texture Stages, Specular, Alpha-Test, Nebel, Depth Bias) |
| MSAA | bis 8× (Default 4×) |
| Anisotropie | bis 16× |
| Auflösung | alle Monitor-Modi, `ResolutionScale` 0–4×, Fenster/Borderless/Vollbild |
| VSync / Tearing | ein/aus, Tearing wo das Display es erlaubt |
| Per-Monitor-DPI | Windows-Skalierung streckt das Bild nicht mehr |
| Alt+Tab | minimiert bei Fokusverlust, stellt wieder her |
| Videos | Intro-/Menüvideos in 16 und 32 Bit |
| Helligkeit | beim Present angewendet (keine Gamma-Rampe) |

**Wichtige Code-Stellen:**
- Texturen: `Driver::TextureAlloc` / `Driver::TextureUpdate` (`src/driver.cpp`), Konvertierung in `src/formats.cpp`.
- Gemeldete Texturformate: `Driver::About()`, nur ARGB1555, RGB565, ARGB8888, ARGB4444.
- Pixelshader: `MainPS` / `AlphaPasses` in `src/shaders.h`. Das Spiel kompiliert ihn zur Laufzeit (vs_4_0/ps_4_0).
- Pipeline-State: `Driver::ApplyPipeline`, Draw: `Driver::Flush`.

## 5. Hooks auf die Engine

- Der Mod skaliert die Sichtweite an genau **zwei** Stellen: `DrawDistanceHook` `0x44F1A0` und den Far-Clip-Operanden `0x4677FC` (Reset `0x45D501`). Details: `BUGS.md` §2.
- In `addresses.h` gibt es **keinen** Code-Hook in `gimme.dll`. Der Mod hookt ausschließlich `porsche.exe`; `gimme.dll` wird nur in `silent_compat.cpp` berührt (Schutzprüfungen, z. B. `FixPictureConversion` für den Shape-Parser RVA `0x1054A0`).

## 7. Eigene Patches an der ASI (`nfs5_enhanced.asi`)

| Datei | Inhalt |
|---|---|
| `work/patches/asi/base/` | Original-Quellen der geänderten Dateien (`addresses.h`, `widescreen.cpp/.h`) aus Mod 1.1.0 |
| `work/patches/asi/asi-nfsp.patch` | Hor+-Culling-Fixes: Hooks `SkyCullPlanes` (`0x45AAF0`, Himmel, B5) und `WorldCullVolume` (`0x47F550`, Map-Chunks, B13) setzen für die Dauer des Aufrufs den Hor+-Halbwinkel ein (`BUGS.md` §4.2, §4.3) |
| `work/patches/asi/build_asi.cmd` | Build mit CMake (aus BuildTools 18) + NMake, Ausgabe `build-asi/nfs5_enhanced.asi` |

| Datei in `Game CD/` | Inhalt | MD5 |
|---|---|---|
| `nfs5_enhanced.asi` | eigener Build mit B5- und B13-Fix | `fffd41ab49df85f466e6573d862cb3ec` |
| `nfs5_enhanced.asi.b5` | eigener Build nur mit B5-Fix | `306ef8bb3401de68df4fc6cb2350ef1c` |
| `nfs5_enhanced.asi.bak` | Original aus dem Release | `55f778dc2b15fd43d5887526b10cbbae` |

## 6. Eigene Patches an `dx11z.dll`

Der Mod-Quellcode ist nicht versioniert. Unsere Änderungen liegen als **ein Gesamtpatch** in `work/patches/dx11z/`. So lassen sie sich auf ein neues Mod-Release übertragen. Die Einzelschritte (3T.1 … 3C.19) stehen in der Git-Historie (bis Commit `d70add6`).

| Datei | Inhalt |
|---|---|
| `pre-3T1/` | Basis: Mod-Quellen 1.1.0 mit dem temporären `B4TEST`-Log (der Patch entfernt es); `thrash_exports.cpp` seit 9.4 ergänzt (unveränderter Release-Stand) |
| `dx11z-nfsp.patch` | Gesamtstand (aufgeräumt 2026-10-16): Messlog, B4-Fix, B10-Korrektur, Rückseiten-Durchgang (B8/B11), Konfig-Verbesserungen 3C.19. Geprüft: Basis + Patch = aktueller Quellstand |
| `build_dx11z.cmd` | Build mit MSVC BuildTools 18, x86 |
| `base-1.2.0/` | Basis ab 2026-10-19: unveränderte Quellen von nfs5_enhanced 1.2.0. `dx11z-nfsp.patch` gilt seitdem gegen diese Basis (Build in `nfs5_enhanced-1.2.0-mod-sourcecode`) |

**Umstieg auf 1.2.0 (2026-10-19):** 3-Wege-Merge (Basis `pre-3T1`, unser 1.1.0-Stand, 1.2.0). `ApplyPipeline` und `Flush` sind **unsere** Fassung (bindet jeden Draw alles neu, Discard je Draw), weil Kanten-, Rückseiten-, Spiegel- und Licht-Pässe am Zustands-Cache und Ringpuffer von 1.2.0 vorbei binden. Damit fehlt nur die CPU-Ersparnis dieses Teils; alles andere aus 1.2.0 (Textur-Handles per Map, Sampler-Cache, Sonnen-Probe, Auflösungsliste, Sichtweite, Controller/SDL3) ist drin. `DrawConstants` steht jetzt in `driver.h`. ASI: `asi-nfsp.patch` gegen `work/patches/asi/base-1.2.0`, Sky-/World-Cull-Hooks neben den neuen Auflösungs-Hooks. Installiert: `nfs5_enhanced.asi` (`c49284851d58719fc9a6d164f94b9bd9`), `SDL3.dll`, `Drivers/dx11z.dll` (`6789cd4ab1fc193c3353872479cda68d`); 1.1-Stand in `Game CD/_backup_1.1/`. INI: `MinDrawDistance = 0` und `[Controller]` ergänzt, eigene Werte unverändert.

Anwenden auf eine frische Basis: `patch -p1 < dx11z-nfsp.patch` im Ordner `drivers/dx11` (Dateien unter `src/`).

**Entfernt beim Aufräumen (2026-10-16):**
- `ClampUnitUv` (alle Stufen): B7 wird per Asset-Fix gelöst (`BUGS.md` §9.4).
- `AlphaEdgeFix = 1` (Schwelle): zerstörte Reflexionen und Ufer-Übergänge (§8.8).
- `ForceTwoSided = 1` (Culling aus): Z-Fighting bei doppelseitigen Schildern (B11).
- `Measure = 3`: Grün-Färbung, gehörte zu `ClampUnitUv`.

**INI-Schalter der NFSP-Erweiterung** (`[Dx11]` in `Game CD/nfs5_enhanced.ini`; sie müssen in diesem Abschnitt stehen, eine eigene Überschrift wie `[Test]` davor macht sie wirkungslos):

| Schlüssel | Werte | Empfehlung |
|---|---|---|
| `AlphaEdgeFix` | 0 = aus (Original), ≠ 0 = Kantentexel alpha-getesteter Draws im zweiten Pass ohne Z-Write (B4) | **1** |
| `AlphaEdgeRef` | 1–255, Trennwert zwischen den Pässen | **128** |
| `ForceTwoSided` | 0 = aus (Original), ≠ 0 = vom Spiel verworfene Rückseiten deckender Texturen leicht nach hinten versetzt zusätzlich zeichnen (B8; ohne B11/B12) | **1** |
| `Measure` | 0 = aus, 1 = Messlog (Draw-States, Formate, Speicher, Koordinaten; ab 9.1 auch Licht-Kandidaten `M3T pass`, Texturabzüge `m3t_<hash>_<w>x<h>[_alpha].bmp` neben dem Log (max. 64), mittlere Vertexfarbe; ab 9.2b Bericht alle 10 s mit Sekunden seit Start; ab 9.2c Momentaufnahme des nächsten Bildes je Textur, `M3T snapshot` (bis 9.4e **F11**, ab 9.4e2 **Rollen**)), 2 = + Magenta für Blend+Z-Write-Draws | **0** (nur zur Diagnose) |
| Licht-Schalter (`Headlights`, `HeadlightsDay…`, `HeadlightsMap`, `HeadlightsOpponents…`) | ab 9.11b in `Game CD/lfsp.ini` Abschnitt `[Lights]` der Mod LFSP (Beschreibung dort und in `LICHT.md` §8.2a). dx11z liest sie dort; fehlt ein Schlüssel, gilt noch `[Dx11]` hier | – |
| `Antialiasing` (Original-Schalter, ab 3C.19) | 0 oder 1 = aus, 2, 4, 8, 16; nicht unterstützte Werte fallen auf die nächste niedrigere Stufe | nach Geschmack |
| `VSync`, `MinimizeOnFocusLoss` (ab 3C.19) | `1/0`, `true/false`, `on/off`, `yes/no` | – |

Ältere Werte wie `AlphaEdgeFix = 2` oder `ForceTwoSided = 2` funktionieren weiter (alles ≠ 0 = an).

**Installierte DLLs in `Game CD/Drivers/`:**

| Datei | Inhalt | MD5 |
|---|---|---|
| `dx11z.dll` (installiert, 2026-10-18) | Basis + `dx11z-nfsp.patch`: wie `b15-measure` + Test 11.4 `[Lights] HeadlightsObjects` (Licht auf Autos/Schildern/Objekten, Zähler im Messlog), Vorgabe `TailGlowStrength` 0 (`LICHT.md` §11) | `2c54ad3320ab68aed86dd3e7ace403d7` |
| `_backup/dx11z.dll.b15-measure.dll` | Stand B21 (`TestDraw`/`TestNoZBias`) + Messlauf M-B15 (Schnappschuss mit Cull, Umlaufrichtung, Rechteck, BMP-Abzug; `BUGS.md` §12) | `a04c67b9d9b1b05575ef5e20783136db` |
| `_backup/dx11z.dll.b21-testdraw.dll` | Stand B21 mit `TestDraw` | `991bb385cb533483abf34576c000af30` |
| `_backup/dx11z.dll.9-2b.dll` | Stand 9.2b | `39d4eb41cd8f90d3e64e539e73a6d2a3` |
| `_backup/dx11z.dll.9-2c.dll` | Stand 9.2c | `f5f08025064e3d5f5e3e6ec2bb6c5cf3` |
| (überschrieben) | Stand 11.5c (= Basis + `dx11z-nfsp.patch`): Rücklicht-Schein, Rücklicht 0,06 / Bremse 0,41 (`LICHT.md` §9.5) | `85a5658220eb1006bec256e4835b7537` |
| (überschrieben) | Stand 11.5b: Rücklicht-Schein mit 0,12 / 0,35 | `4e17c71cca94ac3cde8cc127dd7a26ab` |
| `_backup/dx11z.dll.11-5a.dll` | Stand 11.5a: Messlauf Leuchten-Tabelle (`LICHT.md` §9.2) | `6c94f8cb02581ce48d5610a16777529f` |
| `_backup/dx11z.dll.9-10c.dll` | Stand 9.10c (= Basis + `dx11z-nfsp.patch`): Fernlicht auch ohne Abblendlicht, Matrix-Prüfung wiederholt | `6dae884d92a59f3e4ed6550546550c94` |
| `_backup/dx11z.dll.9-10b.dll` | Stand 9.10b: Fernlicht nur nach oben, Gegner-Matrix in vier Varianten geprüft (9.11d) | `a9fe7ce7453dbe121f28ccf059f00594` |
| (nicht gesichert) | Stand 9.10a: Fernlicht über `lfsp.asi`, Gegner-Licht auch bei Tag (9.11c); beim Austausch überschrieben | `125b620320370347bce4d4cdaf6739ed` |
| `_backup/dx11z.dll.9-11b.dll` | Stand 9.11b: Scheinwerfer der Gegner (`LICHT.md` §8.4a), Licht-Schalter aus `lfsp.ini [Lights]` | `c46459813f7b2085733093a76ab1b51e` |
| `_backup/dx11z.dll.9-9b.dll` | Stand 9.9b (= Basis + `dx11z-nfsp.patch`): Scheinwerfer mischt sich mit farbiger Umgebung (`LICHT.md` §8.3a) | `187c659957c76694b3f6301213eb9cb1` |
| `_backup/dx11z.dll.9-9a.dll` | Stand 9.9a: Lichtfarbe nach Baujahr über `lfsp.asi` | `9fb30b9f082ecf37417fbaaaeccaa7dc` |
| `_backup/dx11z.dll.9-7a.dll` | Stand 9.7a: `[Dx11] HeadlightsMap` | `671427b695633804921bd902a73d9964` |
| `_backup/dx11z.dll.9-6h.dll` | Stand 9.6h: Tag-Licht (committet) | `5b9670987059eb0575076cec2df447b0` |
| `_backup/dx11z.dll.9-6d.dll` | Stand 9.6d | `0a5195dce2ffcc17e0cc6d48f96904c0` |
| `_backup/dx11z.dll.9-6c.dll` | Stand 9.6c | `249338d2caf206d3ad894c2ef8b8f3d6` |
| `_backup/dx11z.dll.9-6b.dll` | Stand 9.6b | `7f737b355d5d7184ac7e88d74fb798ee` |
| `_backup/dx11z.dll.9-6a.dll` | Stand 9.6 (falsche Matrix) | `5b3ba0f8a7eb46f931df586a0a6ea81f` |
| `_backup/dx11z.dll.9-5.dll` | Stand 9.5 | `b0e5e86dbe45011588bf29fa10363f3c` |
| `_backup/dx11z.dll.9-4e2.dll` | Stand 9.4e2 | `49d61c4eb944f3876e388e1435f8de37` |
| `_backup/dx11z.dll.9-4e.dll` | Stand 9.4e (F11) | `39f49b8a14a45a0a028ac50c2d97cd86` |
| `_backup/dx11z.dll.9-4d.dll` | Stand 9.4d | `5512ea215cdbbd40adbc858dd1962bf6` |
| `_backup/dx11z.dll.9-4c.dll` | Stand 9.4c | `016941916fba332f9859a675a1925c50` |
| `_backup/dx11z.dll.9-4b.dll` | Stand 9.4b | `a0a89ed7e2c035ba85c36bdefcf8f12b` |
| `_backup/dx11z.dll.9-4.dll` | Stand 9.4 | `32c14e56394a99c9fd27f476d5b4c30c` |
| `_backup/dx11z.dll.3c20` | Sicherung Stand 3C.20 | `7e1a536614358cf0089bf8953d7b6331` |
| `dx11z.dll.bak` | Original aus dem Release | `5863c16195ace78838675e86a30c48b0` |
| `_backup/dx11z.dll.*` | Zwischenstände 3T.1 … 3C.19 und `.cleanup` (`92c159c9…`) | – |
