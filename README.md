# Felix the Cat (Unl) — native PC port

A GenesisRecomp port of the 1 MB Russian bootleg *Felix the Cat (Unl)*
(CRC32 `BF9BA3BC`) to C for Windows x64.

The 68000 code is translated to C by the pinned GenesisRecomp v0.1.2 framework
in `sdk/`; the runner supplies the VDP, bus, YM2612, SN76489, input, and fiber
scheduler. The generated C is a build product and is never edited by hand.

## Quick start

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release --parallel 8

Copy-Item "Felix the Cat (Unl).bin" "build\Release\felix.bin"
Set-Location build\Release
.\FelixTheCatRecomp.exe felix.bin
```

The window size follows `window_scale` in `build\Release\settings.ini`
(the included file uses 4). At `aspect = native` the window is
`320*window_scale` by `224*window_scale`; explicit aspect modes keep the same
display height and size the window to the requested ratio (`4:3` is slightly
narrower than native's 320x224 pixel grid). Start leaves the attract/title
loop. Run from `build\Release` so the ROM and input-script paths resolve as
shown below.

Headless/scripted run:

```powershell
.\FelixTheCatRecomp.exe felix.bin `
  --max-frames 5000 --input-script ..\..\tools\long_rc.input
```

Interpreter oracle (same runner, 68000 interpreted instead of recompiled):

```powershell
$env:GENESIS_FORCE_INTERP = "1"
.\FelixTheCatRecomp.exe felix.bin `
  --max-frames 5000 --input-script ..\..\tools\long_in.input
```

## Display, HUD, and mouse

Presentation is configured in the `[video]` section of
`build\Release\settings.ini`:

```ini
[video]
window_scale = 4
fullscreen = 0
linear_filter = 0
# native | 4:3 | 16:10 | 16:9 | 21:9 | 32:9
aspect = native
hud_stretch = 1
hud_rows = 40
mouse_menus = 1
```

Keep comments on their own line: the settings parser does not strip a
trailing `;` or `#` from string values such as `aspect`.

- `native` is the default and keeps the historical runner geometry exactly:
  a 320x224 window, with H32 gameplay presented as its authentic 256x224
  frame inside it and H40 menus using the full 320 px. `4:3` uses the same raw
  picture with square-pixel correction; both modes produce byte-identical VDP
  framebuffers in the verified runs.
- `16:10` and the wider ratios widen H32 gameplay only. The VDP renders
  368 px for `16:10` (7 extra cells per side) and 384 px for the wider modes
  (8 extra cells per side, the `game.toml` cap); the presentation fills the
  window.
  The title is H40 (320 px) and the options screen is H32 (256 px); both keep
  their authentic width, centered with pillarbox bars, so menu artwork and
  hitboxes do not stretch.
- `hud_stretch = 1` keeps the 256 px HUD readable on wide screens by scaling
  the bottom `hud_rows` rows (default 40) to the full output width while the
  gameplay area above it fills the window normally. Set it to `0` to keep the
  HUD at gameplay scale.
- `21:9` and `32:9` still render at most 384 px of gameplay (`game.toml`
  `ws_max_extra_cells = 8`); the extra window width scales that canvas rather
  than revealing more level.
- Legacy `widescreen = 1` maps to `16:9` when `aspect = native`. The
  `GENESIS_ASPECT` environment variable overrides the file for a run.

Mouse menus are enabled by default. Over the title or options screen, hovering
a row moves the game's own cursor; left-click starts the selected title entry
or toggles Music/Sound, and right-click acts as Back/C. The adapter synthesizes
controller input only; it never writes guest RAM, and does not touch gameplay.
Set `mouse_menus = 0` for purely keyboard/gamepad runs. For deterministic tests
the input script accepts `MOUSE <x> <y>` and `CLICK [LEFT|RIGHT]` directives,
which drive the same path as real SDL mouse events.

`GENESIS_PRESENT_DUMP=<path>` plus `GENESIS_PRESENT_FRAME=<n>` captures the
composed window image (after aspect/HUD layout) on one frame; the ordinary
`SCREENSHOT` directive continues to capture the raw VDP framebuffer.

## Status

- Final build `exe=046acae0`: 145 generated 68000 functions; 0 unsupported
  instructions; 0 unresolved jump tables; 0 dispatch misses in the verified
  runs.
- Native, explicit `4:3`, and interpreter raw captures are byte-identical at
  frames 2000, 3000, 4000, and 5000 (SHA-256).
- 16:10, 16:9, 21:9, and 32:9 gameplay plus 16:9/32:9 title presentation were
  captured with the final build and visually checked: gameplay fills the
  width, the HUD is stretched independently, and menus stay pillarboxed.
- Scripted mouse navigation was verified through the title and options menus
  (hover, click, Music/Sound toggle, right-click Back); captures, RAM dumps,
  and the log are in `build/Release/mouse/`.
- 20,000-frame run with strict JSR-stack checking on the final build: no stack
  mismatch, watchdog, or crash. Runtime floor/dispatch audits report 0 entries.
- Audio is the same FM+PSG mix, but the interpreter capture lags by one frame
  (3,722 samples, 16.637 ms) due to the boot V-int tick; after alignment the
  waveforms correlate 0.984–0.997 through 18 seconds, then diverge when the
  next track is chosen.
- Known non-graphical deviation: at about 20 seconds the game randomly selects
  one of ten music tracks using an HV-counter RNG. The two backends read the
  HV counter at slightly different horizontal positions, so they pick
  different tracks. Graphics and logic are unaffected.

See [PORTING-NOTES.md](PORTING-NOTES.md) for the ROM analysis, the three
required fixes, the build/verification commands, the display/mouse design, and
the audio assessment.
