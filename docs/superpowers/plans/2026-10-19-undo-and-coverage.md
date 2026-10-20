# Undo for Pattern Edits, and the Unverified Paths — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the findings from the 2026-10-19 verification of the LENGTH knob. Bring Ari's installed plugin up to date. Make step-grid edits, paste, clear and "Load demo set" undoable with Rack's Undo. Document what Initialize does. Drive the two paths nobody has run yet: paste, and pattern changes from the PAT CV input.

**Architecture:** Every pattern write already goes through `AcidBass::edit()`, on the UI thread. A new `PatternChange` undo step stores the moduleId, the slot, and the pattern before and after the change. It plugs into Rack's history system (`history::ModuleAction`). The LENGTH knob's sync in `AcidBassWidget::step()` stays unrecorded, because Rack already records knob turns as parameter changes; recording them a second time would make Undo take two presses. Verification runs in an isolated Rack. The scripts that drive it move from a session scratchpad into `tools/rig/`, so anyone can rerun them.

**Tech Stack:**
- VCV Rack 2 SDK 2.6.6, C++11, nanovg. Undo uses `history::ModuleAction`, `history::ComplexAction` and `history::ParamChange` from `include/history.hpp`.
- Windows builds use MSYS2 mingw64.
- Rig scripts are PowerShell 7 calling Win32 (PrintWindow, PostMessage).

---

## Context the engineer needs

- **Repo:** `C:\Users\Arikl\dev\active\my-lil-acid-bass-vcv`, branch `main`, remote `https://github.com/Adev-io/my-lil-acid-bass-vcv` (public). Make all changes on a new branch, `undo-history`.
- **Build (PowerShell, not Git Bash):**
  ```powershell
  $env:MSYSTEM = 'MINGW64'; $env:CHERE_INVOKING = '1'
  Set-Location C:\Users\Arikl\dev\active\my-lil-acid-bass-vcv
  & C:\msys64\usr\bin\bash.exe -lc 'export RACK_DIR=/c/dev/Rack-SDK; make test && make dist'
  ```
  `make test` must print `PASSED: 0 failure(s)`. `make dist` writes `dist/MyLilAcidBass/` (the unpacked plugin) and `dist/MyLilAcidBass-<ver>-win-x64.vcvplugin`.
- **Commits:** use `git -c user.name=A_Development -c user.email=a_development@users.noreply.github.com commit ... -- <paths>`. Never add any Co-Authored-By or AI attribution line.
- **Do not touch Ari's real Rack data** (`%LOCALAPPDATA%\Rack2`) except in Task 1, which only reinstalls this plugin. All other checks run in an isolated Rack started by `tools/rig/launch.ps1`.
- **How Rack is driven:**
  - Rack draws with OpenGL. Screenshots come from `PrintWindow`, so Rack does not need to be in front.
  - Rack reads the real cursor position, so a click briefly moves the cursor and then puts it back.
  - Posted Ctrl+key combinations do not work, because GLFW reads modifier keys from the real keyboard. Use Rack's menu bar instead: **Edit → Undo / Redo**.
  - Coordinates passed to the scripts are pixel positions in the full-window screenshot. Take a screenshot, find the target, then click it.
  - The screen is at 125 % scaling. The scripts call `SetProcessDPIAware`, so screenshot pixels match click pixels.
- **Reading state:** don't trust how a knob looks. `tools/rig/state.ps1` reads the isolated Rack's own autosave, which Rack rewrites every ~15 s, and prints the LENGTH and PATTERN params plus the lengths of slots 1–6. Wait 17 s after an action before reading it.
- **Never trigger a Rack text dialog** (for example, pasting junk from the clipboard). Native dialogs block the window, and the scripts can't dismiss them.

## Files

| File | Change | Responsibility |
|---|---|---|
| `tools/rig/launch.ps1` | create | isolated Rack user dir + plugin copy + launch |
| `tools/rig/stop.ps1` | create | close that Rack, check for a login token, delete the dir |
| `tools/rig/cap.ps1` | create | window screenshot (PrintWindow), optional zoomed crop |
| `tools/rig/click.ps1` | create | left or right click at screenshot coordinates |
| `tools/rig/type.ps1` | create | type text + Enter into the focused Rack text field |
| `tools/rig/state.ps1` | create | print LENGTH / PATTERN / slot lengths from the autosave |
| `tools/rig/patches/one-module.vcv` | create | one AcidBass, empty patterns |
| `tools/rig/patches/pat-cv.vcv` | create | AcidBass + Fundamental 8vert feeding the PAT input 0.3125 V |
| `tools/rig/README.md` | create | how to use the rig (10 lines) |
| `src/AcidBass.cpp` | modify | `PatternChange` undo step, `editRecorded()`, wire grid/paste/clear/demo |
| `README.md` | modify | Undo and Initialize notes |
| `plugin.json` | modify | version 2.0.1 |

