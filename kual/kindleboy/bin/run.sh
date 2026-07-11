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

# Keep the device awake, and silence the system chrome. The pillow disable
# only works on old firmware; since ~5.7.2 the titlebar clock is repainted by
# the "awesome" window manager, so we freeze that process instead (KOReader's
# trick) and thaw it on exit. volumd gets frozen too so plugging in USB can't
# paint a dialog over the game. In stopfw mode the whole framework goes away,
# so only volumd needs freezing there.
lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>> "$LOG"
lipc-set-prop com.lab126.pillow disableEnablePillow disable 2>> "$LOG"

FROZE=""
FREEZE_LIST="volumd"
[ "$1" != "stopfw" ] && FREEZE_LIST="awesome volumd"
for p in $FREEZE_LIST; do
    if pidof "$p" >/dev/null 2>&1; then
        killall -STOP "$p" 2>> "$LOG" && FROZE="$FROZE $p"
    fi
done
[ -n "$FROZE" ] && echo "-- frozen:$FROZE --" >> "$LOG"

# Pin the CPU to full speed while playing, restoring the original governor on
# exit. Tolerant of devices that don't expose cpufreq (the writes just fail).
GOV_PATH="/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
OLD_GOV=""
if [ -w "$GOV_PATH" ]; then
    OLD_GOV="$(cat "$GOV_PATH" 2>/dev/null)"
    echo performance > "$GOV_PATH" 2>> "$LOG"
    echo "-- cpu governor: $OLD_GOV -> performance --" >> "$LOG"
fi

# Max-performance mode: take down the Java framework AND the background
# services KOReader's launcher also stops, giving the emulator the whole CPU
# and ~100MB of RAM back. Everything restarts on exit — no reboot needed, the
# Kindle UI just takes ~20-30s to come back.
STOPPED=""
STOPPED_SVCS=""
SERVICES="stored webreader kfxreader kfxview todo tmd rcm archive scanner otav3 otaupd"
if [ "$1" = "stopfw" ]; then
    echo "-- max performance: stopping UI + background services --" >> "$LOG"
    trap "" TERM
    if initctl stop framework >> "$LOG" 2>&1; then
        STOPPED="framework"
    elif stop lab126_gui >> "$LOG" 2>&1; then
        STOPPED="lab126_gui"
    fi
    usleep 1250000 2>/dev/null || sleep 2
    trap - TERM
    for job in $SERVICES; do
        stop "$job" >> "$LOG" 2>&1 && STOPPED_SVCS="$STOPPED_SVCS $job"
    done
    echo "-- stopped:$STOPPED$STOPPED_SVCS --" >> "$LOG"
fi

echo "=== launching binary ===" >> "$LOG"
"$RUN" >> "$LOG" 2>&1
echo "=== binary exited, code=$? ===" >> "$LOG"

# Restore the CPU governor.
[ -n "$OLD_GOV" ] && echo "$OLD_GOV" > "$GOV_PATH" 2>> "$LOG"

# Bring back everything we stopped or froze, services first so the framework
# finds them running when it comes up.
for job in $STOPPED_SVCS; do
    start "$job" >> "$LOG" 2>&1
done
[ "$STOPPED" = "framework" ]  && initctl start framework >> "$LOG" 2>&1
[ "$STOPPED" = "lab126_gui" ] && start lab126_gui >> "$LOG" 2>&1

for p in $FROZE; do
    killall -CONT "$p" 2>> "$LOG"
done

lipc-set-prop com.lab126.pillow disableEnablePillow enable 2>> "$LOG"
lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>> "$LOG"
exit 0
