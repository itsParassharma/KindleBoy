#!/bin/sh
# KindleBoy launcher (busybox ash). KUAL runs this with cwd = the extension dir.
#
# Two things make launching a fullscreen native app on Kindle awkward:
#   1. /mnt/us is often mounted noexec, so we copy the binary to /var/tmp (tmpfs)
#      and run it from there.
#   2. To own the screen we stop the Kindle UI ("framework"/"lab126_gui") — but
#      KUAL, and therefore this script, runs *inside* that UI. Stopping it would
#      kill us mid-launch (which is exactly what happened before). So when we're
#      asked to stop the UI we first re-exec ourselves in a NEW SESSION via
#      setsid; that detached copy survives the UI going down.
#
# Everything is logged to /mnt/us/kindleboy.log, including the binary's exit code.

EXT="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
[ -z "$EXT" ] && EXT="/mnt/us/extensions/kindleboy"

BIN="$EXT/bin/kindleboy"
RUN="/var/tmp/kindleboy"
LOG="/mnt/us/kindleboy.log"

# Stage the binary to exec-friendly tmpfs (safe to do before detaching).
cp -f "$BIN" "$RUN" 2>> "$LOG"
chmod +x "$RUN" 2>> "$LOG"

# If asked to stop the UI, detach into our own session first, then exit so KUAL
# is happy. The detached copy (KB_DETACHED=1) does the real work.
if [ "$1" = "stopfw" ] && [ "$KB_DETACHED" != "1" ]; then
    if command -v setsid >/dev/null 2>&1; then
        KB_DETACHED=1 setsid "$0" "$@" < /dev/null >> "$LOG" 2>&1 &
        exit 0
    fi
    # No setsid available: fall through and run WITHOUT stopping the UI, so the
    # emulator at least starts (it draws over the UI via FBInk).
    set -- ""
fi

{
  echo "=== run.sh start: $(date 2>/dev/null) params=[$*] detached=${KB_DETACHED:-0} ==="
  uname -a
  ls -la "$RUN"
} >> "$LOG" 2>&1

export KINDLEBOY_CFG="$EXT/kindleboy.cfg"
lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>> "$LOG"

STOPPED=""
if [ "$1" = "stopfw" ]; then
    echo "-- stopping UI --" >> "$LOG"
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

[ "$STOPPED" = "framework" ]  && initctl start framework >> "$LOG" 2>&1
[ "$STOPPED" = "lab126_gui" ] && start lab126_gui >> "$LOG" 2>&1
lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>> "$LOG"
exit 0