---

### Task 1: Reinstall the current build into Ari's Rack

**Files:** none changed in the repo.

- [ ] **Step 1: Make sure Ari's Rack is closed**

Run (PowerShell):
```powershell
Get-Process Rack -ErrorAction SilentlyContinue | Select-Object Id, MainWindowTitle
```
Expected: no output. If Rack is running, ask Ari to save and close it. Don't kill it, because it holds their patch.

- [ ] **Step 2: Build and install**

```powershell
$env:MSYSTEM = 'MINGW64'; $env:CHERE_INVOKING = '1'
Set-Location C:\Users\Arikl\dev\active\my-lil-acid-bass-vcv
git status --short --branch   # expect: ## main...origin/main, nothing else
& C:\msys64\usr\bin\bash.exe -lc 'export RACK_DIR=/c/dev/Rack-SDK; make install 2>&1 | tail -2'
```
Expected: the last line is `cp dist/*.vcvplugin ".../Rack2/plugins-win-x64"/`.

- [ ] **Step 3: Read the install back**

Rack unpacks the `.vcvplugin` the next time it starts, so start it once, then read the unpacked manifest:
```powershell
Start-Process 'C:\Program Files\VCV\Rack2Free\Rack.exe'
Start-Sleep 12
(Get-Content "$env:LOCALAPPDATA\Rack2\plugins-win-x64\MyLilAcidBass\plugin.json" -Raw | ConvertFrom-Json).modules[0].description
```
Expected: `Acid bass voice (saw/square, resonant 24 dB filter, accent, slide) with a 32-step sequencer and 32 pattern slots`. If it still says `303-style`, Rack did not unpack the new package. Check `%LOCALAPPDATA%\Rack2\log.txt` for `Extracting package`.

Nothing to commit.

---

### Task 2: Move the verification rig into the repo

**Files:**
- Create: `tools/rig/launch.ps1`, `tools/rig/stop.ps1`, `tools/rig/cap.ps1`, `tools/rig/click.ps1`, `tools/rig/type.ps1`, `tools/rig/state.ps1`
- Create: `tools/rig/patches/one-module.vcv`, `tools/rig/patches/pat-cv.vcv`, `tools/rig/README.md`

These scripts aren't shipped: `Makefile` only packages `res` and `LICENSE`.

- [ ] **Step 1: Branch**

```powershell
Set-Location C:\Users\Arikl\dev\active\my-lil-acid-bass-vcv
git switch -c undo-history
```

- [ ] **Step 2: Create `tools/rig/launch.ps1`**

```powershell
# Starts an isolated VCV Rack: a fresh user dir under %TEMP% holding only the dist build of this plugin (plus any
# extra plugins named, copied from Ari's Rack), with tips turned off. Ari's own Rack data is never touched.
# Usage: pwsh -File tools/rig/launch.ps1 -Patch tools/rig/patches/one-module.vcv [-Extra Fundamental]
param([Parameter(Mandatory)][string]$Patch, [string[]]$Extra = @())
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot)
$dist = Join-Path $repo 'dist\MyLilAcidBass'
if (-not (Test-Path "$dist\plugin.dll")) { throw "No build at $dist. Run make dist first." }
$u = Join-Path $env:TEMP ("acidbass-rig-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory "$u\plugins-win-x64" | Out-Null
Copy-Item $dist "$u\plugins-win-x64\" -Recurse
foreach ($x in $Extra) { Copy-Item "$env:LOCALAPPDATA\Rack2\plugins-win-x64\$x" "$u\plugins-win-x64\" -Recurse }
'{ "tipIndex": -1 }' | Set-Content -Encoding utf8NoBOM "$u\settings.json"
$p = Start-Process 'C:\Program Files\VCV\Rack2Free\Rack.exe' -ArgumentList '-u', $u, (Resolve-Path $Patch).Path -PassThru
Start-Sleep 12
if ($p.HasExited) { throw "Rack exited (code $($p.ExitCode)). See $u\log.txt" }
"pid=$($p.Id)"
"dir=$u"
```

- [ ] **Step 3: Create `tools/rig/stop.ps1`**

```powershell
# Closes the isolated Rack started by launch.ps1 and deletes its user dir. It refuses any dir that is not an
# acidbass-rig-* folder under %TEMP%, and warns if a VCV login token was saved (a fresh dir can get logged in).
param([Parameter(Mandatory)][ValidatePattern('\\acidbass-rig-\d{8}-\d{6}$')][string]$Dir)
$ErrorActionPreference = 'Stop'
if (-not $Dir.StartsWith($env:TEMP)) { throw "Not under TEMP: $Dir" }
Get-Process Rack -ErrorAction SilentlyContinue | Where-Object {
    (Get-CimInstance Win32_Process -Filter "ProcessId=$($_.Id)").CommandLine -like "*$Dir*"
} | ForEach-Object { $_.CloseMainWindow() | Out-Null; $_ | Wait-Process -Timeout 30 }
$s = Get-Content "$Dir\settings.json" -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
if ($s.token) { Write-Warning "A VCV login token was saved in $Dir; deleting it with the dir." }
Remove-Item $Dir -Recurse -Force
"deleted $Dir"
```

