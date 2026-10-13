# dx11z.dll, Direct3D 11 THRASH driver.

Experimental replacement for the stock `dx7z.dll`. It exports all the required entry points, keeps the D3D7
render and texture stage state that dx7z would have set, and draws the game's pre transformed vertices with
a shader that emulates the fixed function pipeline.

* The game renders into an off screen target, by default at the size the picture covers on the
  display and with 4x MSAA, which is fitted with its aspect ratio into a borderless flip model swap
  chain covering the monitor. Locks and rectangle copies see a game sized copy of the frame,
  writes are scaled back onto it.
* Settings live in `[Dx11]` of `nfs5_enhanced.ini`: `ResolutionScale`, `Antialiasing`, `Anisotropy`,
  `WindowMode` (borderless, exclusive fullscreen at the desktop resolution, or a resizable window)
  `WindowWidth` / `WindowHeight` for the window, `VSync` and `MinimizeOnFocusLoss` for Alt+Tab.
* All D3D objects are released in `THRASH_restore`, so the process can exit.
* Brightness (internal state `2E`) is applied on present instead of through the display gamma ramp.
* Lock and read back go through a staging copy of the frame, the lock is 32 bit BGRA.

## Use

Copy `dx11z.dll` to the game's `Drivers` folder and set `ThrashDriver = dx11` under `[Compat]` in
`nfs5_enhanced.ini`, or set the registry value Thrash Driver to `dx11`. The driver writes `dx11z.log`
next to itself.