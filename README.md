# NFS5 Enhanced (LHedit) - Need for Speed: Porsche Unleashed Enhanced Patch but expanded


## Overview

### Original Mod (`nfs5_enhanced`)
**NFS5 Enhanced** is an *enhanced vanilla* patch for *Need for Speed: Porsche Unleashed*. It deliberately avoids modifying or expanding the game's visuals, lighting, cars, tracks, or graphics features—focusing strictly on broad compatibility, Hor+ widescreen projection, 640x480 HUD scaling, high-refresh-rate simulation interpolation (64 Hz), native SDL3 controller support, and modern Windows 10/11 crash fixes. It introduced a modern replacement Direct3D 11 THRASH driver (`dx11z.dll`).

For general errors, base setup explanations, or core troubleshooting, please refer to the original [nfs5_enhanced repository](https://github.com/iArtorias/nfs5_enhanced) first.

### LHedit Enhancements (`nfs5_enhanced_LHedit`)
Unlike the original mod which intentionally kept vanilla visuals unchanged, **LHedit** actively expands the visual and features of the game. Built on top of NFS5 Enhanced v1.2.0, LHedit enhances `dx11z.dll` with real-time player and opponent headlight illumination (including high beams), tail/brake/signal light glows, real-time water reflections, special alpha edge (`B4`) and back-face geometry (`B8`) passes, as well as extended Hor+ widescreen sky and world culling hooks.

> [!NOTE]
> **Pipeline Architecture Note:** LHedit intentionally bypasses the v1.2.0 performance state-caching and ring-buffer streaming rewrite for pipeline execution (`ApplyPipeline` / `Flush`). Because LHedit's extra rendering passes (headlights, light glows, water reflections, B4 alpha edge, B8 back-face) bind state and buffers outside of the v1.2.0 state cache assumptions, LHedit uses full state rebinding and per-draw buffer discards to guarantee render state correctness and stability across all custom passes.
> If not stated otherwise, this fork functions exactly like the original nfs5_enhanced

---

## LHedit Feature Highlights

### Direct3D 11 Lighting & Reflection Enhancements

| Feature | What it does |
|---|---|
| Per-pixel headlights | Real-time headlight rendering for player (including high beams) and opponent cars per pixel (night & day options, customizable light map). |
| Model year headlight tinting | Headlight colours follow the car's model year, using the colour table in `lfsp.ini` (needs LFSP). |
| High beams | Hold the high beam key (default `K`, set in `lfsp.ini`) for longer, wider and brighter light with a short fade; off when the headlights are broken (needs LFSP). |
| Tail, brake & signal glows | Soft light glows rendered on ground and surfaces behind and in front of all vehicles for tail, brake, turn signal (driven by native in-game controls), and reversing lights. |
| Dynamic water reflections | Direct3D 11 water reflections rendered directly into water surfaces, replacing static legacy baked textures. |
| Alpha Edge Pass (B4) | Dedicated alpha edge rendering pass (`B4`) for correct transparency edges and foliage alpha testing. |
| Back-Face Pass (B8) | Dedicated back-face pass (`B8`) ensuring correct rendering of double-sided geometry, shadows, and reflections. |
| Distance probe | Crosshair probe (`DistanceKey` in `lfsp.ini`, default `INSERT`) measuring depth under the cursor in game metres. |
| Vehicle lamp detection | LFSP records lamp numbers, positions and colours as the game draws each lamp; dx11z uses them for the signal and reversing glows (needs LFSP). |
| Diagnostic Measure Log | Detailed performance measuring and pass timing recorded in `dx11z.log` when enabled. |

### Widescreen & Culling Enhancements

| Feature | What it does |
|---|---|
| Hor+ Sky & World Cull Hooks | Widens the half-FOV used during skybox and world geometry culling calculations so distant terrain, objects, and sky elements do not disappear or pop at screen edges on widescreen displays. |

### Menu Enhancements

| Feature | What it does |
|---|---|
| Menu list scrolling | Lists in the menus (cars, paints, tracks) scroll with the mouse wheel over the list and with the arrow, page and home/end keys. The wheel over a closed drop down list picks the next or previous entry. Settings in `[Menu]`. |

---

## Requirements

- Windows 10 or 11
- **Need for Speed: Porsche Unleashed**, USA release plus official **v3.5** patch applied over
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) installed as `dinput.dll` in the game folder
- [SDL3](https://github.com/libsdl-org/SDL) (`SDL3.dll`) for gamepad support
- **LFSP** (`lfsp.asi` and `lfsp.ini` next to `Porsche.exe`) for the lighting and water features: dx11z reads their settings from `lfsp.ini` and asks `lfsp.asi` for the render queue list (water reflections), lamp data, model year tint and high beam. Without LFSP these features stay off or fall back to plain behaviour.

---

## Installation

### 1. Get the files
Unzip the release package into a folder.

### 2. Copy into game folder
Copy `nfs5_enhanced.asi`, `nfs5_enhanced.ini`, `lfsp.asi` (from `lfsp/`), `lfsp.ini` (from `config/`) and `SDL3.dll` next to `Porsche.exe`, and `dx11z.dll` into the `Drivers` directory of the game.

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
| `[Menu]` | Mouse wheel and key scrolling of the menu lists. |

The LHedit lighting and water settings are in `lfsp.ini` of LFSP, sections `[Lights]`, `[Water]` and `[World]`. A `[Lights]` key missing there is still read from `[Dx11]` of `nfs5_enhanced.ini`.


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