- [ ] **Step 4: Create `tools/rig/cap.ps1`**

```powershell
# Screenshot of a Rack window via PrintWindow (Rack does not have to be in front). Optional crop + nearest-neighbour zoom.
# Saves to Desktop\screenshots by default (Ari's rule: every screenshot lands there).
param([Parameter(Mandatory)][int]$ProcId, [Parameter(Mandatory)][string]$Name, [int]$X = 0, [int]$Y = 0, [int]$W = 0, [int]$H = 0, [int]$Scale = 1)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class RigCap { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
public struct RECT { public int L, T, R, B; } }
"@
[RigCap]::SetProcessDPIAware() | Out-Null
$hwnd = (Get-Process -Id $ProcId).MainWindowHandle
$r = New-Object RigCap+RECT; [RigCap]::GetWindowRect($hwnd, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc(); [RigCap]::PrintWindow($hwnd, $dc, 2) | Out-Null; $g.ReleaseHdc($dc)
if ($W -gt 0) {
    $c = $bmp.Clone((New-Object System.Drawing.Rectangle $X, $Y, $W, $H), $bmp.PixelFormat)
    $bmp = New-Object System.Drawing.Bitmap ($W * $Scale), ($H * $Scale)
    $g2 = [System.Drawing.Graphics]::FromImage($bmp); $g2.InterpolationMode = 'NearestNeighbor'; $g2.DrawImage($c, 0, 0, $W * $Scale, $H * $Scale)
}
$out = Join-Path "$env:USERPROFILE\Desktop\screenshots" "$Name.png"
$bmp.Save($out); $out
```

- [ ] **Step 5: Create `tools/rig/click.ps1`**

```powershell
# Click at screenshot pixel coordinates (from cap.ps1's full-window image). Rack reads the real cursor, so the cursor
# moves there for ~0.4 s, the button messages are posted to Rack's window only, then the cursor goes back.
param([Parameter(Mandatory)][int]$ProcId, [Parameter(Mandatory)][int]$ImgX, [Parameter(Mandatory)][int]$ImgY, [ValidateSet('Left', 'Right')][string]$Button = 'Left')
Add-Type @"
using System; using System.Runtime.InteropServices;
public class RigClick { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l); [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y); [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
public struct RECT { public int L, T, R, B; } public struct POINT { public int X, Y; } }
"@
[RigClick]::SetProcessDPIAware() | Out-Null
$hwnd = (Get-Process -Id $ProcId).MainWindowHandle
$r = New-Object RigClick+RECT; [RigClick]::GetWindowRect($hwnd, [ref]$r) | Out-Null
$o = New-Object RigClick+POINT; [RigClick]::ClientToScreen($hwnd, [ref]$o) | Out-Null
$cx = $ImgX - ($o.X - $r.L); $cy = $ImgY - ($o.Y - $r.T)
$was = New-Object RigClick+POINT; [RigClick]::GetCursorPos([ref]$was) | Out-Null
$lp = [IntPtr](($cy -shl 16) -bor ($cx -band 0xffff))
$down, $up, $mk = if ($Button -eq 'Left') { 0x0201, 0x0202, 1 } else { 0x0204, 0x0205, 2 }
[RigClick]::SetCursorPos($o.X + $cx, $o.Y + $cy) | Out-Null
[RigClick]::PostMessage($hwnd, 0x0200, [IntPtr]0, $lp) | Out-Null; Start-Sleep -Milliseconds 150
[RigClick]::PostMessage($hwnd, $down, [IntPtr]$mk, $lp) | Out-Null; Start-Sleep -Milliseconds 100
[RigClick]::PostMessage($hwnd, $up, [IntPtr]0, $lp) | Out-Null; Start-Sleep -Milliseconds 150
[RigClick]::SetCursorPos($was.X, $was.Y) | Out-Null
"$Button click at $ImgX,$ImgY"
```

- [ ] **Step 6: Create `tools/rig/type.ps1`**

