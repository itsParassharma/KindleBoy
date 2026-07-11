/*
 * app.c — the state machine, the main loop, and the tricky part: deciding when
 * to push pixels to a screen that's much slower than the game running behind it.
 *
 * The whole e-ink dance, in plain terms:
 *   - The Game Boy always runs at its real 60 Hz (we keep time with a running
 *     accumulator). We only bother actually drawing the picture every other
 *     frame — the player can't tell, and it halves the work.
 *   - We only *send* the picture to the screen about 8 times a second (fast A2
 *     mode), or ~2 in QUALITY mode. plat_present() waits for the previous
 *     refresh to finish before it touches the buffer, so nothing tears — and
 *     because we already waited out the frame interval, that wait is usually
 *     instant.
 *   - If nothing on screen changed, we don't refresh at all. And we only bother
 *     redrawing the strip of rows that did change if it's small.
 *   - Cleaning up e-ink ghosting is done by feel, not on a timer: when the
 *     screen goes still we quietly redraw it as a crisp 4-gray image (which also
 *     makes text much nicer to read), and we only do the jarring full-screen
 *     flash when ghosting has really built up, or on a transition like opening
 *     the menu.
 */
#include "app.h"
#include "emu.h"
#include "render.h"
#include "ui.h"
#include "overlay.h"
#include "browser.h"
#include "menu.h"
#include "../platform/platform.h"

#include <string.h>
#include <stdio.h>

/* ---- tuning constants ---------------------------------------------------- */
#define GB_FRAME_US        16742      /* 59.73 Hz */
#define PRESENT_FAST_US    125000     /* ~8 fps A2 */
#define PRESENT_QUALITY_US 450000     /* ~2.2 fps GL16 */
#define QUIET_US           700000     /* still-scene threshold */
#define A2_SOFT            120         /* A2 presents before a GL16 promo flashes */
#define DEGHOST_FLASH_US   20000000ull /* full cleanup flash cadence during play */
#define QUALITY_FLASH_EVERY 40         /* GL16 presents between GC16 flashes */
#define BAND_SMALL_ROWS    48          /* <= this many GB rows => partial refresh */
#define RESYNC_FRAMES      4           /* accumulator catch-up clamp */
#define FF_EXTRA_FRAMES    2           /* fast-forward = 1 + this per tick (3x) */

typedef enum { APP_BROWSER, APP_PLAYING, APP_MENU, APP_EXIT } app_state_t;

/* ---- app context (single instance) --------------------------------------- */
static plat_info_t   info;
static uint8_t      *canvas;
static int           fbw, fbh;

static config_t     *cfg;
static const char   *cfg_path;

static emu_t         emu;
static bool          emu_loaded;
static render_cfg_t  rcfg;
static overlay_t     overlay;
static browser_t     browser;
static menu_t        menu;

static app_state_t   state;

/* present scheduler bookkeeping */
static uint64_t last_present_us;
static uint64_t last_activity_us;
static uint64_t last_autosave_us;
static uint64_t last_flash_us;       /* last full cleanup flash */
static int      a2_count;
static int      quality_present_count;
static bool     promoted;
static bool     du_after_promo;      /* purge promo grays with one DU sweep */

/* fast-forward + quick-save state */
static bool     ff_on;
static bool     prev_save_touch, prev_ff_touch;
static uint64_t save_feedback_until;

/* ---- helpers ------------------------------------------------------------- */

static void present_full(plat_refresh_t mode)
{
	plat_present(0, 0, fbw, fbh, mode);
}

/* First paint after entering the browser must be a full GC16 flash: on e-ink a
 * non-flashing refresh over whatever was on screen before (the Kindle home
 * screen, a game) leaves ghosted remnants and half-visible buttons. Selection
 * changes afterwards use the gentler non-flashing refresh. */
static bool browser_needs_flash = true;

static void show_browser(void)
{
	browser_draw(&browser, canvas, fbw, fbh);
	present_full(browser_needs_flash ? REFRESH_FLASH : REFRESH_QUALITY);
	browser_needs_flash = false;
	plat_wait_refresh();
}

#if !defined(_WIN32)
#include <sys/stat.h>
static void ensure_dir(const char *p) { mkdir(p, 0777); }
#else
static void ensure_dir(const char *p) { (void)p; }
#endif

/* Find a folder that actually has ROMs in it. We look in the configured dir
 * first, then a few obvious spots — including the extension's own folder, since
 * that's a tempting place to drop a .gb (and where at least one person did).
 * If nothing turns up, we leave the list empty pointing at the configured dir,
 * so the on-screen "no ROMs" message names a real path to use. */
