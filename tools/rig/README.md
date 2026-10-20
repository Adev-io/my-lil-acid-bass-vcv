# Rack rig (Windows, dev only)

Drives an isolated VCV Rack 2 Free to check the module by hand-free clicks. Never touches %LOCALAPPDATA%\Rack2.

1. `make dist` (see the main README), then `pwsh -File tools/rig/launch.ps1 -Patch tools/rig/patches/one-module.vcv` → prints `pid=` and `dir=`.
2. `pwsh -File tools/rig/cap.ps1 -ProcId <pid> -Name <name>` → full-window PNG in Desktop\screenshots. Read pixel positions off it.
3. `click.ps1 -ProcId <pid> -ImgX <x> -ImgY <y> [-Button Right]`, `type.ps1 -ProcId <pid> -Text 5` (Enter is sent too).
4. `state.ps1 -Dir <dir>` → LENGTH / PATTERN / slot lengths from Rack's autosave (wait 17 s after an action).
5. Undo/Redo: click Rack's **Edit** menu (posted Ctrl+Z does not work: GLFW reads the real modifier keys).
6. `stop.ps1 -Dir <dir>` closes that Rack and deletes the dir.
Never trigger a native dialog (e.g. pasting junk): it blocks the window and the scripts cannot close it.