```powershell
# Types text then Enter into Rack's focused text field (e.g. the value field a right-click on a knob opens).
param([Parameter(Mandatory)][int]$ProcId, [Parameter(Mandatory)][string]$Text)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class RigType { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l); }
"@
$hwnd = (Get-Process -Id $ProcId).MainWindowHandle
Start-Sleep -Milliseconds 300
foreach ($ch in $Text.ToCharArray()) { [RigType]::PostMessage($hwnd, 0x0102, [IntPtr][int]$ch, [IntPtr]1) | Out-Null; Start-Sleep -Milliseconds 60 }
[RigType]::PostMessage($hwnd, 0x0100, [IntPtr]0x0D, [IntPtr]0x001C0001) | Out-Null; Start-Sleep -Milliseconds 60
[RigType]::PostMessage($hwnd, 0x0101, [IntPtr]0x0D, [IntPtr]0xC01C0001) | Out-Null
"typed '$Text' + Enter"
```

- [ ] **Step 7: Create `tools/rig/state.ps1`**

```powershell
# What the isolated Rack holds right now, read from its own autosave (rewritten every ~15 s: wait 17 s after an action).
# Param ids: 24 = PATTERN, 27 = LENGTH (see AcidBass::ParamId).
param([Parameter(Mandatory)][string]$Dir)
$a = Get-Content "$Dir\autosave\patch.json" -Raw | ConvertFrom-Json
$m = $a.modules | Where-Object model -eq 'AcidBass'
$len = ($m.data.patterns[0..5] | ForEach-Object { $_.len }) -join '/'
"LENGTH={0} PATTERN={1} slots1-6 len={2} saved={3}" -f ($m.params | ? id -eq 27).value, ($m.params | ? id -eq 24).value, $len, (Get-Item "$Dir\autosave\patch.json").LastWriteTime.ToString('HH:mm:ss')
```

- [ ] **Step 8: Create `tools/rig/patches/one-module.vcv`**

```json
{"version":"2.6.6","modules":[{"id":1,"plugin":"MyLilAcidBass","model":"AcidBass","version":"2.0.0","params":[],"pos":[0,0]}],"cables":[]}
```

- [ ] **Step 9: Create `tools/rig/patches/pat-cv.vcv`**

8vert's first channel, with nothing patched into it, outputs its gain × 10 V. A gain of 0.03125 gives 0.3125 V. AcidBass reads slot = round(PATTERN knob + PAT volts × 32 / 10) = round(0 + 1.0) = slot index 1, which is **I-A2**. Input id 2 is `PATTERN_INPUT`.

```json
{"version":"2.6.6","modules":[{"id":1,"plugin":"MyLilAcidBass","model":"AcidBass","version":"2.0.0","params":[],"pos":[0,0]},{"id":2,"plugin":"Fundamental","model":"8vert","version":"2.0.0","params":[{"id":0,"value":0.03125}],"pos":[42,0]}],"cables":[{"id":1,"outputModuleId":2,"outputId":0,"inputModuleId":1,"inputId":2,"color":"#f3374b"}]}
```

- [ ] **Step 10: Create `tools/rig/README.md`**

```markdown
# Rack rig (Windows, dev only)

Drives an isolated VCV Rack 2 Free to check the module by hand-free clicks. Never touches %LOCALAPPDATA%\Rack2.

1. `make dist` (see the main README), then `pwsh -File tools/rig/launch.ps1 -Patch tools/rig/patches/one-module.vcv` → prints `pid=` and `dir=`.
2. `pwsh -File tools/rig/cap.ps1 -ProcId <pid> -Name <name>` → full-window PNG in Desktop\screenshots. Read pixel positions off it.
3. `click.ps1 -ProcId <pid> -ImgX <x> -ImgY <y> [-Button Right]`, `type.ps1 -ProcId <pid> -Text 5` (Enter is sent too).
4. `state.ps1 -Dir <dir>` → LENGTH / PATTERN / slot lengths from Rack's autosave (wait 17 s after an action).
5. Undo/Redo: click Rack's **Edit** menu (posted Ctrl+Z does not work: GLFW reads the real modifier keys).
6. `stop.ps1 -Dir <dir>` closes that Rack and deletes the dir.
Never trigger a native dialog (e.g. pasting junk): it blocks the window and the scripts cannot close it.
```

- [ ] **Step 11: Smoke-test the rig**

```powershell
Set-Location C:\Users\Arikl\dev\active\my-lil-acid-bass-vcv
& C:\msys64\usr\bin\bash.exe -lc 'export RACK_DIR=/c/dev/Rack-SDK; make dist 2>&1 | tail -1'
pwsh -File tools/rig/launch.ps1 -Patch tools/rig/patches/one-module.vcv
```
Expected: `pid=<n>` and `dir=C:\Users\Arikl\AppData\Local\Temp\acidbass-rig-<stamp>`. Then:
```powershell
pwsh -File tools/rig/cap.ps1 -ProcId <pid> -Name rig-smoke
```
Open the PNG with the Read tool. Expected: the MY LIL ACID BASS panel with an empty grid. If a "Welcome to VCV Rack" tips box still shows, click its **Close** button (`click.ps1` at its pixel position) and note in `tools/rig/README.md` that `tipIndex` didn't suppress it.
```powershell
Start-Sleep 17; pwsh -File tools/rig/state.ps1 -Dir <dir>
```
Expected: `LENGTH=16 PATTERN=0 slots1-6 len=16/16/16/16/16/16 saved=<time>`. Then `pwsh -File tools/rig/stop.ps1 -Dir <dir>`, which should end with `deleted ...`.

