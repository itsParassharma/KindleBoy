#!/bin/sh
# kindleboy launcher (busybox ash). KUAL runs this with cwd = the extension dir.
#
# Recent Kindle firmware mounts /mnt/us noexec, so a native binary cannot be
# exec'd from there directly. We copy it to /var/tmp (tmpfs, exec-allowed) and
# run it from there. We also stop the screensaver for the duration and,
# optionally (param "stopfw"), stop the Kindle framework to free RAM / get a
# guaranteed-clean screen.

EXT="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
[ -z "$EXT" ] && EXT="/mnt/us/extensions/kindleboy"

BIN="$EXT/bin/kindleboy"
RUN="/var/tmp/kindleboy"
LOG="/mnt/us/kindleboy.log"

# Point the emulator's preferences + ROM dir at known locations.
export KINDLEBOY_CFG="$EXT/kindleboy.cfg"

cp -f "$BIN" "$RUN" 2>>"$LOG" && chmod +x "$RUN"

# Keep the device awake while playing.
lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>>"$LOG"

STOPFW=0
[ "$1" = "stopfw" ] && STOPFW=1
if [ "$STOPFW" = "1" ]; then
    initctl stop framework 2>>"$LOG" || stop lab126_gui 2>>"$LOG"
    sleep 2
fi

"$RUN" >>"$LOG" 2>&1

if [ "$STOPFW" = "1" ]; then
    initctl start framework 2>>"$LOG" || start lab126_gui 2>>"$LOG"
fi

lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>>"$LOG"
exit 0
