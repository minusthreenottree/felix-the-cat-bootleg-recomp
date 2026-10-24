# Felix the Cat (Unl) — GenesisRecomp porting notes

Status: native build complete and verified against the interpreter oracle,
with selectable aspect ratios, an independently stretched HUD, and mouse-driven
menus on top of the original presentation. This document records the ROM
analysis, the framework defects that had to be fixed, the project
configuration, the presentation extensions, and the evidence used to sign off.

## 1. ROM identity and boot flow

The source is the 1 MB Russian bootleg dump `Felix the Cat (Unl).bin`.

| Field | Value |
|---|---|
| Size | `0x100000` (1 MB), flat, identity-mapped |
| CRC32 | `BF9BA3BC` |
| Header title | `Felix the Cat` |
| Region | `JUE` |
| Header checksum | `0x0000`; computed `0xA4FE` |
| Reset SSP | `0x00000000` |
| Reset PC | `0x000200` |
| H-Int (IRQ4) | `0x00032E` (vector table offset `0x70`) |
| V-Int (IRQ6) | `0x00033C` (vector table offset `0x78`) |
| Shared handler | `0x00032A` |

The reset SSP is zero. The boot code clears work RAM and sets A7 itself; the
68000's 24-bit bus wraps its later stack pushes into the top of work RAM
(`$FFxxxx`). This is why the `[ram_layout]` stack fields are all zero and why
an A7 such as `0xFFFFFFE8` is normal.

Decoded boot path:

- `$200` reset; RAM clear at `$304`; init at `$512`.
- `$512` seeds the RNG (`$FF0354 = $D94B`), clears the V-int/H-int callbacks,
  then calls `$E90` (VDP init), `$2918`, `$2A2C`, `$27BC`, and `$366C`.
- `$E90` initialises the VDP and the game's display state.
- `$9F8A` performs the large game/level initialisation.
- `$A086` installs the V-int callback (`$E862`) through `$552`; `$A092` starts
  the title music (`$26260`, "Down The City").
- `$32E` (H-int) calls `$4FE` / the `$FF0004` callback.
- `$33C` (V-int) does `addq.l #1,$FF55EA`, saves registers, calls `$4C0` /
  the `$FF0000` callback, restores, and `rte`s.

The music player is a 68000 TFM Music Maker driver (`TFMcom1.22`); the game
does not use a Z80 sound driver. `$EA72` installs a song pointer at `$FF9D1E`;
`$EA94` and `$325E` are the per-V-int FM/PSG service routines.

## 2. Framework fixes (upstream, not generated C)

The port uses the pinned `sdk/` checkout (segagenesisrecomp v0.1.2, commit
`252535d12476f5a697cf87082ea3e827f6430a1b`, `m68k-recomp-core` commit
`e9ebf608933969903edd80ac0b79d41ddb2f013e`). Three defects had to be fixed
for this ROM to run. Two are framework changes, also kept as applyable patches
under `patches/`; the third fix is in the game-owned `felix_spec.c`.

### 2.1 Empty `level_modes` emitted invalid C

`recompiler/src/game_config.c`, `game_config_emit_layout()`:

An empty `level_modes = []` produced `.level_modes = { }`, which is not
portable C (MSVC rejects an empty initializer). The emitter now writes an
explicit zero element when the array is empty:

```c
if (L->level_mode_count == 0)
    fprintf(f, " 0");                 /* empty {} is not portable C; emit {0} */
```

### 2.2 `CLR` with predecrement did not update A7

`external/m68k-recomp-core/profiles/genesis/code_generator.c`, `MN_CLR`:

`clr.l -(a7)` was emitted through the read-modify-write store path
(`rmw=1`). That path assumes a preceding EA load has already applied the
`-(An)` address update; `CLR` never loads, so A7 was never decremented and the
value was written to the old stack address.

The fix uses the non-RMW store path (`rmw=0`) and accounts for the stack slot
in the split-stack bookkeeping:

```c
emit_ea_store_ex(f, instr, instr->src_ea, sz, &er2, "0", 0);
if (*has_sp_adjust && instr->src_ea == ((EA_An_PRE << 3) | 7))
    fprintf(f, "  _sp_popped -= %d; /* CLR -(SP) bytes */\n",
            sz == M68K_SIZE_L ? 4 : 2);
```

Before the fix the strict stack checker reported
`JSR stack mismatch at $000532 -> $000E90: pre=$00000000 slot=$FFFFFFFC
post=$00000030`; A7 drifted by `+0x34` through `$E90`. After the fix the
strict checker is clean through 20,000 frames.