- [ ] **Step 12: Commit**

```powershell
git add tools/rig
git -c user.name=A_Development -c user.email=a_development@users.noreply.github.com commit -m "Rack rig: scripts to drive an isolated Rack for hands-on checks" -- tools/rig
```

---

### Task 3: Record the missing undo, before the fix (failing check)

**Files:** none changed. This task captures the bug so Task 4 can show it fixed.

- [ ] **Step 1: Launch and load the demo set**

```powershell
pwsh -File tools/rig/launch.ps1 -Patch tools/rig/patches/one-module.vcv
```
Take a screenshot with `cap.ps1 -Name undo-before-0`. Right-click a blank part of the panel, such as the area beside the A_DEVELOPMENT label at the bottom (`click.ps1 ... -Button Right`). Screenshot again, find **Load demo set (I-A1..I-A6, and its sound)**, and left-click it.

- [ ] **Step 2: Click step number 12 in the grid header**

The header is the top row of the dark grid. Find the cell labelled `12` in a fresh screenshot and left-click it. Wait 17 s, then run `state.ps1`.
Expected: `LENGTH=12`, and slot 1 length `12`.

- [ ] **Step 3: Open the Edit menu and record what Undo offers**

Left-click **Edit** in Rack's menu bar, then take a screenshot with `cap.ps1 -Name undo-before-editmenu`.
Expected (the bug): the first item is **not** about the pattern. It is either "Undo change parameter" (from the demo-set knob changes) or greyed out. Click **Undo** if it's enabled, wait 17 s, and run `state.ps1`.
Expected (the bug): slot 1 length is still **12**.

- [ ] **Step 4: Stop**

`pwsh -File tools/rig/stop.ps1 -Dir <dir>`. Write down both screenshots' paths for the final report.

---

### Task 4: Make pattern edits undoable

**Files:**
- Modify: `src/AcidBass.cpp`: add the undo step after `struct AcidBass` (right before the `// The step grid:` comment block, around line 275), then change the five call sites listed below.

- [ ] **Step 1: Add `PatternChange` and `editRecorded()`**

Insert this block right before the line `// ---------------------------------------------------------------------------------------------------------------------` that starts the step-grid section:

```cpp
// ---------------------------------------------------------------------------------------------------------------------
// Undo for pattern edits (grid clicks, paste, clear, demo set). Rack itself records knob turns as parameter changes, so
// the LENGTH knob's sync in AcidBassWidget::step() records nothing: recording it too would need two Undos per turn.
// Actions hold the moduleId, never a pointer: Rack may delete and restore the module between push and undo.

struct PatternChange : history::ModuleAction
{
    int slot = 0;
    Pattern before, after;
    void set (const Pattern& p) const
    {
        AcidBass* m = dynamic_cast<AcidBass*> (APP->engine->getModule (moduleId));
        if (m) m->edit (slot, [&] (Pattern& dst) { dst = p; });
    }
    void undo() override { set (before); }
    void redo() override { set (after); }
};

// Applies f to one slot through edit() and returns the undo step for it, or nullptr if f changed nothing
// (for example, a click on the step number that already is the length). UI thread only, like edit().
template <typename F>
static PatternChange* editRecorded (AcidBass* m, int slot, const char* name, F f)
{
    slot = clamp (slot, 0, acidseq::slots - 1);
    PatternChange* a = new PatternChange;
    a->moduleId = m->id; a->slot = slot; a->name = name;
    a->before = m->bank[slot];
    m->edit (slot, f);
    a->after = m->bank[slot];
    if (acidseq::toJson (a->before) == acidseq::toJson (a->after)) { delete a; return nullptr; }
    return a;
}

static void pushUndo (history::Action* a) { if (a) APP->history->push (a); }
```

Why compare with `toJson` rather than `memcmp`: `Pattern` holds bools and an enum, so its padding bytes aren't defined, and `memcmp` could report a change where there is none. `acidseq::toJson` already writes every field that matters.

- [ ] **Step 2: Record grid clicks**

In `StepGrid::onButton`, replace

```cpp
        module->edit (module->curSlot.load(), [&] (Pattern& p) {
```

with

```cpp
        pushUndo (editRecorded (module, module->curSlot.load(), row == 0 ? "set pattern length" : "edit step", [&] (Pattern& p) {
```

and the closing line of that lambda, a few lines below,

```cpp
        });
    }

    void drawLayer
```

