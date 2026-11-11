#!/bin/bash
# watchdog.sh — autonomous phone-connection babysitter for overnight runs.
# Polls adb state; escalates recovery ladder; writes actions to watchdog.log.
# Run from Git Bash:  bash watchdog.sh &   (keeps running, logs everything)
export MSYS_NO_PATHCONV=1
ADB="C:/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe"
FB="C:/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/fastboot.exe"
UN="C:/Users/Cisco He/Desktop/Git/Patch/Xperia/unattend"
LOG="C:/Users/Cisco He/Desktop/Git/Patch/Xperia/unattend/out/watchdog.log"

say() { echo "$(date '+%H:%M:%S') $*" | tee -a "$LOG"; }
cmd_daemon() { local f="$UN/cmd/w$RANDOM.cmd"; echo "$1" > "$f"; }
adb_state() { "$ADB" devices 2>/dev/null | grep 'BH905ND69G' | awk '{print $2}'; }
fb_state() { "$FB" devices 2>/dev/null | grep -c BH905ND69G; }

say "watchdog started"
n=0
while true; do
  s=$(adb_state); f=$(fb_state)
  if [ "$s" = "device" ]; then say "adb=DEVICE"; sleep 30; n=0; continue; fi
  if [ "$f" = "1" ]; then say "fastboot mode"; sleep 30; n=0; continue; fi
  n=$((n+1)); say "adb=${s:-gone} fb=$f try=$n"
  case $n in
    1|2)  "$ADB" kill-server >/dev/null 2>&1; sleep 2; "$ADB" start-server >/dev/null 2>&1 ;;
    3|4)  cmd_daemon adbreset ;;
    5|6)  cmd_daemon devreset ;;
    7)    cmd_daemon hardreplug ;;
    8)    cmd_daemon replug ;;
    9)    say "ESCALATED: all soft recovery failed; phone needs a human"; n=0; sleep 120 ;;
  esac
  sleep 12
done
