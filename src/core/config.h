/*
 * config.h — remembering the handful of settings the player can change.
 *
 * It's a plain key=value file that lives next to the extension (on the Kindle,
 * /mnt/us/extensions/kindleboy/kindleboy.cfg). Anything we don't recognise is
 * ignored, and anything missing just keeps its default — so it's hard to break.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

typedef struct {
	bool quality_mode;      /* false = A2 dithered (fast), true = GL16 4-gray */
	int  autosave_sec;      /* battery-save flush cadence during play */
	char rom_dir[256];      /* directory the browser scans for *.gb */
	char last_rom[256];     /* absolute path of the most recently played ROM */
	int  scale_override;    /* 0 = auto integer scale, >0 = force this scale */
} config_t;

/* Populate *c with defaults, then overlay any values found in the file. */
void config_defaults(config_t *c);
int  config_load(config_t *c, const char *path);   /* returns 0 if file read */
int  config_save(const config_t *c, const char *path);

#endif /* CONFIG_H */