with

```cpp
        }));
    }

    void drawLayer
```

- [ ] **Step 3: Record paste and clear**

In `AcidBassWidget::appendContextMenu`, replace

```cpp
            m->edit (slot, [&] (Pattern& dst) { dst = p; });
        }));
        menu->addChild (createMenuItem ("Clear pattern", "", [=]() { m->edit (slot, [] (Pattern& dst) { dst = Pattern(); }); }));
```

with

```cpp
            pushUndo (editRecorded (m, slot, "paste pattern", [&] (Pattern& dst) { dst = p; }));
        }));
        menu->addChild (createMenuItem ("Clear pattern", "", [=]() { pushUndo (editRecorded (m, slot, "clear pattern", [] (Pattern& dst) { dst = Pattern(); })); }));
```

- [ ] **Step 4: Record "Load demo set" as one undo step, patterns and knobs together**

Replace the whole demo-set menu item:

```cpp
        menu->addChild (createMenuItem ("Load demo set (I-A1..I-A6, and its sound)", "", [=]() {
            for (int i = 0; i < 6; ++i) { Pattern p; if (acidseq::parseNotation (acidseq::demo[i], p)) m->edit (i, [&] (Pattern& dst) { dst = p; }); }
            const struct { int id; float v; } sound[] = {
                { AcidBass::CUTOFF_PARAM, cutoffKnob (265.3f) }, { AcidBass::RES_PARAM, 0.72f }, { AcidBass::ENVMOD_PARAM, 0.75f },
                { AcidBass::DECAY_PARAM, 0.59f }, { AcidBass::ACCENT_PARAM, 0.8f }, { AcidBass::VOLUME_PARAM, 0.7f },
                { AcidBass::TEMPO_PARAM, 134.f }, { AcidBass::PATTERN_PARAM, 0.f },
            };
            for (const auto& s : sound) m->paramQuantities[s.id]->setValue (s.v);
        }));
```

with

```cpp
        menu->addChild (createMenuItem ("Load demo set (I-A1..I-A6, and its sound)", "", [=]() {
            history::ComplexAction* all = new history::ComplexAction;
            all->name = "load demo set";
            for (int i = 0; i < 6; ++i)
            {
                Pattern p;
                if (! acidseq::parseNotation (acidseq::demo[i], p)) continue;
                if (PatternChange* a = editRecorded (m, i, "load demo set", [&] (Pattern& dst) { dst = p; })) all->push (a);
            }
            const struct { int id; float v; } sound[] = {
                { AcidBass::CUTOFF_PARAM, cutoffKnob (265.3f) }, { AcidBass::RES_PARAM, 0.72f }, { AcidBass::ENVMOD_PARAM, 0.75f },
                { AcidBass::DECAY_PARAM, 0.59f }, { AcidBass::ACCENT_PARAM, 0.8f }, { AcidBass::VOLUME_PARAM, 0.7f },
                { AcidBass::TEMPO_PARAM, 134.f }, { AcidBass::PATTERN_PARAM, 0.f },
            };
            for (const auto& s : sound)
            {
                history::ParamChange* pc = new history::ParamChange;
                pc->moduleId = m->id; pc->paramId = s.id;
                pc->oldValue = m->paramQuantities[s.id]->getValue(); pc->newValue = s.v;
                m->paramQuantities[s.id]->setValue (s.v);
                all->push (pc);
            }
            pushUndo (all);   // never empty: the sound params are always recorded
        }));
```

Leave the LENGTH-knob sync in `AcidBassWidget::step()` alone. It must keep calling `m->edit` directly (see the comment in Step 1).

- [ ] **Step 5: Build. No warnings allowed, and the core tests still pass**

```powershell
$env:MSYSTEM = 'MINGW64'; $env:CHERE_INVOKING = '1'
Set-Location C:\Users\Arikl\dev\active\my-lil-acid-bass-vcv
& C:\msys64\usr\bin\bash.exe -lc 'export RACK_DIR=/c/dev/Rack-SDK; make test 2>&1 | grep -E "PASSED|FAIL"; make clean >/dev/null; make dist 2>&1 | grep -iE "warning|error" || echo "build clean"'
```
Expected: `PASSED: 0 failure(s)` and `build clean`.

- [ ] **Step 6: Run Task 3 again; it should now pass**

