#!/bin/sh
# KindleBoy launcher (busybox ash). KUAL runs this with cwd = the extension dir.
#
# Lessons learned on real hardware, encoded here:
#   1. /mnt/us is often mounted noexec -> stage the binary to /var/tmp (tmpfs).
#   2. NEVER stop the framework from inside KUAL's own process: KUAL lives in
#      the framework, so stopping it kills the launcher (and leaves the Kindle
#      UI in a "application could not be started" sulk). Instead we detach into
#      our own session with setsid FIRST, and by default we don't stop the UI
#      at all — we just disable the screensaver + pillow repaints and draw
#      straight to the framebuffer over the live UI, the way KOReader and
#      Gambatte-K2 do. "stopfw" remains available as an explicit option.
#   3. Everything is logged to /mnt/us/kindleboy.log, including the binary's
#      exit code, so any failure is diagnosable from the PC afterwards.

EXT="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
[ -z "$EXT" ] && EXT="/mnt/us/extensions/kindleboy"

BIN="$EXT/bin/kindleboy"
RUN="/var/tmp/kindleboy"
LOG="/mnt/us/kindleboy.log"

# Stage the binary onto exec-friendly tmpfs.
cp -f "$BIN" "$RUN" 2>> "$LOG"
chmod +x "$RUN" 2>> "$LOG"

# Detach into our own session so (a) KUAL's action returns immediately and
# (b) nothing that happens to the UI can take us down. The detached copy
# (KB_DETACHED=1) does the real work below.
if [ "$KB_DETACHED" != "1" ]; then
    if command -v setsid >/dev/null 2>&1; then
        KB_DETACHED=1 setsid "$0" "$@" < /dev/null >> "$LOG" 2>&1 &
    else
        KB_DETACHED=1 "$0" "$@" < /dev/null >> "$LOG" 2>&1 &
    fi
    exit 0
fi

{
  echo ""
  echo "=== KindleBoy launch: $(date 2>/dev/null) params=[$*] ==="
  uname -a
  ls -la "$RUN"
} >> "$LOG" 2>&1

export KINDLEBOY_CFG="$EXT/kindleboy.cfg"

# Keep the device awake and stop pillow (status bar / UI chrome) repainting
# over the game while the Kindle UI stays alive underneath us.
lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>> "$LOG"
lipc-set-prop com.lab126.pillow disableEnablePillow disable 2>> "$LOG"

# Pin the CPU to full speed while playing, restoring the original governor on
# exit. Tolerant of devices that don't expose cpufreq (the writes just fail).
GOV_PATH="/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
OLD_GOV=""
if [ -w "$GOV_PATH" ]; then
    OLD_GOV="$(cat "$GOV_PATH" 2>/dev/null)"
    echo performance > "$GOV_PATH" 2>> "$LOG"
    echo "-- cpu governor: $OLD_GOV -> performance --" >> "$LOG"
fi

STOPPED=""
if [ "$1" = "stopfw" ]; then
    echo "-- stopping UI (explicit stopfw mode) --" >> "$LOG"
    if initctl stop framework >> "$LOG" 2>&1; then
        STOPPED="framework"
    elif stop lab126_gui >> "$LOG" 2>&1; then
        STOPPED="lab126_gui"
    fi
    sleep 2
fi

echo "=== launching binary ===" >> "$LOG"
"$RUN" >> "$LOG" 2>&1
echo "=== binary exited, code=$? ===" >> "$LOG"

# Restore the CPU governor.
[ -n "$OLD_GOV" ] && echo "$OLD_GOV" > "$GOV_PATH" 2>> "$LOG"

[ "$STOPPED" = "framework" ]  && initctl start framework >> "$LOG" 2>&1
[ "$STOPPED" = "lab126_gui" ] && start lab126_gui >> "$LOG" 2>&1

lipc-set-prop com.lab126.pillow disableEnablePillow enable 2>> "$LOG"
lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>> "$LOG"
exit 0
