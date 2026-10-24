/*
 * felix_spec.c — GameSpec for the "Felix the Cat (Unl)" Genesis bootleg.
 *
 * ROM identity (verified from the dump):
 *   size   0x100000 (1 MB), CRC32 BF9BA3BC
 *   Reset PC   0x000200
 *   H-Int      0x00032E  (IRQ4, vector table offset 0x70)
 *   V-Int      0x00033C  (IRQ6, vector table offset 0x78)
 *   Shared     0x00032A  (all other vectors)
 *
 * The ROM is a bootleg with no disassembly, so function discovery is
 * heuristic. Keep the Tier-3 interpreter floor enabled: indirect object
 * handlers that static discovery misses still run and feed coverage.
 */
#include "game_spec.h"
#include "genesis_runtime.h"
#include "genesis_machine.h"
#include "input_map.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Mouse-driven game menus (presentation/input only).
 *
 * Felix uses a glove cursor in its title/options screens. Menu state found by
 * diffing work-RAM dumps across title, options, loading, and gameplay:
 *   RAM 0x0020  $81 on the title menu, 0 elsewhere
 *   RAM 0x5FFB  200 once gameplay is active, 0 on menus and while loading
 *   RAM 0x6017  selected row, shared by both screens
 * The title renders H40 (320px) and the options screen H32 (256px), so the
 * VDP width alone cannot separate the options menu from H32 gameplay.
 * No guest state is written; the adapter only synthesizes D-pad/Start/C input
 * on the frame(s) after a hover or click. */
static const int felix_title_rows[]   = { 146, 165 };
static const int felix_options_rows[] = {  48,  64,  80 };

/* Screen state used by the title/options presentation (RAM offsets):
 *   0x0020  $81 on the title menu, 0 elsewhere
 *   0x5FFB  low byte of the level/play long at 0x5FF8: 200 once gameplay
 *           has started, 0 on menus and while a level is loading
 *   0x6017  selected row on both menus
 * The title renders H40 (320px) and the options screen H32 (256px), so widths
 * alone cannot separate "options" from "gameplay". The runner's peek helpers
 * take the full 68K address. */
#define FELIX_RAM_SCREEN 0x00FF0020u
#define FELIX_RAM_PLAY   0x00FF5FFBu
#define FELIX_RAM_CURSOR 0x00FF6017u
#define FELIX_SCREEN_TITLE  0x81
#define FELIX_PLAY_ACTIVE   200

static int      s_mm_screen;
static int      s_mm_target;
static int      s_mm_cursor;
static int      s_mm_move;
static int      s_mm_cooldown;
static int      s_mm_click;
static int      s_mm_press_frames;
static uint16_t s_mm_press_pad;
static int      s_mm_debug = -1;
static unsigned long s_mm_calls = 0;

static void felix_menu_mouse_update_impl(int x, int y, int left_click, int right_click,
                                         GameMenuMouseState *out)
{
    (void)x;
    out->active = 0;
    out->pad = 0;

    int vidw = gvdp_screen_width(&g_machine.vdp);
    int mode = glue_peek8(FELIX_RAM_SCREEN);
    int playing = glue_peek8(FELIX_RAM_PLAY) == FELIX_PLAY_ACTIVE;
    const int *rows;
    int row_count;
    int screen;
    if (!playing && mode == FELIX_SCREEN_TITLE && vidw == 320) {
        rows = felix_title_rows;
        row_count = (int)(sizeof felix_title_rows / sizeof felix_title_rows[0]);
        screen = 1;
    } else if (!playing && mode == 0 && vidw == 256) {
        rows = felix_options_rows;
        row_count = (int)(sizeof felix_options_rows / sizeof felix_options_rows[0]);
        screen = 2;
    } else {
        s_mm_screen = -1;
        s_mm_move = 0;
        s_mm_click = 0;
        s_mm_press_frames = 0;
        return;
    }

    out->active = 1;

    int cursor = glue_peek8(FELIX_RAM_CURSOR);
    if (cursor < 0 || cursor >= row_count) cursor = 0;

    if (screen != s_mm_screen) {
        s_mm_screen = screen;
        s_mm_target = cursor;
        s_mm_cursor = cursor;
        s_mm_move = 0;
        s_mm_cooldown = 2;
        s_mm_click = 0;
        s_mm_press_frames = 0;
    } else if (cursor != s_mm_cursor) {
        /* The game moved its cursor. If that change was not requested by the
         * mouse, it came from the keyboard/script: adopt it instead of
         * synthesizing a correction back toward the old pointer row. */
        s_mm_cursor = cursor;
        if (!s_mm_move && s_mm_press_frames == 0) {
            s_mm_target = cursor;
            s_mm_click = 0;
        }
    }

    if (right_click) {
        out->pad = input_button_bit(GB_C);
        s_mm_press_pad = out->pad;
        s_mm_press_frames = 1;
        s_mm_cooldown = 6;
        s_mm_move = 0;
        s_mm_click = 0;
        return;
    }

    /* A pointer over a row selects that row. Clicks queue the row action.
     * Rows are only 16-19px apart, so pick the NEAREST row rather than the
     * first whose broad +-12px band contains the pointer. */
    int hit = -1, best = 13;
    for (int i = 0; i < row_count; ++i) {
        int d = y - rows[i];
        if (d < 0) d = -d;
        if (d < best) { best = d; hit = i; }
    }
    if (hit >= 0) {
        if (left_click) {
            if (hit != s_mm_target) s_mm_move = 1;
            s_mm_target = hit;
            s_mm_click = 1;
        } else if (hit != s_mm_target) {
            s_mm_target = hit;
            s_mm_move = 1;
            s_mm_click = 0;
        }
    }

    if (s_mm_press_frames > 0) {
        out->pad = s_mm_press_pad;
        --s_mm_press_frames;
        return;
    }
    if (s_mm_cooldown > 0) {
        --s_mm_cooldown;
        return;
    }
    if (s_mm_target < 0) s_mm_target = cursor;

    /* Move one row at a time so the menu's own wrap/clamp logic stays in
     * charge. The movement is only "pending" while the mouse requested it;
     * keyboard moves are adopted above and never fought. */
    if (s_mm_move) {
        if (cursor == s_mm_target) {
            s_mm_move = 0;
        } else {
            int down_steps = (s_mm_target - cursor + row_count) % row_count;
            int up_steps   = (cursor - s_mm_target + row_count) % row_count;
            uint16_t pad = input_button_bit(down_steps <= up_steps ? GB_DOWN : GB_UP);
            out->pad = pad;
            s_mm_press_pad = pad;
            s_mm_press_frames = 1;
            s_mm_cooldown = 5;
            return;
        }
    }

    if (s_mm_click) {
        s_mm_click = 0;
        uint16_t pad = 0;
        if (screen == 2 && s_mm_target < 2)
            pad = input_button_bit(GB_RIGHT);   /* music / sound toggle */
        else if (screen == 1)
            pad = input_button_bit(GB_START);   /* One Player / Options */
        if (pad) {
            out->pad = pad;
            s_mm_press_pad = pad;
            s_mm_press_frames = 1;
            s_mm_cooldown = 6;
        }
    }
}

