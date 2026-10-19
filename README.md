# NFS5 Enhanced (LHedit) - Need for Speed: Porsche Unleashed Enhanced Patch but expanded


## Overview

### Original Mod (`nfs5_enhanced`)
**NFS5 Enhanced** is an *enhanced vanilla* patch for *Need for Speed: Porsche Unleashed*. It deliberately avoids modifying or expanding the game's visuals, lighting, cars, tracks, or graphics features—focusing strictly on broad compatibility, Hor+ widescreen projection, 640x480 HUD scaling, high-refresh-rate simulation interpolation (64 Hz), native SDL3 controller support, and modern Windows 10/11 crash fixes. It introduced a modern replacement Direct3D 11 THRASH driver (`dx11z.dll`).

For general errors, base setup explanations, or core troubleshooting, please refer to the original [nfs5_enhanced repository](https://github.com/iArtorias/nfs5_enhanced) first.

### LHedit Enhancements (`nfs5_enhanced_LHedit`)
Unlike the original mod which intentionally kept vanilla visuals unchanged, **LHedit** actively expands the visual and features of the game. Built on top of NFS5 Enhanced v1.2.0, LHedit enhances `dx11z.dll` with real-time player and opponent headlight illumination (including high beams), tail/brake/signal light glows, real-time water reflections, special alpha edge (`B4`) and back-face geometry (`B8`) passes, as well as extended Hor+ widescreen sky and world culling hooks. All features of LFSP (L0calH0rst Light & Reflection plugin) are natively integrated directly into LHedit.

> [!NOTE]
> **Pipeline Architecture Note:** LHedit intentionally bypasses the v1.2.0 performance state-caching and ring-buffer streaming rewrite for pipeline execution (`ApplyPipeline` / `Flush`). Because LHedit's extra rendering passes (headlights, light glows, water reflections, B4 alpha edge, B8 back-face) bind state and buffers outside of the v1.2.0 state cache assumptions, LHedit uses full state rebinding and per-draw buffer discards to guarantee render state correctness and stability across all custom passes.
> If not stated otherwise, this fork functions exactly like the original nfs5_enhanced

---

## LHedit Feature Highlights

### Direct3D 11 Lighting & Reflection Enhancements

| Feature | What it does |
|---|---|
| Per-pixel headlights | Real-time headlight rendering for player (including high beams) and opponent cars per pixel (night & day options, customizable light map). |
| Model year headlight tinting | Headlight colors adapt automatically to the car's model year (warm yellow for pre-1970 classic cars, standard halogen for 1970–1995 models, cool xenon/litronic bluish tint for 1996+ models). |
| High beams | Toggleable high beam (`K` key) with extended range, beam width, height, and brightness. |
| Tail, brake & signal glows | Soft light glows rendered on ground and surfaces behind and in front of all vehicles for tail, brake, turn signal (driven by native in-game controls), and reversing lights. |
| Dynamic water reflections | Direct3D 11 water reflections rendered directly into water surfaces, replacing static legacy baked textures. |
| Alpha Edge Pass (B4) | Dedicated alpha edge rendering pass (`B4`) for correct transparency edges and foliage alpha testing. |
| Back-Face Pass (B8) | Dedicated back-face pass (`B8`) ensuring correct rendering of double-sided geometry, shadows, and reflections. |
| Distance probe | Crosshair probe (configurable `DistanceKey`, off by default) measuring depth under the cursor in game metres. |
| Inline vehicle lamp detection | Lamp positions, numbers, and colors are read directly from in-memory CRP model structures for both stock Porsche cars and newly added mod vehicles. |
| Diagnostic Measure Log | Detailed performance measuring and pass timing recorded in `dx11z.log` when enabled. |

### Widescreen & Culling Enhancements

| Feature | What it does |
|---|---|
| Hor+ Sky & World Cull Hooks | Widens the half-FOV used during skybox and world geometry culling calculations so distant terrain, objects, and sky elements do not disappear or pop at screen edges on widescreen displays. |

---

## Requirements

- Windows 10 or 11
- **Need for Speed: Porsche Unleashed**, USA release plus official **v3.5** patch applied over
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) installed as `dinput.dll` in the game folder
- [SDL3](https://github.com/libsdl-org/SDL) (`SDL3.dll`) for gamepad support

---

## Installation

### 1. Get the files
Unzip the release package into a folder.

### 2. Copy into game folder
Copy `nfs5_enhanced.asi`, `nfs5_enhanced.ini`, and `SDL3.dll` next to `Porsche.exe`, and `dx11z.dll` into the `Drivers` directory of the game.

### 3. Play
Run `Porsche.exe`. Options are configured in `nfs5_enhanced.ini`.

---

## Configuration Options

Everything is set in `nfs5_enhanced.ini`, and every setting is explained in the file. The sections:

| Section | Contents |
|---|---|
| `[General]` | Log level, coexistence with `SilentPatchNFS90s`. |
| `[Compat]` | The SilentPatch fixes, the crash fixes, graphics driver selection and fallback. |
| `[Dx11]` | Internal resolution, antialiasing, anisotropic filtering, window mode and size, VSync, Alt+Tab behaviour. |
| `[Affinity]` | Which threads are pinned and to which core. |
| `[Timing]` | Precise game clock. |
| `[Widescreen]` | Hor+, field of view, flares, resolution list, race resolution and color depth, car detail distance. |
| `[Framerate]` | Frame caps, interpolation. |
| `[IO]`, `[Loading]` | Read chunk size, free space cache, CPU clock check. |
| `[HudScaling]` | HUD size, text, gauge style, loading screen. |
| `[Text]` | Sharp text, enlargement factor and edge sharpness. |
| `[Detail]` | Draw distance, far clip, distant wheels. |
| `[Controller]` | SDL3 gamepads, rumble, dead zones, default bindings. |
| `[Lights]` | Headlights, model year tints, high beam, tail & brake glows, opponent lights. |
| `[Water]` | Direct3D 11 water reflections, texture hashes, baked reflection removal. |
| `[World]` | GuardBand clipping, distance probe key. |

Common adjustments for LHedit settings:

| I want | Set |
|---|---|
| Diagnostic measurement and rendering pass logging in `dx11z.log` | `Measure = 1` or `2` under `[Dx11]` |
| Alpha edge transparency fix for foliage and detailed textures | `AlphaEdgeFix = 1` under `[Dx11]` |
| Custom alpha edge reference threshold value | `AlphaEdgeRef = 128` (1 to 255) under `[Dx11]` |
| Force double-sided geometry rendering for back-faces | `ForceTwoSided = 1` under `[Dx11]` |
| Extended FOV culling so sky and scenery don't pop on wide screens | `HorPlus = 1` under `[Widescreen]` |
| Toggle high beams in race | `HighBeamKey = K` under `[Lights]` |
| Enable water reflections | `Reflections = 1` under `[Water]` |
| Enable 3D distance probe crosshair | `DistanceKey = INSERT` under `[World]` |

---

## Troubleshooting & General Support

For general issues, fundamental errors, or core feature explanations, check the original [nfs5_enhanced repository](https://github.com/iArtorias/nfs5_enhanced) first.

If you have specific problems with features from this build, please use the included debug options.

---

## Tools used

- Ghidra
- Claude Opus 5.5
- Notepad++
- Paint.NET

---

## Credits

- [iArtorias](https://github.com/iArtorias/nfs5_enhanced) for original [nfs5_enhanced](https://github.com/iArtorias/nfs5_enhanced) and bringing me to dive into NFSP again
- Sniper's Windows Down patch for the vehicle window opening intel (cant be found online anymore)
