# NFS5 Combined Patch v1.0 with nfs5_enhanced and LFSP

Source: nfsaddons.com "NFS: Porsche Unleashed Combined Patch v1.0". Contents of its `Copy` folder were
compared byte by byte with an install of the official v3.5 `Porsche.exe` (MD5 `316f33a5…`) running
`nfs5_enhanced.asi` with `dx11z.dll`, `lfsp.asi`, `nfsp_texpool.asi` and the Window Opener `.tpg` files.

## Safe to install

Copy these from the patch's `Copy` folder into the game folder. Back up the originals first.

| File(s) | Fixes |
|---|---|
| `GameData/DashHud/speedKN.fsh` | Correct night km/h speedometer for Boxster, 911 (996), GT1, GT3 (fix 34). With LFSP the gauges follow the light switch, so this bug also shows on day tracks. |
| `FEData/Data/nfs5.car`, `FEData/Data/nfs5.prt` | Car and parts data: HUD tachometer/speedometer types (fixes 1, 32, 33), gear ratios, upgrade parts, prices, years, addon cars in Evolution (fixes 2–31, 40) |
| `FEData/Locale/festrings.csv`, `FEData/Locale/festrings.loc` | Text for the addon cars and new parts. Install it together with `nfs5.car`/`nfs5.prt`. |
| `FEData/Layouts/garage.lay`, `usedcars.lay`, `loadreplay.lay` | Adds the Showcase buttons (fix 30) |
| `GameData/Simulation/CarData/928coupeGTS54.sim`, `959coupe29.sim`, `993coupeS36.sim` | Addon car physics, 993 Carrera S speed boost fix |
| `GameData/CarModel/*` **except `carcmn.fsh`** (77 files: `*.fsh`, `356_1.crp`, `cop356_german.tpg`) | Interior speedometer ranges (fixes 35, 36), windows stay clear after cracking (fix 38), 356 rims and tach needle, cop 356 crash (fix 39) |
| `GameData/Track/Cops/monaco1.cop` … `monaco5.cop` | New files, cops for the Monte Carlo tracks (fix 37) |
| `GameData/Render/common.fsh` | Optional, cosmetic: "Smooth Shines and Lights" (softer light flares) |

Notes:

- Install the `FEData` files as one set: `nfs5.car`, `nfs5.prt` and `festrings.*` refer to each other.
- Back up `SaveData` before you install them, because they change the car and part lists an
  existing Evolution career uses. This was not tested with an existing career.
- `356_1.crp` is the only changed car geometry. LFSP's window table uses `356_1.2` (Cabrio). After
  installing, check that the side windows on the Pre-A 356 Cabrio still open and close.
- The Window Opener `.tpg` files are not affected. The patch changes only `cop356_german.tpg`.
  LFSP does not need the Window Opener files: its window table is built in (`windows_table.h`), and
  it reads the original `.tpg` files (`[fileN.top]`). Keep the original (`cwindow`) `.tpg` files installed.
  Installing the `owindow` set would open the windows permanently.

Installed in `Game CD` on 2026-10-17: everything in "Safe to install" except `common.fsh` (92 files).
Originals are in `Game CD/_backup/combined-patch-orig/`, and `speedKN.fsh.orig` is next to the new `speedKN.fsh`.
- If you repack car textures (FSH work), use the patched `*.fsh` files as the base.

## Do not install

| File(s) | Reason |
|---|---|
| `Porsche.exe` | A different build from the v3.5 exe that nfs5_enhanced requires (about 1,600 different bytes). LFSP and nfs5_enhanced hook fixed addresses and would fail. The installed v3.5 already contains the patch's dashboard/headlight fix. nfs5_enhanced covers the low texture quality fix. Not ported yet: lens flare (`0x6059E`), B button closing the pits (`0xD2996`), 32-bit menu colors. |
| `GameData/CarModel/carcmn.fsh` | Already modified locally (2026-10-16), so the patch would overwrite local work |
| `Drivers/dx7z.dll`, `Drivers/voodoo2z.dll`, `Drivers/dx7z.7z` | Meant for nGlide. nfs5_enhanced uses `dx11z.dll`. The stock drivers stay as a fallback. |
| `gimme.dll` | A 5 KB replacement, written in Delphi (internal name `FaceInRace.dll`). It only rewrites the single export the game uses, `ConvertFile`, which turns the selected driver photo from `AllDrivr.fsh` into `SaveData/pic16.fsh`, to avoid the original's crash after a race. The original (4.6 MB) is EA's full image library, but the game only calls `ConvertFile`. nfs5_enhanced fixes that crash in the original DLL, checking a signature first. With the replacement, that fix finds no match. |
| `Porsche.sdb` | Compatibility shim for the CPU speed problem. nfs5_enhanced already fixes it. |
| `SaveData/8Player.sav`, `player.lst`, `XXhud1XX.hud`, `XXhud2XX.hud` | Would replace your profile and HUD settings |
| `properties.txt`, `version.txt` | No functional change (timestamp / version label) |
| `Install/*` (nGlide, regfix) | Not used with `dx11z.dll` |