Repeat Task 3 Steps 1–2 (launch, load the demo set, click header 12, read `LENGTH=12`, slot 1 `12`). Then:
1. Click **Edit**, screenshot (`-Name undo-after-editmenu`). Expected: the first item reads **Undo set pattern length**.
2. Click it, wait 17 s, run `state.ps1`. Expected: `LENGTH=16`, slot 1 `16`. The knob follows because the pattern changed and the knob didn't.
3. Click **Edit → Redo set pattern length**, wait 17 s, run `state.ps1`. Expected: `LENGTH=12`, slot 1 `12`.
4. Click a pitch cell (any row between C' and C) in step 3, then **Edit**. Expected: **Undo edit step**. Click it, and the cell clears in the next screenshot.
5. Click header `16` when the length is already 16, then **Edit**. Expected: the first item isn't a new "set pattern length", because no-op clicks record nothing.
6. Probe: set the LENGTH knob with right-click → `type.ps1 -Text 5`, then **Edit**. Expected: **Undo change parameter**, which is Rack's own. Click it once, wait 17 s, run `state.ps1`. Expected: `LENGTH` is back to its earlier value and slot 1 matches it, after a **single** Undo.
7. Probe: **Edit → Undo** until "Undo load demo set" is offered, then click it. Wait 17 s. Expected: slots 1–6 back to `16/16/16/16/16/16` with empty steps (take a screenshot of the empty grid), and cutoff back to its default. The default cutoff param is `0.4163` (`cutoffKnob(460)` = ln(460/60) / ln(8000/60)). Read it from the autosave:
   ```powershell
   ((Get-Content "<dir>\autosave\patch.json" -Raw | ConvertFrom-Json).modules | ? model -eq AcidBass).params | ? id -eq 1
   ```
   It may be missing from the file; Rack omits params left at their default, and missing means default.

`stop.ps1` when done.

- [ ] **Step 7: Commit**

```powershell
git -c user.name=A_Development -c user.email=a_development@users.noreply.github.com commit -m @'
Undo for pattern edits

Grid clicks (step length and step edits), paste, clear and "Load demo set" are now steps in Rack's Undo/Redo, recorded as a PatternChange holding the slot's pattern before and after. The demo set is one step covering its six patterns and its knob settings. No-op edits record nothing. The LENGTH knob's sync stays unrecorded because Rack already records the knob turn, so one Undo still reverts one turn.
'@ -- src/AcidBass.cpp
```

---

### Task 5: Initialize: confirm Undo brings patterns back, then document it

Rack's **Initialize** calls `onReset()`, which clears all 32 patterns. That's normal for Rack sequencers. Rack records Initialize as a full module change (`history::ModuleChange`, old and new module JSON), so Undo should restore the patterns. This task proves that before writing it down.

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Check Undo after Initialize**

Launch `one-module.vcv`, load the demo set, wait 17 s, and run `state.ps1` (expect six lengths of 16 and a grid with notes). Then right-click the panel → **Initialize**, wait 17 s, and take a screenshot plus `state.ps1` (expect an empty grid). Then **Edit → Undo** (expected: "Undo initialize module" or similar), wait 17 s, and take a screenshot plus `state.ps1`.
Expected: the notes are back in the screenshot. If they **don't** come back, stop and report to Ari with both screenshots. Don't change `onReset()` without Ari's decision, because keeping or clearing patterns on Initialize is a product choice.

- [ ] **Step 2: Document Undo and Initialize in `README.md`**

In `README.md`, replace

```markdown
Patterns are saved with the patch.
```

with

```markdown
Patterns are saved with the patch. Edit → Undo / Redo (Ctrl+Z / Ctrl+Shift+Z) covers grid clicks, paste, clear and
Load demo set as well as the knobs. **Initialize** (right-click → Initialize, Ctrl+I) resets the knobs **and clears all
32 patterns**; Undo brings them back.
```

If Step 1 found that Undo does **not** restore the patterns, leave out the last clause ("Undo brings them back") and tell Ari.

- [ ] **Step 3: Commit**

```powershell
git -c user.name=A_Development -c user.email=a_development@users.noreply.github.com commit -m "README: Undo covers pattern edits; Initialize clears the patterns too" -- README.md
```

---

### Task 6: Drive the two unverified paths, paste and PAT CV

**Files:** none changed, unless a check fails. If one does, stop and report with the screenshot and the `state.ps1` output; don't patch around it.

- [ ] **Step 1: Paste sets the length, the knob follows, and Undo reverts it**

Launch `one-module.vcv`. Put a 10-step pattern on the clipboard:
```powershell
Set-Clipboard -Value 'A* - A A^ - A_* A - G~ A'
```
That's 10 tokens: `A*`, `-`, `A`, `A^`, `-`, `A_*`, `A`, `-`, `G~`, `A`. Right-click the panel → **Paste step notation or JSON**. Wait 17 s and run `state.ps1`.
Expected: `LENGTH=10`, slot 1 `10`, and a screenshot shows notes in steps 1–10 with steps 11+ dimmed. Then **Edit → Undo paste pattern**, wait 17 s, and run `state.ps1`. Expected: `LENGTH=16`, slot 1 `16`, and an empty grid.
Don't test a bad clipboard here. It opens a native warning dialog that blocks the window.

- [ ] **Step 2: Copy round-trip**

With the pasted pattern back (**Edit → Redo paste pattern**), right-click → **Copy as step notation**, then:
```powershell
Get-Clipboard
```
Expected: `A* - A A^ - A_* A - G~ A`. Small spacing differences are fine, but there must be exactly 10 tokens.

`stop.ps1`.

- [ ] **Step 3: PAT CV picks the slot, and the knob follows the slot CV chose**

```powershell
pwsh -File tools/rig/launch.ps1 -Patch tools/rig/patches/pat-cv.vcv -Extra Fundamental
```
Screenshot. Load the demo set (right-click AcidBass → Load demo set). Loading it sets the PATTERN knob to 0, but the 0.3125 V on PAT still adds one slot, so the slot label at the grid's top-left should read **I-A2**. Click header `8` (this sets I-A2's length to 8), wait 17 s, and run `state.ps1`.
Expected: `LENGTH=8 PATTERN=0 slots1-6 len=16/8/16/16/16/16`. PATTERN stays 0 because the knob didn't move. Only the CV added the slot, and I-A1 kept 16.

