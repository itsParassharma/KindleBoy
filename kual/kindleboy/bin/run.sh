#!/bin/sh
# KindleBoy launcher (busybox ash). KUAL runs this with cwd = the extension dir.
#
# Recent Kindle firmware mounts /mnt/us noexec, so a native binary cannot be
# exec'd from there directly. We copy it to /var/tmp (tmpfs, exec-allowed) and
# run it from there. We also stop the screensaver for the duration and,
# optionally (param "stopfw"), stop the Kindle framework to free RAM / get a
# guaranteed-clean screen.
#
# Every step is logged to /mnt/us/kindleboy.log, including the binary's exit
# code and the kernel version, so a failed launch is diagnosable after the fact.

EXT="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
[ -z "$EXT" ] && EXT="/mnt/us/extensions/kindleboy"

BIN="$EXT/bin/kindleboy"
RUN="/var/tmp/kindleboy"
LOG="/mnt/us/kindleboy.log"

{
  echo "=== run.sh start: $(date 2>/dev/null) params=[$*] ==="
  echo "EXT=$EXT"
  uname -a
  echo "-- binary on disk --"
  ls -la "$BIN"
} >> "$LOG" 2>&1

export KINDLEBOY_CFG="$EXT/kindleboy.cfg"

cp -f "$BIN" "$RUN" >> "$LOG" 2>&1
chmod +x "$RUN" 2>> "$LOG"
echo "-- staged binary at $RUN --" >> "$LOG"
ls -la "$RUN" >> "$LOG" 2>&1

lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>> "$LOG"

STOPFW=0
[ "$1" = "stopfw" ] && STOPFW=1
if [ "$STOPFW" = "1" ]; then
    echo "-- stopping framework --" >> "$LOG"
    initctl stop framework 2>> "$LOG" || stop lab126_gui 2>> "$LOG"
    sleep 2
fi

echo "=== launching binary ===" >> "$LOG"
"$RUN" >> "$LOG" 2>&1
echo "=== binary exited, code=$? ===" >> "$LOG"

if [ "$STOPFW" = "1" ]; then
    initctl start framework 2>> "$LOG" || start lab126_gui 2>> "$LOG"
fi

lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>> "$LOG"
exit 0