### 2.3 V-int and H-int entry points were swapped

`felix_spec.c` originally wired `call_vblank` to `$32E` and `call_hblank` to
`$33C`. The vector table is the opposite: `$32E` is IRQ4/H-int and `$33C` is
IRQ6/V-int. With the wrong mapping the V-int handler fired roughly 111 times
per frame (`$FF55EA` reached ~119125 by frame 1072) and the game hit the
watchdog at frame 1073. The game spec now calls `$33C` for V-int and `$32E`
for H-int; `game.toml` documents the same mapping.

## 3. Project configuration

`game.toml`:

- `output_prefix = "felix"`.
- `jump_table_autodiscovery = true` (the bootleg has no disassembly).
- The vector-table entries `$200`, `$32A`, `$32E`, `$33C` are in
  `[functions].extra`.
- Four runtime-discovered indirect handlers are in `[functions].late_extra`:
  - `$00E862` — V-int sound-service callback (`JSR $EA94`, `JSR $325E`)
  - `$00E870` — input/pad callback (`$FF87AA`, `$FF73B0`)
  - `$00E944` — input/pad callback (`$FF95E6`, `$FF87A2`)
  - `$00DDB4` — per-frame game/world update (calls `$E862` and the VRAM
    helpers)

  These were first observed as Tier-3 floor entries. Adding them to
  `late_extra` lets their direct-call closure be generated after the heuristic
  fixed point. The tested runs now have no floor entries and no dispatch
  misses.

`felix_spec.c` keeps `tier3_floor_default = 1` as a safety net for any handler
that future inputs or levels may reach indirectly. It is not exercised by the
verified runs (the floor is never consulted when every dispatch target is
generated).

The RAM-layout fields other than the stack values are deliberately zero: the
only load-bearing values for this title are the zero SSP/stack fields. The
Sonic-shaped `game_mode`, `vint_runcount`, `vint_routine`, `plc_pending`, and
`player_object` names are not used by the game adapter.

## 4. Build

Prerequisites: CMake 3.16+, Visual Studio 2019 (x64), and the vendored SDL2
under `sdk/runner/external/SDL2`. The `sdk/` tree is the pinned framework 
(if sdk is not there clone it from segagenesisrecomp)
checkout; `CMakeLists.txt` also accepts `-DGENESIS_RECOMP_ROOT=<path>`.

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release --parallel 8
```

The build regenerates the C from the ROM and `game.toml` before compiling it.
The recompiler report for the final configuration:

```
[GameConfig] prefix='felix', 0 manual jump tables, 4 extra funcs
[FunctionFinder] 116 functions found
[FunctionFinder] Jump-table discovery: pc_indexed=20 auto=20 targets=300
[FunctionFinder] 0 unresolved sites
[Codegen] Function boundary split: added 17 + 12 entries
[Codegen] Final function count after boundary splitting: 145
[Codegen] Dispatch audit: 3 sites (2 static, 1 hybrid offset_table),
           0 interior, 0 external, 0 unsupported
[Codegen] Unsupported summary: TOTAL 0
```

## 5. Run and verify

From `build\Release` (the runner resolves `--input-script` relative to the
working directory), run the native build:

```powershell
.\FelixTheCatRecomp.exe felix.bin `
  --max-frames 5000 --input-script ..\..\tools\long_rc.input
```

Run the interpreter oracle (same runner, same audio/video path, only the 68K
execution engine changes):

```powershell
$env:GENESIS_FORCE_INTERP = "1"
.\FelixTheCatRecomp.exe felix.bin `
  --max-frames 5000 --input-script ..\..\tools\long_in.input
