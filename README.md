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

## The honest catch: you have to compile the binary

KindleBoy is a native ARM program. I can't hand you a ready-to-run `.exe` for
the Kindle the way you'd copy a script. It's machine code for the Kindle's CPU,
and it has to be *cross-compiled* on a Linux machine (or WSL on Windows). That's
a one-time setup, maybe 20 minutes, and then rebuilds take seconds.

Everything *else* in the extension is ready to paste. The only missing piece is
`bin/kindleboy`, which the build produces.

---

## Easiest: download a prebuilt binary

You don't have to build anything. Every push is cross-compiled by GitHub Actions,
and tagged releases carry a ready-to-copy extension folder:

**https://github.com/itsParassharma/KindleBoy/releases**

- Firmware 5.16.3 or newer (e.g. a 10th-gen on 5.17.1) → **`kindleboy-kindlehf.zip`**
- Older firmware, or if the hf build won't launch → `kindleboy-kindlepw2.zip`

Unzip it, drop the `kindleboy` folder into `extensions/` on the Kindle, put a
`.gb` in `roms/gb/` (it also checks the extension folder), and launch
**KUAL → KindleBoy → Play**.

The rest of this section is only if you want to build it yourself.

## Building it yourself

You need Linux. On Windows that means WSL:

```powershell
# Admin PowerShell, one time. Reboots afterward.
wsl --install
```

Then, inside Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential git libsdl2-dev
```

### Desktop build (play on your PC, no Kindle needed)

Great for trying games and testing before you deploy:

```bash
make desktop
./build/kindleboy_desktop path/to/game.gb   # or leave the ROM off to get the browser
```

Keys: **arrows** move, **Z** = B, **X** = A, **Enter** = Start,
**Right-Shift** = Select, **Esc** = pause menu, **click** = touch.
Run with `KINDLEBOY_EINK=1` to fake the ~8fps e-ink feel.

### Kindle build

1. Grab the cross-compiler (koxtoolchain, `kindlepw2`). Prebuilt is easiest:

   ```bash
   cd ~
   # download kindlepw2.tar.gz from https://github.com/koreader/koxtoolchain/releases
   tar xzf kindlepw2.tar.gz
   export PATH="$HOME/x-tools/arm-kindlepw2-linux-gnueabi/bin:$PATH"
   arm-kindlepw2-linux-gnueabi-gcc --version   # should print a version
   ```
   (Drop that `export PATH` line into `~/.bashrc` so it sticks.)

2. Grab FBInk (it lives in `vendor/`, we don't check it in):

   ```bash
   git clone --recursive https://github.com/NiLuJe/FBInk vendor/FBInk
   ```

3. Build:

   ```bash
   make fbink     # builds FBInk once
   make kindle    # -> build/kindleboy
   ```

### Getting it onto the Kindle

Plug in the Kindle over USB (it shows up as a drive), then:

```bash
make deploy KINDLE=/mnt/e     # point KINDLE at the Kindle's mount
```

That drops the extension at `<kindle>/extensions/kindleboy/` and makes a
`roms/gb/` folder. Copy your `.gb` files into `roms/gb/`, unplug, and on the
Kindle open **KUAL → KindleBoy → Play**.

You can also just drag the files across in Explorer: put the `kual/kindleboy`
folder at `extensions/kindleboy` on the Kindle, with the compiled binary at
`extensions/kindleboy/bin/kindleboy`.

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

---

## Testing without a Kindle

```bash
make headless && ./build/headless dmg-acid2.gb 120 30 120   # dump frames as images
make sim      && ./build/simrun play dmg-acid2.gb            # render the real UI to images
```

`sim` runs the actual app (browse → play → pause menu) against a fake in-memory
screen and saves snapshots, so you can eyeball UI and rendering changes with no
device and no SDL. It's how the browser, controls, dithering, and menu were
checked. See `test/`.

### First things to check on the actual device

The only stuff that genuinely can't be tested off-device is the e-ink behavior.
After your first deploy:

1. **Frame rate.** Confirm A2 holds ~7-8fps and the dither looks readable
   (`/mnt/us/kindleboy.log` reports it). If it drags, nudge the defaults toward
   the slower-but-cleaner DU/QUALITY modes: nothing else changes.
2. **Touch.** Make sure taps land where you expect (the log shows the transform).
3. Then just play something and confirm saves and a clean exit.
