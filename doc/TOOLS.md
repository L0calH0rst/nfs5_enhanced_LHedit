# TOOLS.md – Werkzeuge und Verzeichnisse

> Themen-Dokument: welche Werkzeuge es gibt, wo sie liegen und wie man sie aufruft. Status und Aufgaben stehen in `PLAN.md`.

## 1. Werkzeuge

| Zweck | Tool | Pfad | Status |
|---|---|---|---|
| FSH/QFS ↔ BMP (NFS5: Palette `0x29`, Typ `0x6D`, komprimiert/Multiscale, Alpha) | `fshtool` v1.22 (Denis Auroux) | `NFS5 Editing Kit/FSHTool/fshtool.exe`; Quelle `OpenNFS-0.3-sourcecode/tools/fshtool.c` | ✅ Entpacken und Repack funktional geprüft (`FSH-REPACK.md`) |
| Fahrzeugtexturen (CRP → FSH) | `NFSPUTexture.exe` | `NFS5 Editing Kit/NFSPUTEX/NFSPUTexture.exe` | vorhanden, ungetestet |
| Hex-/Format-Analyse | Python (`struct`, `hashlib`), `hexdump` | System | verfügbar |
| Dekompilieren einzelner Funktionen | Ghidra 12.1.4 headless | `GhidraNFSP/decompile.cmd` + `DecompileList.java` (`decompile.cmd Porsche.exe <ausgabe> <adresse\|refs:adresse> …`); Ausgabe in `work/re/` bleibt lokal | ✅ |
| Dekompilieren von Funktionen, die der Export nicht kennt oder zu kurz schneidet | Ghidra 12.1.4 headless | `GhidraNFSP/DecompileRebuild.java`: legt jede Funktion am angegebenen Einsprung neu an (`-readOnly`, nichts wird gespeichert) und dekompiliert sie; Aufruf wie `decompile.cmd`, nur `-postScript DecompileRebuild.java <ausgabe> <einsprung> …` | ✅ |
| ASI bauen | CMake (BuildTools 18) + NMake | `work/patches/asi/build_asi.cmd` | ✅ |
| Ganze Disassembly / ganze Dekompilierung für Offline-Suchen | Ghidra headless | `GhidraNFSP/script.cmd Porsche.exe DumpListing.java <ausgabe.tsv>` (Adresse, Funktion, Länge, Befehl); `… DecompileAll.java <ausgabe.c>` (≈ 15 min); Ausgaben in `work/re/` bleiben lokal | ✅ |
| Alle Code-Operanden in einem Adressbereich | Ghidra headless | `GhidraNFSP/script.cmd Porsche.exe DumpGlobalRefs.java <ausgabe.csv> <lo-hex> <hi-hex>` | ✅ |
| Verlegungstabelle für mehr als 9 Autos | Python | `nfs5_enhanced_LHedit/lfsp/gen_carlimit_table.py <globalrefs.csv> <listing.tsv> <Porsche.exe> carlimit_table.h` (globalrefs über `5d0000 6e3000`); bricht bei mehrdeutigen oder unerfassten Stellen ab (`VERKEHR.md` §6.6) | ✅ |
| Leser eines Struktur-Offsets finden | Ghidra headless | `GhidraNFSP/script.cmd Porsche.exe FindDisplacement.java <ausgabe> <offset-hex> [<global-hex>]` | ✅ |
| Disassembling | Ghidra-Export | `GhidraNFSP/export/{gimme.dll,porsche.exe}/function_map.csv` | ✅ validiert (`GHIDRA.md`) |
| Win11/DX11 | `nfs5_enhanced` (ASI) + `dx11z.dll` | `Game CD/` | ✅ installiert, läuft (`MOD.md`) |
| `dx11z.dll` bauen | MSVC BuildTools 18 (x86) | `work/patches/dx11z/build_dx11z.cmd` | ✅ |
| HLSL offline prüfen | `fxc` | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe` (die x64-Version; die arm64-Version läuft hier nicht) | ✅ |
| Kantenzeilen von Alpha-Texturen prüfen (B7) | `edge_scan.py` | `tools/hd/edge_scan.py` (`python edge_scan.py <extract_dir>`) | ✅ |
| Alpha-Lücken an Atlas-Zellgrenzen setzen (B7) | `atlas_gutter.py` | `tools/hd/atlas_gutter.py` (`python atlas_gutter.py <dir> <objekt> <x0>:<x1>:<zellen> …`) | ✅ |
| QFS/RefPack entpacken und packen (z. B. `.crp`) | `qfs.py` | `work/track/qfs.py` (`python -I qfs.py <ein> <aus>`; `compress()` als Funktion) | ✅ alle 15 Strecken-`.crp`, Packen per Rück-Entpacken geprüft |
| Trak-Container als Baum auflisten | `trak.py` | `work/track/trak.py` (`python -I trak.py <strecke.crp> [tiefe]`) | ✅ |
| Objekt als neues Objekt kopieren (Test) | `trak_clone.py` | `work/track/trak_clone.py <crp> <neu.crp> <nummer>:<dx>,<dy>,<dz>[:<modell.obj>[:<material>]] …`, Suche `--list TEXT` | ✅ Kopien im Spiel (Schilder); Modell offline ✅ |
| Fahrbahn: SimD und LOD 1/2 gegen LOD 0 messen | `simd_vs_mesh.py` | `work/track/simd_vs_mesh.py <crp> […]` | ✅ alle Strecken (`work/track/out/simd_vs_mesh.txt`) |
| Bild als neue Textur in eine `.fsh` (ARGB8888 + Mips) | `fsh_add.py` | `work/track/fsh_add.py <fsh> <neu.fsh> <name4> <bild.png\|bmp>`, Liste `--list` | offline ✅ |
| Strecken-Geometrie ↔ Blender (OBJ) | `trak_obj.py` | `work/track/trak_obj.py export <crp> <obj> [--lod N] [--only TEXT]` / `import <crp> <obj> <neu.crp> [--uv]` | ✅ Round-Trip byte-genau; 👤 bearbeitete coastal lädt im Spiel |
| Messlog-Hash einer Textur zuordnen | `texture_hash.py` | `tools/hd/texture_hash.py` (`python texture_hash.py <extract_dir> [hash …]`) | ✅ |
| Multiscale-Objekt verdoppeln (Test 2.0) | `upscale_mbp.py` | `tools/hd/upscale_mbp.py` (`python upscale_mbp.py <extract_dir> <objekt> [--mark]`) | ✅ |
| HD-Texturen: Ablauf prep → chaiNNer → finish → pack → install | `hd_pipeline.py` (+ `hd_prep.py`, `hd_finish.py`, `hd_config.ini`) | `tools/hd/` (`python hd_pipeline.py status`), Anleitung `HD-TEXTUREN.md` | ✅ getestet (Lanczos statt Modell) |
| KI-Upscaling | chaiNNer | `G:\NFSP\work\hd\Texture_Upscaling_Tools\chaiNNer.exe`, Modelle in `work\hd\models` | 👤 eingerichtet, Modelle fehlen noch |
| W2 (OpenNFS, nur optional) | Vulkan-Renderer, Loader für NFS2–4, **kein NFS5-Loader** | `OpenNFS-0.3-sourcecode/src/` | nicht in Arbeit |

**Weitere Bestände im Workspace:**
- `OpenNFS.Pre-Alpha.v_03/resources/`: bereits extrahierte Assets (BMPs `lanes/`, `sfx/`) als Referenz.
- `NFS5 Combined Patch/`, `NFS5_broken_texture_fix/`: Community-Patches. Nicht mit dem Mod kombinieren (`MOD.md` §2).

## 2. Aufrufe

```
# Strecke nach Blender und zurück (SICHTWEITE.md §2b)
python -I work/track/trak_obj.py export "Game CD/GameData/Track/alps.crp" work/track/out/alps.obj
#   Blender: Datei → Import → Wavefront (.obj), "Split by Object" an, "Validate Meshes" aus.
#   Vertices verschieben, Flächen unterteilen oder Teile im selben Objekt duplizieren (Shift+D);
#   keine neuen Objekte anlegen. Export → Wavefront (.obj) mit denselben Achsen (Forward −Z, Up Y),
#   "Triangulated Mesh" an, "Colors" an (Vertexfarben = Vorbeleuchtung), "Materials" an, gleicher Dateiname.
python -I work/track/trak_obj.py import "Game CD/GameData/Track/alps.crp" work/track/out/alps.obj work/track/out/alps.crp --uv
#   die .map.json vom Export muss neben der OBJ liegen; Original vorher sichern, dann die neue .crp einsetzen
#   Neues Objekt: in Blender ein Objekt anlegen/importieren, platzieren und umbenennen in
#     NEU_<Vorlage>_<Textur>_<frei>, z. B. NEU_0448_bbal_ball1 (Physik wie Schild 0448, Textur bbal)
#     optional HIT_<Vorlage>_<Textur>_<frei> (gleicher Rest): dessen Hülle wird die Box in Base (Hitbox), sonst die Hülle des Modells
#     Vorlage: platziertes Objekt (trak_clone.py <crp> --list sgn|crate|bush), Textur: 4-Zeichen-Name
#     aus der .fsh (eigenes Bild vorher mit fsh_add.py einfügen), mNNN = Material NNN, - = wie Vorlage
python -I work/track/fsh_add.py "Game CD/GameData/Track/coastal.fsh" work/track/out/coastal.fsh bbal work/track/models/beachball.png
```

```
# fshtool entpacken / repacken (vor dem Repack immer die Zieldatei sichern, FSH-REPACK.md §8)
fshtool.exe <datei>.fsh [<dir>]        # erzeugt BMPs + INDEX.FSH
fshtool.exe <dir>/INDEX.FSH [<ziel>]   # packt aus den BMPs
# bei vergrößerten Texturen vorher BUFSZ in INDEX.FSH erhöhen (FSH-REPACK.md §11.1)