static void rescan_roms(void)
{
	if (browser_scan(&browser, cfg->rom_dir) > 0) return;
	static const char *cands[] = {
		"/mnt/us/roms/gb", "/mnt/us/roms",
		"/mnt/us/extensions/kindleboy", "/mnt/us/extensions/kindleboy/roms",
	};
	for (unsigned i = 0; i < sizeof cands / sizeof cands[0]; i++)
		if (browser_scan(&browser, cands[i]) > 0) return;
	browser_scan(&browser, cfg->rom_dir);
}

static void show_menu(void)
{
	menu_draw(&menu, canvas, fbw, fbh, cfg->quality_mode);
	present_full(REFRESH_FLASH);   /* flash doubles as a deghost */
	plat_wait_refresh();
}

/* Repaint the whole play screen (game rect + control overlay) and flash. Used
 * on entering gameplay and on resuming from the menu. */
static void repaint_play(void)
{
	int rx, ry, rw, rh;
	ui_fill(canvas, fbw, fbh, 0, 0, fbw, fbh, 0xFF);
	render_game(&emu, &rcfg, canvas, fbw, 0, GB_H - 1, cfg->quality_mode,
		    &rx, &ry, &rw, &rh);
	overlay_draw(&overlay, canvas, fbw, fbh, ff_on);
	present_full(REFRESH_FLASH);
	plat_wait_refresh();

	last_present_us = plat_now_us();
	last_activity_us = last_present_us;
	last_flash_us = last_present_us;
	a2_count = 0;
	quality_present_count = 0;
	promoted = false;
	du_after_promo = false;
	emu_frame_consumed(&emu);
}

static bool enter_playing(int idx)
{
	char path[600];
	browser_path(&browser, idx, path, sizeof path);

	if (emu_loaded) { emu_unload(&emu); emu_loaded = false; }

	int r = emu_load(&emu, path);
	if (r != EMU_OK) {
		plat_log("app: emu_load(%s) failed %d", path, r);
		ui_fill(canvas, fbw, fbh, 0, 0, fbw, fbh, 0xFF);
		ui_text(canvas, fbw, fbh, 16, fbh / 2, "Failed to load ROM", 3, 0x00);
		present_full(REFRESH_QUALITY);
		plat_wait_refresh();
		plat_sleep_us(1500000);
		return false;
	}
	emu_loaded = true;
	emu.gb.direct.frame_skip = true;   /* rasterise at 30 Hz, emulate at 60 Hz */

	/* Fresh game, fresh control state. */
	ff_on = false;
	prev_save_touch = prev_ff_touch = false;
	save_feedback_until = 0;

	/* Remember for next launch. */
	strncpy(cfg->last_rom, path, sizeof cfg->last_rom - 1);
	config_save(cfg, cfg_path);

	repaint_play();
	state = APP_PLAYING;
	return true;
}

/* The per-frame present decision during gameplay. */
static void play_present(uint64_t now)
{
	bool quality = cfg->quality_mode;
	bool dirty = emu.dirty_min_y <= emu.dirty_max_y;
	uint64_t min_interval = quality ? PRESENT_QUALITY_US : PRESENT_FAST_US;
	int rx, ry, rw, rh;

	if (dirty) {
		last_activity_us = now;
		promoted = false;
		if (now - last_present_us < min_interval)
			return;

		/* Periodic full cleanup: a GC16 flash of the game area PLUS the
		 * strip above it (where the Kindle clock likes to paint), so A2
		 * residue and UI droppings never build up for long. */
		if (!quality && now - last_flash_us >= DEGHOST_FLASH_US) {
			render_game(&emu, &rcfg, canvas, fbw, 0, GB_H - 1, false, &rx, &ry, &rw, &rh);
			plat_present(0, 0, fbw, rcfg.dst_y + rcfg.game_h, REFRESH_FLASH);
			a2_count = 0;
			last_flash_us = now;
			du_after_promo = false;
			emu_frame_consumed(&emu);
			last_present_us = now;
			return;
		}

		/* Action resumed after a 4-gray still: A2 can't erase gray pixels,
		 * so run one DU sweep of the whole game rect to purge them first. */
		if (!quality && du_after_promo) {
			render_game(&emu, &rcfg, canvas, fbw, 0, GB_H - 1, false, &rx, &ry, &rw, &rh);
			plat_present(rcfg.dst_x, rcfg.dst_y, rcfg.game_w, rcfg.game_h, REFRESH_BW);
			du_after_promo = false;
			emu_frame_consumed(&emu);
			last_present_us = now;
			return;
		}

		int y0 = emu.dirty_min_y, y1 = emu.dirty_max_y;
		int band = y1 - y0 + 1;
		if (band <= BAND_SMALL_ROWS)
			render_game(&emu, &rcfg, canvas, fbw, y0, y1, quality, &rx, &ry, &rw, &rh);
		else
			render_game(&emu, &rcfg, canvas, fbw, 0, GB_H - 1, quality, &rx, &ry, &rw, &rh);

		plat_present(rx, ry, rw, rh, quality ? REFRESH_QUALITY : REFRESH_FAST);
		emu_frame_consumed(&emu);
		last_present_us = now;

		if (quality) {
			if (++quality_present_count >= QUALITY_FLASH_EVERY) {
				quality_present_count = 0;
				plat_present(rcfg.dst_x, rcfg.dst_y, rcfg.game_w, rcfg.game_h, REFRESH_FLASH);
			}
		} else {
			a2_count++;
		}
	} else {
		/* Scene is quiescent: promote the FAST frame to a 4-gray GL16 still
		 * once, which both deghosts and sharpens static text. */
		if (!quality && !promoted && last_present_us &&
		    now - last_activity_us >= QUIET_US) {
			render_game(&emu, &rcfg, canvas, fbw, 0, GB_H - 1, true, &rx, &ry, &rw, &rh);
			if (a2_count >= A2_SOFT) {
				plat_present(rcfg.dst_x, rcfg.dst_y, rcfg.game_w, rcfg.game_h, REFRESH_FLASH);
				a2_count = 0;
				last_flash_us = now;
			} else {
				plat_present(rcfg.dst_x, rcfg.dst_y, rcfg.game_w, rcfg.game_h, REFRESH_QUALITY);
				a2_count /= 2;
			}
			promoted = true;
			du_after_promo = true;   /* purge the grays when action resumes */
		}
	}
}