static void felix_menu_mouse_update(int x, int y, int left_click, int right_click,
                                    GameMenuMouseState *out)
{
    if (s_mm_debug < 0) {
        const char *e = getenv("GENESIS_MM_DEBUG");
        s_mm_debug = (e && *e) ? 1 : 0;
    }
    felix_menu_mouse_update_impl(x, y, left_click, right_click, out);
    if (s_mm_debug) {
        ++s_mm_calls;
        fprintf(stderr,
                "[MM] f=%lu vidw=%d mode=%d play=%d x=%d y=%d l=%d r=%d screen=%d cursor=%d target=%d move=%d "
                "cool=%d click=%d press=%d active=%d pad=0x%04X\n",
                s_mm_calls, gvdp_screen_width(&g_machine.vdp),
                (int)glue_peek8(FELIX_RAM_SCREEN), (int)glue_peek8(FELIX_RAM_PLAY),
                x, y, left_click, right_click, s_mm_screen,
                (int)glue_peek8(FELIX_RAM_CURSOR), s_mm_target, s_mm_move,
                s_mm_cooldown, s_mm_click, s_mm_press_frames, out->active,
                (unsigned)out->pad);
    }
}

static int felix_is_menu_screen(void)
{
    /* The level/play long at 0x5FF8 reports 200 once gameplay has started.
     * Everything else (title, options, level loading) keeps the authentic
     * view: no widescreen widening and no HUD-band stretch. */
    return glue_peek8(FELIX_RAM_PLAY) != FELIX_PLAY_ACTIVE;
}

static void felix_call_entry_point(void) { recomp_call_addr(0x000200u); }
static void felix_call_vblank(void)      { recomp_call_addr(0x00033Cu); }
static void felix_call_hblank(void)      { recomp_call_addr(0x00032Eu); }

const GameSpec g_game_spec = {
    .display_name           = "Felix the Cat (Bootleg)",
    .short_name             = "FELIX",

    .menu_mouse_update      = felix_menu_mouse_update,
    .is_menu_screen         = felix_is_menu_screen,
    /* H32 gameplay draws an 8px black frame at x=0..7 and x=248..255. Wide
     * presentation trims that host-side before filling the window; the raw
     * VDP frame stays byte-identical for parity captures. */
    .present_crop_x         = 8,

    .expected_rom_crc32     = 0xBF9BA3BCu,
    .expected_rom_size      = 0x100000u,

    .tier3_floor_default    = 1,

    .call_entry_point       = felix_call_entry_point,
    .call_vblank            = felix_call_vblank,
    .call_hblank            = felix_call_hblank,

    .resume_main_loop_pc    = 0u,
    .save_resume_pc         = NULL,
    .dispatch_main_loop_pc  = 0u,
    .call_periodic          = NULL,

    .on_post_reset          = NULL,
    .on_frame_pre           = NULL,
    .on_frame_post          = NULL,
    .on_hblank              = NULL,

    .handle_arg             = NULL,
    .arg_usage              = NULL,
    .dispatch_override      = NULL,

    .fill_frame_record      = NULL,
    .frame_record_version   = 0u,

    .commands               = NULL,
    .command_count          = 0,
};
