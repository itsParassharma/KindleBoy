/*
 * emu.h — our wrapper around Peanut-GB.
 *
 * Holds one running Game Boy: the ROM (loaded whole into RAM), the cartridge's
 * battery-backed save RAM, and the latest 160x144 frame at 2 bits per pixel.
 * Peanut-GB hands us the picture one scanline at a time via a callback. We stash
 * each line and, by diffing it against what was there before, remember the range
 * of rows that actually changed since we last drew — the "dirty band". That's
 * the entire dirty-rectangle scheme, and row-level is all e-ink cares about
 * anyway (a refresh costs about the same whether it covers a line or a strip).
 *
 * Peanut-GB's build knobs live in peanut_impl.c: no sound (the Kindle has no
 * speaker), LCD on, high accuracy (the CPU has cycles to spare).
 */
#ifndef EMU_H
#define EMU_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* peanut_gb.h is a single-header library: struct gb_s and all function
 * prototypes are always emitted, but the implementation is guarded by
 * PEANUT_GB_HEADER_ONLY. Every TU that includes emu.h gets declarations only;
 * the sole file that instantiates the implementation is peanut_impl.c. The
 * config macros (ENABLE_SOUND/ENABLE_LCD/HIGH_LCD_ACCURACY) do not affect the
 * layout of struct gb_s, so this split is ABI-safe. */
#ifndef PEANUT_GB_HEADER_ONLY
#define PEANUT_GB_HEADER_ONLY
#endif
#include "peanut_gb.h"

#define GB_W 160
#define GB_H 144

/* Result codes for the load/save entry points. 0 == success. */
enum {
	EMU_OK              =  0,
	EMU_ERR_OPEN        = -1,  /* could not open/read the file            */
	EMU_ERR_TOO_BIG     = -2,  /* ROM larger than we allow                */
	EMU_ERR_INIT        = -3,  /* gb_init rejected the cartridge          */
	EMU_ERR_STATE_MAGIC = -4,  /* save-state header mismatch (wrong game / build) */
	EMU_ERR_NOMEM       = -5
};

typedef struct emu_s {
	struct gb_s gb;

	uint8_t *rom;              /* whole ROM file */
	size_t   rom_size;

	uint8_t *cart_ram;         /* battery-backed SRAM (may be NULL / size 0) */
	size_t   cart_ram_size;

	/* Latest rasterised frame: 2-bit DMG shade per pixel (0..3) in bits 1-0.
	 * Upper bits (palette) are masked out on capture so downstream code sees
	 * a clean 0..3. */
	uint8_t  lcd[GB_H][GB_W];

	/* Dirty rectangle since the last emu_frame_consumed(): rows in
	 * [dirty_min_y, dirty_max_y] and columns in [dirty_min_x, dirty_max_x]
	 * changed. When dirty_min_y > dirty_max_y the frame is clean. The column
	 * band lets the caller refresh a narrower slice of e-ink (dialogs, HUDs). */
	int      dirty_min_y;
	int      dirty_max_y;
	int      dirty_min_x;
	int      dirty_max_x;

	bool     sram_dirty;       /* cart RAM written since last flush */
	bool     have_frame;       /* at least one frame rasterised since load */

	char     rom_path[512];    /* absolute path to the loaded ROM */
} emu_t;

/* Load a ROM from disk, initialise Peanut-GB, allocate cart RAM, and load the
 * sibling <rom>.sav battery file if present. Returns EMU_OK or an EMU_ERR_*. */
int  emu_load(emu_t *e, const char *rom_path);

/* Flush the battery save (if dirty) and release all buffers. Safe on a
 * zero-initialised or partially-loaded emu_t. */
void emu_unload(emu_t *e);

/* Advance emulation by one frame. joypad_bits is a mask of pressed buttons
 * using the JOYPAD_* defines (bit set == pressed); this function converts to
 * Peanut-GB's active-low convention internally. Rasterisation of the frame is
 * governed by gb.direct.frame_skip (set by the caller). */
void emu_run_frame(emu_t *e, uint8_t joypad_bits);

/* Reset the dirty band to "clean" after the caller has presented it. */
void emu_frame_consumed(emu_t *e);

/* Write <rom>.sav iff sram_dirty, using temp-file + rename + fsync so a yanked
 * USB cable cannot corrupt an existing save. Clears sram_dirty on success.
 * No-op (returns EMU_OK) when there is no cart RAM or nothing is dirty. */
int  emu_sram_flush(emu_t *e);

/* Single-slot save state to/from <rom>.st. The file is a small header
 * (magic/version/sizeof(struct gb_s)/rom checksum) followed by the raw gb_s
 * struct and the cart RAM. On load, the header is validated and all Peanut-GB
 * callback/priv pointers are re-bound to this process (the saved pointer values
 * are meaningless). States are not portable across builds or between the
 * desktop and Kindle binaries; the battery .sav is the portable artifact. */
int  emu_state_save(emu_t *e);
int  emu_state_load(emu_t *e);

/* Fill title (>=17 bytes) with the cartridge's internal title (NUL-terminated). */
void emu_rom_title(emu_t *e, char title[17]);

#endif /* EMU_H */