static void handle_menu_action(menu_action_t act)
{
	switch (act) {
	case MENU_RESUME:
		state = APP_PLAYING;
		repaint_play();
		break;
	case MENU_SAVE_STATE:
		emu_state_save(&emu);
		show_menu();      /* stay in menu, flash confirms */
		break;
	case MENU_LOAD_STATE:
		if (emu_state_load(&emu) == EMU_OK) {
			state = APP_PLAYING;
			repaint_play();
		} else {
			show_menu();
		}
		break;
	case MENU_TOGGLE_QUALITY:
		cfg->quality_mode = !cfg->quality_mode;
		config_save(cfg, cfg_path);
		show_menu();
		break;
	case MENU_DEGHOST:
		state = APP_PLAYING;
		repaint_play();
		break;
	case MENU_QUIT_TO_BROWSER:
		emu_sram_flush(&emu);
		emu_unload(&emu);
		emu_loaded = false;
		rescan_roms();                          /* refresh list */
		browser.touch_prev = true;              /* finger from this tap is still down */
		browser_needs_flash = true;             /* clear the game off the panel */
		state = APP_BROWSER;
		show_browser();
		break;
	case MENU_QUIT:
		state = APP_EXIT;
		break;
	default:
		break;
	}
}

/* ---- main loop ----------------------------------------------------------- */