# dx11z.dll bauen (Ausgabe: nfs5_enhanced-1.1.0-mod-sourcecode/drivers/dx11/build/dx11z.dll)
work\patches\dx11z\build_dx11z.cmd

# Scene-Shader offline prüfen (HLSL vorher aus src/shaders.h herauslösen)
fxc /nologo /T ps_4_0 /E MainPS scene.hlsl
fxc /nologo /T vs_4_0 /E MainVS scene.hlsl
```

## 3. Verzeichnisse

```
G:\NFSP\
  doc\                 # Doku (PLAN.md, knowledge.md, Themen-Dokumente)
    screenshots\       # Belegbilder je Bug
  work\
    extract\           # Ausgaben der Extraktion
    hd\                # HD-Textur-Spezifikation und Pipeline
    patches\dx11z\     # Patch-Kette für dx11z.dll + Build-Skript
    logs\              # gesicherte Spiel-Logs (*.log ist per .gitignore ausgeschlossen, bei Bedarf mit git add -f)
  GhidraNFSP\          # Ghidra-Projekt + Exporte
  Game CD\             # installiertes Spiel mit Mod (nicht versioniert)
```

## edg_obj.py – Randbegrenzung (.edg) ↔ Blender

```
python -I edg_obj.py export "<Spiel>/GameData/Track/coastal.edg" out/coastal_edg.obj
python -I edg_obj.py import "<Spiel>/GameData/Track/coastal.edg" out/coastal_edg.obj out/coastal.edg
```
Art: 0 Stein, 16 Metall-Leitplanke, 32 Holz, 96 Haus. Neue Linien in Objekten `NEUEDG_<art>_<frei>` (Kanten oder Flächenränder) werden angehängt, das Original bleibt. Ausgabe als `coastal.edg` ins Spiel kopieren (Original sichern).