- [ ] **Step 4: Change the CV live; the knob follows without rewriting either slot**

Right-click the first knob on the 8vert, `type.ps1 -Text 0`. That's 0 V on PAT, so slot I-A1. Wait 17 s and run `state.ps1`.
Expected: `LENGTH=16`, slots `16/8/16/16/16/16` (I-A2 is still 8), and the grid label reads **I-A1**. Undo the 8vert change (**Edit → Undo change parameter**), wait 17 s, and run `state.ps1`. Expected: `LENGTH=8`, slots unchanged.

`stop.ps1`.

---

### Task 7: Version, publish, and the listening check (Ari)

**Files:**
- Modify: `plugin.json` (`"version": "2.0.0"` → `"version": "2.0.1"`)

- [ ] **Step 1: Bump the version**

In `plugin.json`, change `"version": "2.0.0"` to `"version": "2.0.1"`. Then rebuild and check the package name:
```powershell
& C:\msys64\usr\bin\bash.exe -lc 'export RACK_DIR=/c/dev/Rack-SDK; make dist 2>&1 | tail -1'; Get-ChildItem dist\*.vcvplugin | Select-Object Name
```
Expected: `MyLilAcidBass-2.0.1-win-x64.vcvplugin`.

- [ ] **Step 2: Commit, merge, tag**

```powershell
git -c user.name=A_Development -c user.email=a_development@users.noreply.github.com commit -m "Version 2.0.1" -- plugin.json
git switch main
git merge --ff-only undo-history
git -c user.name=A_Development -c user.email=a_development@users.noreply.github.com tag -a v2.0.1 -m "My lil Acid Bass 2.0.1"
git log --oneline -6 --decorate
```

- [ ] **Step 3: Push (public: ask Ari first)**

Only after Ari says yes:
```powershell
git push origin main; git push origin v2.0.1
$R = 'Adev-io/my-lil-acid-bass-vcv'; Start-Sleep 8
$id = gh run list -R $R -L 1 --json databaseId --jq '.[0].databaseId'; gh run watch $id -R $R --exit-status
gh run view $id -R $R --json jobs --jq '.jobs[] | .name + ": " + .conclusion'
```
Expected: `lin-x64: success`, `mac-arm64: success`, `mac-x64: success`.

- [ ] **Step 4: Install for Ari**

Repeat Task 1 (Rack closed → `make install` → start Rack → read back `plugin.json`). This time `version` must be `2.0.1`.

- [ ] **Step 5: Ari listens (by ear; can't be automated)**

Ask Ari to open `acid-bass-house.vcv` (File → Open, `%LOCALAPPDATA%\Rack2\patches`), load the demo set, and listen for:
1. Slides glide (the 303 "swoop") and accented notes hit harder and brighter.
2. Turning LENGTH while the drums play shortens the loop on the next pass, and the bass stays on the drum grid.
3. A header click followed by Ctrl+Z restores the loop length you heard before.
4. Nothing clicks or pops when the pattern wraps.

Record Ari's answers in the final report. Anything that sounds wrong becomes a new finding, not a fix inside this plan.

- [ ] **Step 6: VCV Library issue text**

If Ari already opened the `MyLilAcidBass` issue on github.com/VCVRack/library, the update comment is:
```
Version: 2.0.1
Commit: <full hash from `git rev-parse v2.0.1^{commit}`>
```
If not, the first-post body is the same as before, with version `2.0.1` and this hash.

---

## Out of scope (from the findings, deliberately not done)

- **Source files newer than the DLL after switching branches.** That's an artifact of Git converting line endings when it rewrites files. The content matched the build. No change.
- **Cleaning up the isolated folder.** Already handled; `stop.ps1` now does it every time.