```

Strict stack checking:

```powershell
$env:GENESIS_STRICT_JSR_STACK = "1"
```

Presentation and menu input come from `[video]` in `settings.ini` (see §9).
`GENESIS_ASPECT=16:9` (and the other mode names) overrides the file for one
run; legacy `GENESIS_WIDESCREEN=1` still means 16:9 when the file says
`aspect = native`. For parity/oracle work set `mouse_menus = 0` so no host
pointer state can contribute input.

Screenshots: `GENESIS_SCREENSHOT_AT_EXIT=<path>`, or the `SCREENSHOT`
directive in an input script. Audio capture: `--wav out.wav`; the same WAV tap
is used by both backends. Useful script directives: `WAIT`, `PRESS`, `HOLD`,
`SCREENSHOT`, `DUMP_RAM`, `DUMP_VRAM`, `SAVE_STATE`, `MOUSE x y`,
`CLICK [LEFT|RIGHT]`.

`SCREENSHOT` captures the raw VDP framebuffer, which is the oracle for parity
checks. To see what the window actually shows after aspect/HUD composition,
set `GENESIS_PRESENT_DUMP=<path>` and `GENESIS_PRESENT_FRAME=<n>`; the runner
reads back the renderer immediately before `SDL_RenderPresent`.

Important: compare screenshots with **non-turbo** runs. `--turbo` is fine for
survival tests, but it changes interpreter wall-frame timing enough to move the
HV-counter random music choice and eventually the demo state. The native
turbo capture happened to still match the non-turbo oracle at frames
2000–5000, but that is not a guarantee.

## 6. Verification evidence

### Graphics parity (non-turbo)

Native and `GENESIS_FORCE_INTERP=1` captures are pixel-identical. MD5 of the
active framebuffer, frames 2000/3000/4000/5000 (re-verified with the final
`exe=046acae0` build; native, explicit `4:3`, and interpreter all produced
the same files):

| Frame | Native MD5 | Interpreter MD5 | Result |
|---|---|---|---|
| 2000 | `20a5bb83f34c9b81a6d85df2f4a365de` | `20a5bb83f34c9b81a6d85df2f4a365de` | identical |
| 3000 | `0d81d4f4d7192114fee0b2eadfa4d5f5` | `0d81d4f4d7192114fee0b2eadfa4d5f5` | identical |
| 4000 | `a3fc52755a4cc7e3a4f958916d06f93c` | `a3fc52755a4cc7e3a4f958916d06f93c` | identical |
| 5000 | `236e80e9e7581726d96a3c43240c0917` | `236e80e9e7581726d96a3c43240c0917` | identical |

A separate no-input attract/title capture at frame 1200 also matches
(`b95caafd4a367f465701b77fc91edf2f` for both;
`rc_title_1200.png` / `in_title_1200.png`). It was re-captured with the final
`exe=046acae0` build. The title animation is frame-sensitive, so this
supersedes earlier development captures.

### Display and mouse presentation

- The final `exe=046acae0` build with `aspect = native` and with the explicit
  `aspect = 4:3` produced raw framebuffers byte-identical to the verified
  baseline at frames 2000/3000/4000/5000. The 4:3 copies are
  `rc43_long_*.png`; SHA-256 values
  match the corresponding `rc_long_*.png` (for example
  `5c3557c42a47…` at 2000 and `43fca97449f0…` at 5000).
- Composed window captures are in `build/Release/present/` (manifest:
  `captures.txt`), all taken with `window_scale = 2`: `present_16x10.png`
  (716x448), `present_16x9_game.png` (796x448), `present_21x9.png`
  (1046x448), and `present_32x9.png` (1592x448). Gameplay fills the width, and
  the bottom 40-row HUD band is stretched independently (for 16:9, 184
  gameplay rows map to 368 px and 40 HUD rows to 80 px).
- `present_16x9_title.png` (796x448) and `present_32x9_title.png` (1592x448)
  show the 320 px H40 title centered with pillarbox bars; the options screen
  (H32) gets the same treatment at 256 px. Menu content is never horizontally
  stretched.
- Mouse flow was verified end to end with `MOUSE`/`CLICK` scripts and RAM
  dumps: hover over the title's Options row sets RAM `0x6017` to 1, click
  enters the options screen (`0x0020` clears from `$81`), hover selects
  Music/Sound, click toggles the setting, and right-click returns to the
  title. The final scripted run's captures and log are in
  `build/Release/mouse/` (`f_hover_opts.png`, `f_options0.png`,
  `f_hover_sound.png`, `f_sound_toggle.png`, `f_music_toggle.png`,
  `f_back_title.png`, `mouse.err`); the script is
  `tools/menu_mouse.input`. `GENESIS_MM_DEBUG=1` prints the adapter's
  screen/cursor/target state without changing behaviour.

### Stability

- Final aspect/HUD/mouse build (`exe=046acae0`), non-turbo, 5,100 frames with
  Start pressed: native, explicit `4:3`, and the interpreter oracle were all
  re-run with the final binary and matched at all four screenshots; clean.
- Same build, `GENESIS_STRICT_JSR_STACK=1`, 20,000 frames: clean, no
  `JSR stack mismatch`, no watchdog, no crash. Log:
  `strict20k_aspect.err`. (The earlier pre-extension run left
  `strict20k.png`.)
- Runtime evidence covering that 20,000-frame session:
  `floor_coverage.toml`, `floor_unsafe.toml`, `dispatch_misses.toml`, and
  `interior_label_misses.toml` all report `entries: 0` for one session and
  20,000 frames of build `046acae0`. The Tier-3 floor was never needed.
- Dispatch audit: `0` unsupported, `0` interior/external unresolved jump
  tables, `0` dispatch misses in the verified runs.

## 7. Audio assessment

The runner's WAV tap is the same generated FM+PSG mix at 223,721 Hz on both
backends; there is no backend-specific mixer in this comparison. Final-build
captures (1,600 frames, `play_rc.input` / `play_in.input`):

- `rc_audio.wav` (native) — 23,884,844 bytes, stereo, 223,721 Hz, nonzero
  audio, peak 15,281, RMS 2,833.57.
- `in_audio.wav` (interpreter) — same size/format, peak 17,347, RMS 3,121.75.

Re-running both captures on the final `exe=046acae0` build reproduced the WAV
files byte-for-byte.

At zero lag the two waveforms look uncorrelated, but that is a linear time
offset. The interpreter capture is delayed by **3,722 samples = 16.637 ms**
(approximately one 59.94 Hz frame, 16.683 ms). After shifting by that lag,
per-second waveform correlation is 0.984–0.997 through second 18, is 0.968 in
second 19 as the transition begins, then falls to roughly zero from second 20
onward. That is the sound of the same synth output with a one-frame
service-phase offset and then a different song.

The one-frame offset matches the V-int counter deviation in §8: native's sound
driver receives one extra V-int during boot and is serviced one tick ahead.
The later divergence starts exactly at the transition into the attract demo,
where the game picks one of ten music tracks using its HV-counter RNG.

Root cause, confirmed in the ROM and in RAM dumps:

- `$3882` calls the RNG at `$E52`, computes `d0 % 10`, and starts one of ten
  TFM songs (`$38C4`–`$3984`).
- `$E52` mixes `$FF0354` with the VDP HV counter at `$C00008`.
- At the first RNG call (between frames 1200 and 1250) native read
  `HV=$EA80`, the interpreter read `HV=$EA00`; the unique RNG outputs are
  `$E1B2` and `$E032`, selecting track `$3221A` (native) and `$28718`
  (interpreter).
- After that call the RNG state is unchanged (`$E1B2` / `$E032`) for the rest
  of the run — the difference is a one-shot music selection, not a continuous
  audio or synth defect.

Decision: audio parity is **not** an acceptance criterion for this port. The
graphics and game logic are pixel-identical. After removing the one-frame
service offset, the remaining audio difference is the game's own random music
choice, caused by a small HV-read timing skew between the recompiled and
interpreted 68000. Forcing a match would require cycle-exact boot/V-int and HV
phase parity — a framework timing change, not a game fix.

## 8. Known deviations

1. **V-int counter +1.** `$FF55EA` is consistently one higher in native from
   about frame 9 onward. It first appears during boot, when a pending V-int is
   delivered as the 68000 unmasks interrupts. All game timers are relative, so
   this does not affect graphics. It does mean native's sound driver has run
   one extra tick by frame 1200, visible as ±1 in `$FF9Dxx` sound state and as
   a one-frame audio phase difference. Measured on the final captures: 3,722
   samples at 223,721 Hz (16.637 ms), with the interpreter behind native.
2. **HV-counter RNG / music choice.** Described in §7. It is not a logic bug
   in the recompilation; it is a timing-domain difference between the two
   execution engines.
3. **Debug-server PC.** In recompiled builds `get_registers` reports PC `0`;
   use the RAM/chip dumps or the framelog instead. This is a known runtime
   limitation, not specific to Felix.
4. **`--turbo` and the oracle.** Do not use turbo for screenshot or audio A/B
   comparisons; it perturbs the interpreter's wall-frame timing.

## 9. Aspect, HUD, and mouse extensions

These are runner and game-spec changes only. The generated 68000 C, the ROM,
and the recompiler output are untouched; `felix_spec.c` supplies the
game-specific state, and the shared runner provides the presentation and
input plumbing.

### 9.1 Aspect selection

`[video] aspect` accepts `native` (default), `4:3`, `16:10`, `16:9`, `21:9`,
or `32:9`. The runner derives the logical size from the selected ratio at the
native 224-line display height:

| Mode | Logical size | Window at `window_scale = 2` |
|---|---|---|
| `native` | 320x224 | 640x448 |
| `4:3` | 299x224 | 598x448 |
| `16:10` | 358x224 | 716x448 |
| `16:9` | 398x224 | 796x448 |
| `21:9` | 523x224 | 1046x448 |
| `32:9` | 796x224 | 1592x448 |

For `native`, the logical size follows the VDP mode (320x224 in H40,
256x224 in H32) inside a fixed 320x224 window at scale 1; the table lists the
H40/menu shape. Explicit modes use one fixed logical width, so the window never
resizes between screens.

`native` remains the historical path: one full-frame texture copy, no aspect
composition, and byte-identical screenshots. Explicit `4:3` corrects the pixel
aspect and also leaves the raw framebuffer untouched.

For the wide modes the runner arms a fixed VDP canvas: 368 px for `16:10`
(7 extra cells per side) and 384 px for `16:9`, `21:9`, and `32:9`
(8 extra cells per side, the `game.toml` cap). Widening is gated to actual
H32 gameplay, not just to an H32 width:

- `widescreen_update_for_frame()` requires a wide aspect, a
  widescreen-capable game, and `is_menu_screen()` false.
- The title is H40 (320 px); the options screen and gameplay are both H32
  (256 px). Without the game-state gate, the options screen would be widened
  and its hitboxes would drift.
- `gvdp_set_ws_canvas()` arms that per-aspect canvas (368 or 384 px) and keeps
  one output size across screens, so the window never resizes when moving
  between title, options, loading, and gameplay.
- `21:9` and `32:9` are wider than the 384 px canvas. They upscale the canvas
  instead of revealing more level because `game.toml` caps widening at 8
  cells per side (`ws_max_extra_cells = 8`).

### 9.2 HUD presentation

Felix draws its status bar in the bottom 40 rows of the 224-line frame
(`y = 184..223`). With `hud_stretch = 1` and `hud_rows = 40`, the compositor
splits gameplay and HUD:

- Top `224 - hud_rows` rows (the widened H32 playfield) fill the whole window.
- The bottom `hud_rows` rows are taken from the centered native 256 px band
  and scaled to the full window width.

The split only applies when the source frame is wider than 256 px.
`native` keeps the untouched single-copy path; `4:3` also uses a single copy
but scales the 320x224 (or 256x224 gameplay) source into the 299x224 logical
canvas to correct the pixel aspect. Neither mode splits the HUD.
`hud_rows = 0` or `hud_stretch = 0` disables the split.

### 9.3 Menu state and mouse adapter

The menu adapter uses three stable work-RAM bytes:

| RAM offset | Meaning |
|---|---|
| `0x0020` | `$81` on the title menu, 0 on the options/loading/gameplay screens |
| `0x5FFB` | low byte of the long at `0x5FF8`; 200 once gameplay is active, 0 on menus and while loading |
| `0x6017` | selected row, shared by the title and options screens |

`felix_is_menu_screen()` is simply `0x5FFB != 200`; it drives both the
widening gate and the menu presentation. `felix_menu_mouse_update()` recognizes
the title when `0x0020 == $81` at 320 px and the options screen when
`0x0020 == 0` at 256 px. Title rows are at `y = 146, 165`; options rows at
`y = 48, 64, 80`. It picks the nearest row within 13 px.

On a hover it emits one D-pad step at a time so the game's own wrap/clamp logic
stays in charge. A left click emits Start on the title and Right on the options
toggles; a right click emits C and acts as Back. The adapter only returns a
synthetic pad word, which `input_requested_cb` merges with keyboard/gamepad
input; it never writes guest memory. The OS pointer is shown on the menus next
to the game's glove cursor and hidden during gameplay/cutscenes.

SDL delivers mouse event coordinates in renderer-logical space once
`SDL_RenderSetLogicalSize` is active (its event watch applies the scale and
letterbox offset). `mouse_note_position()` therefore uses the event
coordinates directly; calling `SDL_RenderWindowToLogical()` as well would
scale them twice.

The earlier candidate `0xFFB3` is not a screen id: sampling showed it is a
timer that happened to take values matching the menu rows during one probe.
The final adapter uses `0x0020`/`0x5FFB`. A 20,000-frame run on the final build
sampled work RAM every 1,000 frames; `0x5FFB` is 200 from the 2,000-frame
sample through 20,000 (`build/Release/ramscan20k.txt`).

### 9.4 Configuration summary

```ini
[video]
# native | 4:3 | 16:10 | 16:9 | 21:9 | 32:9
aspect = 16:9
# 0 keeps the HUD at gameplay scale
hud_stretch = 1
# bottom rows treated as HUD (0..224)
hud_rows = 40
# 0 disables the mouse adapter entirely
mouse_menus = 1
```

Keep comments on their own line: a trailing `;` or `#` is not stripped from
string values such as `aspect`.

