/*
 * overlay.c — where the on-screen buttons go, how they're drawn, and figuring
 * out which one a finger landed on.
 */
#include "overlay.h"
#include "ui.h"
#include "emu.h"      /* JOYPAD_* */
#include <string.h>

static void add_zone(overlay_t *o, int x, int y, int w, int h,
		     uint8_t bit, bool is_menu, const char *label)
{
	if (o->count >= (int)(sizeof o->zones / sizeof o->zones[0])) return;
	touch_zone_t *z = &o->zones[o->count++];
	z->x = x; z->y = y; z->w = w; z->h = h;
	z->bit = bit; z->is_menu = is_menu; z->label = label;
}

void overlay_layout(overlay_t *o, const render_cfg_t *rc, int fb_w, int fb_h)
{
	memset(o, 0, sizeof *o);

	const int pad = 12;
	int y0 = rc->dst_y + rc->game_h + pad;
	if (y0 > fb_h) y0 = fb_h;
	o->area_y = y0;
	int avail_h = fb_h - y0 - pad;
	if (avail_h < 60) avail_h = 60;

	/* D-pad: a square on the left, as large as the area allows but no wider
	 * than ~45% of the screen. */
	int dside = avail_h;
	int dmax  = (fb_w * 45) / 100;
	if (dside > dmax) dside = dmax;
	int dx = pad;
	int dy = y0 + (avail_h - dside) / 2;
	int c = dside / 3;   /* cell size */

	/* 3x3 grid: corners are diagonals, edges are cardinal, centre neutral. */
	add_zone(o, dx,         dy,         c, c, JOYPAD_UP|JOYPAD_LEFT,  false, "");
	add_zone(o, dx + c,     dy,         c, c, JOYPAD_UP,              false, "^");
	add_zone(o, dx + 2*c,   dy,         c, c, JOYPAD_UP|JOYPAD_RIGHT, false, "");
	add_zone(o, dx,         dy + c,     c, c, JOYPAD_LEFT,            false, "<");
	/* centre (dx+c, dy+c) intentionally has no zone */
	add_zone(o, dx + 2*c,   dy + c,     c, c, JOYPAD_RIGHT,           false, ">");
	add_zone(o, dx,         dy + 2*c,   c, c, JOYPAD_DOWN|JOYPAD_LEFT, false, "");
	add_zone(o, dx + c,     dy + 2*c,   c, c, JOYPAD_DOWN,            false, "v");
	add_zone(o, dx + 2*c,   dy + 2*c,   c, c, JOYPAD_DOWN|JOYPAD_RIGHT,false, "");

	/* A/B on the right, arranged diagonally like a Game Boy (B lower-left,
	 * A upper-right). */
	int bsize = dside / 3;
	int bx_right = fb_w - pad - bsize;
	int b_bx = bx_right - bsize - pad;          /* B is left of A */
	int a_y  = dy + (dside - bsize) / 2 - bsize / 2;
	int b_y  = dy + (dside - bsize) / 2 + bsize / 2;
	if (a_y < y0) a_y = y0;
	add_zone(o, bx_right, a_y, bsize, bsize, JOYPAD_A, false, "A");
	add_zone(o, b_bx,     b_y, bsize, bsize, JOYPAD_B, false, "B");

	/* Centre column: Start, Select, Menu stacked. */
	int cw_col = (b_bx - (dx + dside)) - 2 * pad;
	if (cw_col < 60) cw_col = 60;
	int cx = dx + dside + pad;
	int bh = (avail_h - 2 * pad) / 3;
	if (bh > 46) bh = 46;
	int cy = y0 + (avail_h - (bh * 3 + 2 * pad)) / 2;
	if (cy < y0) cy = y0;
	add_zone(o, cx, cy,               cw_col, bh, JOYPAD_START,  false, "START");
	add_zone(o, cx, cy + bh + pad,    cw_col, bh, JOYPAD_SELECT, false, "SELECT");
	add_zone(o, cx, cy + 2*(bh+pad),  cw_col, bh, 0,             true,  "MENU");
}

void overlay_draw(const overlay_t *o, uint8_t *canvas, int cw, int ch)
{
	/* Clear the overlay strip to white, then draw dark outlines + labels. */
	ui_fill(canvas, cw, ch, 0, o->area_y, cw, ch - o->area_y, 0xFF);

	for (int i = 0; i < o->count; i++) {
		const touch_zone_t *z = &o->zones[i];
		ui_rect(canvas, cw, ch, z->x, z->y, z->w, z->h, 0x00);
		if (z->label && z->label[0]) {
			int px = (z->w >= 120) ? 2 : 3;
			int tw = ui_text_width(z->label, px);
			int th = 8 * px;
			ui_text(canvas, cw, ch,
				z->x + (z->w - tw) / 2,
				z->y + (z->h - th) / 2,
				z->label, px, 0x00);
		}
	}
}

uint8_t overlay_map(const overlay_t *o, const plat_input_t *in, bool *menu_tapped)
{
	uint8_t bits = in->direct_buttons;
	if (menu_tapped) *menu_tapped = false;

	for (int t = 0; t < in->count; t++) {
		int px = in->pts[t].x, py = in->pts[t].y;
		for (int i = 0; i < o->count; i++) {
			const touch_zone_t *z = &o->zones[i];
			if (px >= z->x && px < z->x + z->w &&
			    py >= z->y && py < z->y + z->h) {
				if (z->is_menu) { if (menu_tapped) *menu_tapped = true; }
				else bits |= z->bit;
			}
		}
	}
	return bits;
}
