/*
 * input_evdev.c — reading the touchscreen on the Kindle.
 *
 * We hunt through /dev/input/event* for the one that reports multitouch, then
 * follow its slot-based event stream to track where each finger is. The raw
 * coordinates get corrected (swapped/mirrored as needed) using what FBInk told
 * us about this device. We keep all the fingers, not just one, so the on-screen
 * controls can register a D-pad direction and A at the same time.
 *
 * One device quirk to watch for (the Kindle Basic does this): it doesn't send
 * the usual "finger lifted" event, so we treat a BTN_TOUCH of 0 as "everyone
 * let go".
 */
#include "../platform.h"
#include "kindle_shared.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#ifndef ABS_MT_SLOT
#define ABS_MT_SLOT 0x2f
#endif

#ifndef EVIOCGRAB
#define EVIOCGRAB _IOW('E', 0x90, int)
#endif

#define MAX_SLOTS PLAT_MAX_TOUCHES

/* Set by main.c's SIGTERM/SIGINT handler; a clean-exit request. */
extern volatile sig_atomic_t g_should_quit;

static int  s_fd = -1;
static int  s_cur_slot;
static struct { int x, y; bool active; } s_slot[MAX_SLOTS];

static int  s_absx_min, s_absx_max, s_absy_min, s_absy_max;

static int test_bit(const unsigned long *arr, int bit)
{
	return (arr[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1ul;
}

static bool dev_has_mt(int fd)
{
	unsigned long bits[(ABS_MAX + 1) / (8 * sizeof(long)) + 1];
	memset(bits, 0, sizeof bits);
	if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof bits), bits) < 0) return false;
	return test_bit(bits, ABS_MT_POSITION_X);
}

static void read_ranges(int fd)
{
	struct input_absinfo ai;
	if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0) { s_absx_min = ai.minimum; s_absx_max = ai.maximum; }
	if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0) { s_absy_min = ai.minimum; s_absy_max = ai.maximum; }
	if (s_absx_max <= s_absx_min) s_absx_max = s_absx_min + 1;
	if (s_absy_max <= s_absy_min) s_absy_max = s_absy_min + 1;
}

/* Called from plat_init (display) order guarantees FBInk state is ready. */
void kindle_input_open(void)
{
	char path[32];
	for (int i = 0; i < 12; i++) {
		snprintf(path, sizeof path, "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		if (dev_has_mt(fd)) {
			s_fd = fd;
			read_ranges(fd);
			/* Grab the touchscreen exclusively. Without this every tap
			 * ALSO reaches the Kindle UI underneath, which merrily opens
			 * the library / home screen and repaints over the game. */
			int grabbed = ioctl(fd, EVIOCGRAB, 1);
			plat_log("input: touch on %s range x[%d,%d] y[%d,%d] grab=%s",
				 path, s_absx_min, s_absx_max, s_absy_min, s_absy_max,
				 grabbed == 0 ? "ok" : "FAILED");
			break;
		}
		close(fd);
	}
	if (s_fd < 0) plat_log("input: no MT touch device found");
	for (int i = 0; i < MAX_SLOTS; i++) s_slot[i].active = false;
}

/* Map a raw panel coordinate pair to canvas coordinates. */
static void transform(int rx, int ry, int *ox, int *oy)
{
	const kindle_disp_t *d = kindle_disp();
	int w = d ? d->w : 600, h = d ? d->h : 800;

	int xmin = s_absx_min, xmax = s_absx_max, ymin = s_absy_min, ymax = s_absy_max;
	int x = rx, y = ry;

	if (d && d->touch_swap_axes) {
		int t = x; x = y; y = t;
		int a = xmin; xmin = ymin; ymin = a;
		int b = xmax; xmax = ymax; ymax = b;
	}
	if (d && d->touch_mirror_x) x = xmax - (x - xmin) + xmin;
	if (d && d->touch_mirror_y) y = ymax - (y - ymin) + ymin;

	int cx = (int)((long)(x - xmin) * w / (xmax - xmin));
	int cy = (int)((long)(y - ymin) * h / (ymax - ymin));
	if (cx < 0) cx = 0;
	if (cx >= w) cx = w - 1;
	if (cy < 0) cy = 0;
	if (cy >= h) cy = h - 1;
	*ox = cx; *oy = cy;
}

void plat_input_poll(plat_input_t *out)
{
	memset(out, 0, sizeof *out);

	if (s_fd >= 0) {
		struct input_event ev;
		ssize_t n;
		while ((n = read(s_fd, &ev, sizeof ev)) == (ssize_t)sizeof ev) {
			if (ev.type == EV_ABS) {
				switch (ev.code) {
				case ABS_MT_SLOT:
					s_cur_slot = ev.value;
					break;
				case ABS_MT_TRACKING_ID:
					if (s_cur_slot >= 0 && s_cur_slot < MAX_SLOTS)
						s_slot[s_cur_slot].active = (ev.value >= 0);
					break;
				case ABS_MT_POSITION_X:
					if (s_cur_slot >= 0 && s_cur_slot < MAX_SLOTS)
						s_slot[s_cur_slot].x = ev.value;
					break;
				case ABS_MT_POSITION_Y:
					if (s_cur_slot >= 0 && s_cur_slot < MAX_SLOTS)
						s_slot[s_cur_slot].y = ev.value;
					break;
				}
			} else if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 0) {
				/* Snow-protocol: lift = all contacts up. */
				for (int i = 0; i < MAX_SLOTS; i++) s_slot[i].active = false;
			}
		}
	}

	int count = 0;
	for (int i = 0; i < MAX_SLOTS && count < PLAT_MAX_TOUCHES; i++) {
		if (!s_slot[i].active) continue;
		int cx, cy;
		transform(s_slot[i].x, s_slot[i].y, &cx, &cy);
		out->pts[count].x = cx;
		out->pts[count].y = cy;
		out->pts[count].id = i;
		count++;
	}
	out->count = count;
	out->quit_requested = g_should_quit ? true : false;
}

void plat_input_wait(int timeout_ms)
{
	if (s_fd >= 0) {
		struct pollfd pfd = { .fd = s_fd, .events = POLLIN };
		poll(&pfd, 1, timeout_ms);   /* returns early on touch, else on timeout */
	} else {
		struct timespec ts = { timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000L };
		nanosleep(&ts, NULL);
	}
}

void kindle_input_close(void)
{
	if (s_fd >= 0) {
		ioctl(s_fd, EVIOCGRAB, 0);   /* hand the touchscreen back to the UI */
		close(s_fd);
		s_fd = -1;
	}
}