`GENESIS_ASPECT` overrides `aspect` for one process. Legacy
`widescreen = 1` (or `GENESIS_WIDESCREEN=1`) maps to 16:9 only when the
configured aspect is `native`. For oracle/parity runs set `mouse_menus = 0`.

## 10. Key files

| Path | Purpose |
|---|---|
| `Felix the Cat (Unl).bin` | source ROM (not distributed with the framework) |
| `felix_spec.c` | GameSpec: entry point, V-int/H-int callbacks, floor default, menu state, mouse adapter |
| `sdk/runner/main.c` | aspect sizing, gameplay/HUD/menu composition, SDL mouse events, presentation dump |
| `sdk/runner/game_spec.h` | `GameSpec.menu_mouse_update` and `is_menu_screen` callbacks |
| `sdk/runner/app_config.c/.h` | `[video]` aspect/HUD/mouse fields and aspect-ratio helpers |
| `sdk/runner/input_script.c/.h` | deterministic `MOUSE`/`CLICK` test directives |
| `sdk/runner/video/genesis_vdp.c/.h` | fixed wide canvas, widening, pillarbox |
| `game.toml` | ROM/discovery/IRQ configuration and `[widescreen]` cap |
| `build/Release/settings.ini` | runtime `[video]` selection for aspect/HUD/mouse |
| `CMakeLists.txt` | native build definition |
| `sdk/recompiler/src/game_config.c` | empty `level_modes` emitter fix |
| `sdk/external/m68k-recomp-core/profiles/genesis/code_generator.c` | `CLR -(An)` fix |
| `patches/` | applyable copies of the two framework fixes |
| `tools/long_rc.input`, `tools/long_in.input` | 5,000-frame paired run scripts |
| `tools/menu_mouse.input` | scripted title/options hover, click, toggle, and Back test |
| `tools/strict20k.input` | 20,000-frame strict-stack run script |
| `build/Release/FelixTheCatRecomp.exe` | final native build |
| `build/Release/rc_long_*.png`, `in_long_*.png` | paired gameplay screenshot evidence |
| `build/Release/rc43_long_*.png` | explicit-4:3 copies, byte-identical to `rc_long_*.png` |
| `build/Release/present/` | composed-window captures for 16:10, 16:9, 21:9, 32:9 and title |
| `build/Release/mouse/` | scripted menu-flow captures and log |
| `build/Release/rc_title_1200.png`, `in_title_1200.png` | paired title/attract capture |
| `build/Release/strict20k_aspect.err` | strict 20k log for the final build |
| `build/Release/ramscan20k.txt`, `ramscan20k.err` | final-build menu-state sampling through 20,000 frames |
| `build/Release/strict20k.err`, `strict20k.png` | earlier pre-extension strict run |
| `build/Release/rc_audio.wav`, `in_audio.wav` | final-build audio captures |
| `build/Release/floor_coverage.toml`, `dispatch_misses.toml`, `floor_unsafe.toml`, `interior_label_misses.toml` | runtime audits (0 entries) |
| `build/gen_check.log` | recompiler coverage/dispatch report |

## 11. Reproduction checklist

1. Configure and build with the commands in §4.
2. Run the native and interpreter 5,000-frame scripts non-turbo.
3. Compare the MD5s of `rc_long_*.png` and `in_long_*.png`.
4. Re-run with `aspect = 4:3` and confirm the raw captures still match; build
   the wide modes and inspect `GENESIS_PRESENT_DUMP` output for the HUD split
   and menu pillarboxing.
5. Run `tools/menu_mouse.input` with `mouse_menus = 1` (or test
   interactively) and confirm hover, click, toggle, and right-click Back.
   The script writes `f_*.png` and `f_*.bin` into the current directory.
   Keep the physical pointer still during scripted runs, because real SDL
   mouse events drive the same adapter.
6. Run the 20,000-frame strict script and confirm no `JSR stack mismatch`,
   watchdog, or crash.
7. Inspect `build/gen_check.log` (or the build's re-generation output) for
   `TOTAL 0` and `0 unresolved sites`, and confirm `floor_coverage.toml` has
   an empty `extra` list.
