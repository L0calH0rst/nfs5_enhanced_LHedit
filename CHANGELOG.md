# Changelog

## 1.1.0 (2026-10-13)

### Direct3D 11 renderer

- New graphics driver `dx11z.dll`, a Direct3D 11 implementation of the game's THRASH driver
  interface that replaces the DirectX 7 `dx7z.dll`. It is now the default (`ThrashDriver = dx11`),
  `ThrashDriver = dx` returns to the stock driver.
- The fixed function pipeline of the original driver is emulated in shaders: two texture stages
  with all combine modes the game uses, specular, alpha test, vertex and table fog, the depth bias
  and flat shading quirks of `dx7z.dll`.
- The 640x480 arage and showroom are rendered at the size they cover on the display, races
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