# Excluded materials

This integration deliberately excludes:

- `GameData/Track/Cops/monaco1.cop` through `monaco5.cop` (five Monaco police files / original fix #45).
- The altered `Porsche.exe` from the NFS5 Essentials bundle (incompatible with NFS5 Enhanced's official v3.5 executable requirement).
- `SaveData/8Player.sav` (third-party savegame).
- Legacy add-on installers `Setup.exe`, per-add-on `move.txt` and alternate per-add-on `nfs5.car` files.
- Duplicate official add-on `.fsh` textures where the main Essentials archive has its own versions. Main Essentials textures are retained.
- Test logs, crash dumps, local file paths, PowerShell diagnosis and build products (`.asi`, `.lib`, `.obj`).

A separate subset of `.fsh` assets with filenames beginning with `cop` stays included because they are model/texture resources and not the five optional Monaco `.cop` files.
