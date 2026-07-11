/*
 * peanut_impl.c — the single file that actually compiles the emulator core.
 *
 * The core (Walnut-CGB, a CGB-capable rewrite of Peanut-GB) is header-only.
 * Everyone else includes emu.h, which pulls in the header in "declarations
 * only" mode. Here — and only here — we let the real function bodies through,
 * so we don't end up with duplicate symbols at link time. The knobs below
 * match the core's defaults; we set them anyway so it's obvious what we build
 * and so an upstream default change can't surprise us.
 *
 * (The filename stays peanut_impl.c out of habit / to avoid churn; Walnut-CGB
 * is a direct descendant of Peanut-GB and keeps the same API.)
 */
#define ENABLE_SOUND 0                  /* the Kindle has nothing to play sound with */
#define ENABLE_LCD   1
#define WALNUT_GB_HIGH_LCD_ACCURACY 1   /* the CPU can easily afford it */

#include "walnut_cgb.h"
