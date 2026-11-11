#!/bin/bash
# adb-force 窗口内抢数据：诊断 framework 死因 + 写持久化配置
ADB="/c/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe"
export MSYS_NO_PATHCONV=1
DEV="-s BH905ND69G"
OUT="/c/Users/Cisco He/Desktop/Git/Patch/Xperia/unattend/diag-$(date +%H%M%S).log"

echo "[*] 等 adb..."
for i in $(seq 1 30); do
    "$ADB" devices 2>/dev/null | grep -q "BH905ND69G.*device" && break
    sleep 2
done
"$ADB" devices | grep -q "BH905ND69G.*device" || { echo "[!] 没等到"; exit 1; }
echo "[+] $(date +%T) adb 上线，抢数据 → $OUT" | tee -a "$OUT"

{
echo "===== ID/PROPS ====="
"$ADB" $DEV shell 'id; getprop ro.crypto.state; getprop ro.crypto.type; getprop sys.boot_completed; getprop init.svc.zygote; getprop vold.decrypt; getprop vold.post_fs_data_done; getprop ro.build.type; getprop ro.secure' 2>&1
echo "===== VOLD CRYPT ====="
"$ADB" $DEV shell 'logcat -d -b main | grep -iE "vold|crypt|keymaster|keystore" | tail -40' 2>&1
echo "===== ZYGOTE/INIT ====="
"$ADB" $DEV shell 'logcat -d -b events | tail -40' 2>&1
echo "===== CRASH ====="
"$ADB" $DEV shell 'logcat -d -b crash | tail -40' 2>&1
echo "===== MAIN TAIL ====="
"$ADB" $DEV shell 'logcat -d | tail -60' 2>&1
} | tee -a "$OUT"

echo "[*] 写持久化配置..."
"$ADB" $DEV shell 'settings put global device_provisioned 1 2>&1; settings put secure user_setup_complete 1 2>&1; settings put global adb_enabled 1 2>&1; echo SET_DONE' | tee -a "$OUT"
"$ADB" $DEV shell 'mkdir -p /data/misc/adb; chmod 700 /data/misc/adb' | tee -a "$OUT"
"$ADB" $DEV push "/c/Users/Cisco He/.android/adbkey.pub" /data/misc/adb/adb_keys | tee -a "$OUT"
"$ADB" $DEV shell 'chmod 640 /data/misc/adb/adb_keys; chown system:system /data/misc/adb/adb_keys; ls -la /data/misc/adb/; setprop persist.sys.usb.config mtp,adb' | tee -a "$OUT"
echo "[*] $(date +%T) 完事"
