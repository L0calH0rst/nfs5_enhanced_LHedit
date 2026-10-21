# Changelog

## 1.2.0-LH.1 (2026-10-22)

LHedit build on top of upstream 1.2.0.

### Direct3D 11 renderer (dx11z)

- Lighting and water reflections working together with LFSP (`lfsp.asi` / `lfsp.ini`): lamp data,
  headlight tint by model year, high beam, tail/brake/signal/reverse glows, water reflection queue.
  Without LFSP these features stay off.
- The racer list is read through `LfspCarList`.
- Distance measure log.

### Widescreen

- Hor+ culling: the half FOV used for sky and world culling is widened, so ground, objects and
  sky at the screen edges are no longer culled on wide displays.

### Detail

- Nearby objects that the occlusion walls would hide are drawn, and the sides of distant objects
  are tested against the occlusion walls (no more missing scenery behind walls).
- Wheels keep their suspension at LOD 0 with `FullCarDetail`.

### Menu

- Menu lists (cars, paints, tracks) scroll with the mouse wheel and the arrow, page and home/end
  keys; the wheel over a closed drop down list picks the next or previous entry (`[Menu]`).

### Build

- GitHub Actions workflow builds the release package (Win32, MSVC) for every `v*` tag.

## 1.2.0 (2026-10-19)

### Controllers

- New `[Controller]` section: gamepads through SDL3 (`SDL3.dll`, loaded at run time). DualSense,
  DualShock 4, Xbox, Switch Pro and every other pad SDL knows work without DS4Windows or similar.
- Pads can be connected and disconnected at any time, the one pressed last is used.
- The game sees a single controller that is always present and listed right after the keyboard,
  so bindings are no longer lost between starts. The game reset all bindings whenever the
  controller a binding pointed at was missing or listed at another place.
- DirectInput copies of pads SDL reads are left out of the list (`HideHandledDevices`).
- Modern default bindings for the gamepad (`TriggerBindings`): right trigger accelerate, left
  trigger brake, left stick steer, A hand brake, B camera, X look behind, Y reset car, shoulders
  shift up and down.
- Force feedback is played as rumble (`Rumble`, `RumbleStrength`).

### Direct3D 11 renderer

- Much less CPU time per frame: a repeated render state no longer ends the batch, vertices and
  indices go through ring buffers instead of a buffer discard per draw, and constants, states,
  textures and samplers are only rebound when they change.
- The sun glare probe copies only the 4x4 pixels it reads, it used to scale the whole frame, up to
  4K with 4x MSAA, down to the game size five times a second.
- The sun glare probe no longer waits for the GPU. It gets the pixels of its previous read, 200 ms
  earlier. The game turns the lens flare off for the race once these reads average more than
  5 ms, which waiting for a busy GPU easily took.
- Partial clears no longer create a blend state each time.
- Texture handles are looked up instead of read, a stale handle from the game no longer touches
  freed memory, and freeing textures no longer searches the whole list.

### Fixes

- The resolution list in *Options > Graphics* skipped the last mode of the driver table, which is
  the largest display mode in 32 bit, for example 2560x1600x32 on a 2560x1600 display.
- Without saved options the screen size started at 640x480x16, it now starts at the desktop size
  in 32 bit.
- The screen size is saved as the mode itself. The game kept its position in the list, which
  pointed at another mode as soon as the list changed, after a monitor or driver change or with
  `UnlockResolutions`. Positions in older saves are still read.
- The Direct3D 11 driver lists the display modes sorted by size and color depth.
- Distant scenery is drawn consistently. Two kinds of scenery objects had fixed limits of 600 and
  700 m that ignored the draw distance, so with `DrawDistanceScale` above 1 they vanished while
  the rest was still drawn. Buildings floated in the sky and ground was missing under them. Both
  limits now follow the draw distance.
- The far clip plane leaves room for large pieces of scenery, they were sliced at the horizon
  and flickered in and out as single triangles.
- New `MinDrawDistance` draws at least a fixed distance at every spot of the track, the track
  data keeps it short in many places for the hardware of 2000s.
- `DrawDistanceScale` defaults to 2, the included ini used 10.
- `ThrashDriver` and `LodDistanceScale` had different defaults in code and documentation.
- The Thrash Driver registry override could write a longer name than the buffer of the game held.

## 1.1.0 (2026-10-13)

### Direct3D 11 renderer

- New graphics driver `dx11z.dll`, a Direct3D 11 implementation of the game's THRASH driver
  interface that replaces the DirectX 7 `dx7z.dll`. It is now the default (`ThrashDriver = dx11`),
  `ThrashDriver = dx` returns to the stock driver.
- The fixed function pipeline of the original driver is emulated in shaders: two texture stages
  with all combine modes the game uses, specular, alpha test, vertex and table fog, the depth bias
  and flat shading quirks of `dx7z.dll`.
- The 640x480 garage and showroom are rendered at the size they cover on the display, races
  at their own resolution. Previously this needed DDrawCompat.
- Multisample antialiasing (`Antialiasing`, up to 8x) and optional forced anisotropic filtering
  (`Anisotropy`).
- Window modes: borderless, exclusive fullscreen at the desktop resolution, or a resizable window
  with a configurable size (`WindowMode`, `WindowWidth`, `WindowHeight`). The display mode is never
  changed to 640x480.
- `VSync` on or off, with tearing support where the system allows it.
- Alt+Tab: the full screen modes minimize when another program is activated and come back on
  return (`MinimizeOnFocusLoss`). The game's own focus grabbing, which pulled it back in front right
  after Alt+Tab, is ignored while another program is active.
- Intro and menu movies play, through the same front buffer path the game uses with the stock
  driver, in 16 bit menus as well as 32 bit modes.
- The game brightness option is applied on present instead of through the display gamma ramp.
- The window is per monitor DPI aware, so Windows scaling does not stretch the picture.
- Every display mode of the monitor is offered, the stock driver kept only about 30.
- The device is released when the game shuts the driver down, so the process always exits.
- `dx11z.log` next to the driver records the device, window and video mode changes. `Trace` in
  `[Dx11]` adds a call trace for bug reports.
- New `[Dx11]` section in `nfs5_enhanced.ini` with all of the above.

### Other

- DDrawCompat is now optional and only needed with `ThrashDriver = dx`.

## 1.0.0 (2026-10-12)

First release.