int app_run(config_t *cfg_in, const char *cfg_path_in, const char *autostart_rom)
{
	cfg = cfg_in;
	cfg_path = cfg_path_in;

	if (plat_init(&info) != 0) {
		plat_log("app: plat_init failed");
		return 1;
	}
	canvas = info.canvas;
	fbw = info.fb_w;
	fbh = info.fb_h;

	int min_overlay_h = fbh / 3;
	render_layout(&rcfg, fbw, fbh, min_overlay_h);
	if (cfg->scale_override > 0) {
		int s = cfg->scale_override;
		/* Reject an override that wouldn't fit — otherwise render_game would
		 * write past the canvas. Fall back to the auto layout. */
		if (s * GB_W <= fbw && rcfg.dst_y + s * GB_H <= fbh) {
			rcfg.scale = s;
			rcfg.game_w = s * GB_W;
			rcfg.game_h = s * GB_H;
			rcfg.dst_x = (fbw - rcfg.game_w) / 2;
			if (rcfg.dst_x < 0) rcfg.dst_x = 0;
		} else {
			plat_log("app: scale_override %d does not fit; using auto", s);
		}
	}
	overlay_layout(&overlay, &rcfg, fbw, fbh);

	ensure_dir("/mnt/us/roms");
	ensure_dir("/mnt/us/roms/gb");
	rescan_roms();
	menu_reset(&menu);

	if (autostart_rom && autostart_rom[0]) {
		/* Boot straight into a ROM by path (desktop dev convenience). The
		 * browser stays scanned on cfg->rom_dir so "quit to list" works. */
		if (emu_loaded) emu_unload(&emu);
		int r = emu_load(&emu, autostart_rom);
		if (r == EMU_OK) {
			emu_loaded = true;
			emu.gb.direct.frame_skip = true;
			strncpy(cfg->last_rom, autostart_rom, sizeof cfg->last_rom - 1);
			repaint_play();
			state = APP_PLAYING;
		} else {
			plat_log("app: autostart load failed %d", r);
			state = APP_BROWSER;
			show_browser();
		}
	} else {
		state = APP_BROWSER;
		show_browser();
	}

	uint64_t next_tick = plat_now_us();
	last_autosave_us = next_tick;

	while (state != APP_EXIT) {
		plat_input_t in;
		plat_input_poll(&in);
		if (in.quit_requested) break;

		if (state == APP_BROWSER) {
			bool changed = false;
			int idx = browser_input(&browser, &in, fbw, fbh, &changed);
			if (idx == BROWSER_QUIT) {
				state = APP_EXIT;
			} else if (idx >= 0) {
				if (!enter_playing(idx)) {
					browser_needs_flash = true;   /* clear the error screen */
					show_browser();
				}
			} else if (changed) {
				show_browser();
			}
			plat_sleep_us(30000);
			continue;
		}

		if (state == APP_MENU) {
			bool changed = false;
			menu_action_t act = menu_input(&menu, &in, fbw, fbh, &changed);
			if (act != MENU_NONE) handle_menu_action(act);
			else if (changed) { menu_draw(&menu, canvas, fbw, fbh, cfg->quality_mode);
					    present_full(REFRESH_QUALITY); }
			plat_sleep_us(30000);
			continue;
		}

		/* APP_PLAYING */
		ov_specials_t sp;
		uint8_t joypad = overlay_map(&overlay, &in, &sp);
		if (in.key_menu) sp.menu = true;
		if (sp.menu) {
			emu_sram_flush(&emu);       /* good moment: stall is hidden by the flash */
			menu_reset(&menu);
			menu.touch_prev = true;     /* finger from this tap is still down */
			state = APP_MENU;
			show_menu();
			next_tick = plat_now_us();
			continue;
		}

		/* Quick save: one tap on SAVE writes the save state; the button
		 * inverts briefly so you know it took. */
		if (sp.save && !prev_save_touch) {
			emu_sram_flush(&emu);
			emu_state_save(&emu);
			int bx, by, bw2, bh2;
			overlay_draw_special(&overlay, canvas, fbw, fbh, OV_SAVE, true,
					     &bx, &by, &bw2, &bh2);
			plat_present(bx, by, bw2, bh2, REFRESH_BW);
			save_feedback_until = plat_now_us() + 700000;
		}
		if (save_feedback_until && plat_now_us() >= save_feedback_until) {
			int bx, by, bw2, bh2;
			overlay_draw_special(&overlay, canvas, fbw, fbh, OV_SAVE, false,
					     &bx, &by, &bw2, &bh2);
			plat_present(bx, by, bw2, bh2, REFRESH_BW);
			save_feedback_until = 0;
		}

		/* Fast-forward: one tap toggles 3x speed; the button stays
		 * inverted while it's on. */
		if (sp.ff && !prev_ff_touch) {
			ff_on = !ff_on;
			int bx, by, bw2, bh2;
			overlay_draw_special(&overlay, canvas, fbw, fbh, OV_FF, ff_on,
					     &bx, &by, &bw2, &bh2);
			plat_present(bx, by, bw2, bh2, REFRESH_BW);
		}
		prev_save_touch = sp.save;
		prev_ff_touch   = sp.ff;

		emu_run_frame(&emu, joypad);
		if (ff_on)
			for (int ff = 0; ff < FF_EXTRA_FRAMES; ff++)
				emu_run_frame(&emu, joypad);

		uint64_t now = plat_now_us();
		play_present(now);

		if (cfg->autosave_sec > 0 &&
		    now - last_autosave_us >= (uint64_t)cfg->autosave_sec * 1000000ull) {
			emu_sram_flush(&emu);
			last_autosave_us = now;
		}

		/* Fixed-timestep pacing with catch-up clamp. */
		next_tick += GB_FRAME_US;
		now = plat_now_us();
		if (now < next_tick)
			plat_sleep_us(next_tick - now);
		else if (now - next_tick > (uint64_t)RESYNC_FRAMES * GB_FRAME_US)
			next_tick = now;
	}

	if (emu_loaded) {
		emu_sram_flush(&emu);
		emu_unload(&emu);
	}
	config_save(cfg, cfg_path);
	plat_shutdown();
	return 0;
}
