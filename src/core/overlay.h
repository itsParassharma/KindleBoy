/*
 * overlay.h — the buttons we draw on the touchscreen so you can actually play.
 *
 * The game sits at the top; the controls fill the space below it. On the left
 * is a 3x3 D-pad (the corners press two directions at once, so diagonals just
 * work). On the right are A and B, laid out like a real Game Boy. Down the
 * middle: Start, Select, and Menu. We draw the button outlines once when a game
 * starts and never touch them again — only the game area refreshes while you
 * play — so the controls are effectively free.
 *
 * overlay_map combines the buttons under every finger that's down, so holding
 * Right and tapping A at the same time does what you'd expect.
 */
#ifndef OVERLAY_H
#define OVERLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "render.h"
#include "../platform/platform.h"

typedef struct {
	int x, y, w, h;
	uint8_t bit;        /* OR of JOYPAD_* bits, 0 for the menu button */
	bool    is_menu;
	const char *label;
} touch_zone_t;

typedef struct {
	touch_zone_t zones[16];
	int count;
	int area_y;         /* top of the overlay area (below the game rect) */
} overlay_t;

/* Compute zone rectangles from the game layout and framebuffer size. */
void    overlay_layout(overlay_t *o, const render_cfg_t *rc, int fb_w, int fb_h);

/* Draw the static control outlines and labels into the canvas. */
void    overlay_draw(const overlay_t *o, uint8_t *canvas, int cw, int ch);

/* Map current contacts to a JOYPAD_* bitmask (bit set == pressed). Sets
 * *menu_tapped if any contact fell in the menu button. Also folds in
 * in->direct_buttons (desktop keyboard path). */
uint8_t overlay_map(const overlay_t *o, const plat_input_t *in, bool *menu_tapped);

#endif /* OVERLAY_H */
