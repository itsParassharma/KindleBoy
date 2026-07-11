/*
 * peanut_impl.c — the single file that actually compiles Peanut-GB's guts.
 *
 * Peanut-GB is a header-only library. Everyone else includes emu.h, which pulls
 * in peanut_gb.h in "declarations only" mode. Here — and only here — we let the
 * real function bodies through, so we don't end up with duplicate symbols at
 * link time. The three knobs below happen to match Peanut-GB's defaults; we set
 * them anyway so it's obvious what we're building and so an upstream default
 * change can't surprise us.
 */
#define ENABLE_SOUND 0                  /* the Kindle has nothing to play sound with */
#define ENABLE_LCD   1
#define PEANUT_GB_HIGH_LCD_ACCURACY 1   /* the CPU can easily afford it */

#include "peanut_gb.h"
