# Optional NFS5 Essentials V2 integration

## Overview

This change adds support for applying a user-selected NFS5 Essentials V2 gameplay data overlay to NFS5 Enhanced without replacing the official game executable or its on-disk original `FEData/` and `GameData/`.

The data overlay contains **102 files**. Details and SHA-256 checksums: [`ESSENTIALS_V2_MANIFEST.md`](ESSENTIALS_V2_MANIFEST.md) and [`ESSENTIALS_V2_MANIFEST.csv`](ESSENTIALS_V2_MANIFEST.csv). Original fix list: [`ESSENTIALS_V2_CHANGELOG.md`](ESSENTIALS_V2_CHANGELOG.md).

### File categories

| Category | Count | Intended effect |
|---|---:|---|
| Vehicle assets | 85 | Selected Essentials textures and palettes, including bonus and police-car art |
| Official add-on assets | 13 | Missing models, colour data, palettes and simulation records for GT2, GT3, 928 GTS and 959 |
| Databases | 2 | `nfs5.car` and `nfs5.prt` — full vehicle/parts definitions, including gameplay and balance adjustments |
| Localization | 1 | `festrings.loc` |
| HUD | 1 | `speedKN.fsh` |
| **Total** | **102** | |

The full databases are shipped unmodified from the source Essentials archive. Consequently the overlay includes its associated balancing, pricing and tuning changes; these cannot be independently enabled by fix number.

### Source code and configuration

- `src/core/config.h`: introduce optional redirect and diagnostic flags (default: disabled).
- `src/core/config.cpp`: read the `GameplayRedirect` and `GameplayRedirectTrace` flags from `[IO]`.
- `src/subsystems/io_hooks/io_hooks.h`: store the overlay path and redirect hook state.
- `src/subsystems/io_hooks/io_hooks.cpp`: restrict overrides to read-only file access under `FEData/` and `GameData/`, normalize relative game paths, fall back to the original asset if the override fails, and also handle direct `CreateFileA` calls (including `.tpg` reads) outside EA's file-layer hook. The optional `GetFileAttributesA` IAT hook may be unavailable if the game executable does not import that function.
- `config/nfs5_enhanced.ini`: enable `GameplayRedirect = 1` and keep `GameplayRedirectTrace = 0` under `[IO]`. The supplied candidate package contains a snippet, not a replacement of the upstream INI file.
- `GameplayFixes/`: optional files read from the overlay. No override is active while the flag is disabled.

### How users enable the optional overlay

Place the `GameplayFixes` directory next to the game executable, then set the existing `[IO]` section of `nfs5_enhanced.ini` to:

```ini
GameplayRedirect = 1
GameplayRedirectTrace = 0
```

This integration enables `GameplayRedirect = 1` by default. Set it to `0` to disable Essentials. No native car models or databases are overwritten in their original location. The build/release packaging must preserve the `GameplayFixes/` directory.

### Compatibility and tests

- Windows 11, official USA game executable with v3.5 patch; isolated `NFS5_ASI_Redirect_TEST` directory.
- Visual Studio 2026, Win32 Release build: passed; ASI SHA-256 `2C95BD0136D65B900157774630DB011422A374555B025949C67E0744D81B2311`.
- Redirect disabled: control launch passed; missing car assets in this special test copy remain unavailable, as expected.
- Redirect enabled: Porsche 959 Quick Race and Porsche 928 GTS Quick Race launched successfully in separate recorded runs.
- Runtime logs show `CreateFileA` direct redirects for `959.tpg` and `928.tpg`, the respective `.crp` models and `.sim` files loaded from `GameplayFixes/`, and normal game termination in both runs.
- An import warning for `GetFileAttributesA` appears because `Porsche.exe` does not import that API; direct `CreateFileA` interception was effective in tested cases.

**Not yet independently established:** repeated extended-play stability; GT2 / GT3 playthroughs; exact Factory Driver Carrera RS 2.7 Class 2/3 eligibility reported in issue #15; individual validation of all original author's fixes; tests on other game builds. No claim is made that all historical crashes are fixed.

### Attribution and excluded files

Credit **ND4SPD Racer** as the original NFS5 Essentials author. See [`ESSENTIALS_V2_CREDITS.md`](ESSENTIALS_V2_CREDITS.md) and [`ESSENTIALS_V2_EXCLUSIONS.md`](ESSENTIALS_V2_EXCLUSIONS.md). The five Monaco `.cop` assets (original item #45) are not included.
