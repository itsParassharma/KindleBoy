# KindleBoy

A Game Boy emulator for a jailbroken Kindle. It plays real `.gb` games on the
e-ink screen, lets you pick a game from a list right on the device, and draws
touch buttons on screen so you can actually play. It saves your battery saves
and has save states too.

Built for the **Kindle Basic, 10th/11th gen** (the cheap touch ones) running
KUAL. There's also a desktop build so you can develop and test it without
touching the Kindle.

A few things worth knowing up front:

- The emulator itself is [Peanut-GB](https://github.com/deltabeard/Peanut-GB), a
  tiny, rock-solid Game Boy core that people already run on microcontrollers.
  Its output is 4 shades of gray, which is exactly what e-ink wants.
- The screen is driven through [FBInk](https://github.com/NiLuJe/FBInk), which
  hides all the messy per-model differences in how Kindle e-ink panels refresh.
- E-ink is slow, so the trick is: run the emulator at full 60fps internally, but
  only *push pixels to the screen* about 8 times a second, using the fast "A2"
  refresh mode with a dither so motion stays crisp. When you stop and a screen
  sits still (a dialog box, a menu), it quietly upgrades to a clean 4-gray image.

This is GPL-3 (because FBInk is). No game ROMs are included. Bring your own.
See `LICENSE` and `NOTICE.md`.

<p align="center">
  <img src="docs/screenshots/browser.png" width="30%" alt="ROM picker">
  <img src="docs/screenshots/gameplay.png" width="30%" alt="in-game with on-screen controls">
  <img src="docs/screenshots/menu.png" width="30%" alt="pause menu">
</p>

<p align="center"><sub>Shown with <a href="https://github.com/mattcurrie/dmg-acid2">dmg-acid2</a>, an open Game Boy PPU test ROM — no copyrighted game ROMs ship in this repo, but any <code>.gb</code> you own runs the same way.</sub></p>

---

## Does it run on my Kindle?

**10th gen, firmware 5.17.1: yes.** The binary is built fully static, so it
doesn't care that 5.17.1 uses a hard-float userland: everything it needs is
baked into the one file, and it talks to the kernel the same way regardless.
That's the whole reason we build it static.

If for some reason it won't launch, the fallback is to rebuild with the
`kindlehf` toolchain instead of `kindlepw2` (one line in the Makefile). You
almost certainly won't need to.

The one thing that varies by firmware is whether `/mnt/us` is mounted "noexec"
(can't run binaries from it). Newer firmware does this. `run.sh` already handles
it by copying the binary to `/var/tmp` first, so you're covered either way.

---

## Getting started

1. Go to the [Releases page](https://github.com/itsParassharma/KindleBoy/releases)
   (every push is cross-compiled by GitHub Actions, so tagged releases always
   carry a ready-to-copy extension folder).
2. Download the zip for your firmware:
   - Firmware 5.16.3 or newer (e.g. a 10th-gen on 5.17.1) → **`kindleboy-kindlehf.zip`**
   - Older firmware, or if the hf build won't launch → `kindleboy-kindlepw2.zip`
3. Unzip it.
4. Drop the `kindleboy` folder into `extensions/` on the Kindle.
5. Put a `.gb` ROM in `roms/gb/` (it also checks the extension folder).
6. On the Kindle, launch **KUAL → KindleBoy → Play**.

---

## Playing

The game sits at the top of the screen; the controls are drawn underneath:

- **Left:** a D-pad. The corners work: hold down-left to go diagonally.
- **Right:** **A** (top) and **B** (bottom), like a real Game Boy.
- **Middle:** **START**, **SELECT**, and **MENU**.

Tap **MENU** to pause. From there you can save/load a state, flip between FAST
and QUALITY display modes, force a screen cleanup ("Deghost Now"), go back to the
game list, or quit. You can hold a direction and press A at the same time.
Multi-touch works.

Your progress saves automatically: `game.sav` for the in-game battery save
(written safely so a yanked cable won't corrupt it), and `game.st` for save
states.

---

## When something's off

- **Nothing happens on launch?** Check `/mnt/us/kindleboy.log`: everything the
  program does gets logged there.
- **Won't run at all?** Probably the noexec thing. `run.sh` already copies to
  `/var/tmp`, but if it still won't go, try the **Play (stop framework)** menu
  item, which frees up the device more aggressively.
- **Touch feels offset or mirrored?** The startup log prints the touch transform
  it picked up from FBInk plus the raw coordinate range. Grab those lines if you
  want to report it.
- **Ghosting / smearing over time?** Open and close the menu (that forces a full
  screen cleanup), or hit **Deghost Now**. If it bugs you, switch to QUALITY mode.
- **Feels sluggish?** FAST mode aims for ~8fps, which is about all A2 e-ink can
  do. That's normal for this kind of screen.

---

## How it's put together

```
core (runs anywhere)                 platform (pick one)
  emu       the Peanut-GB glue        kindle/   FBInk + touch + timing
  render    scaling + dithering       desktop/  an SDL2 window
  ui/browser/menu/overlay             sim/      fake, in-memory, for tests
  app       the state machine + the e-ink refresh scheduler
        \___________ platform.h (the one seam) ____________/
```

The core draws *every* pixel (game, menus, buttons) into a plain grayscale
buffer. The platform layer's only jobs are to show rectangles of that buffer and
to handle input. That's why the desktop build looks pixel-for-pixel identical to
the Kindle, and why almost everything could be built and tested without the
device.
