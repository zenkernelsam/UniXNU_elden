#!/bin/bash
# adb-force 窗口赛跑：在镜像死亡前把持久化 adb/向导配置写进 /data
ADB="/c/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe"
export MSYS_NO_PATHCONV=1
DEV="-s BH905ND69G"

echo "[*] 等设备枚举..."
for i in $(seq 1 20); do
    "$ADB" devices 2>/dev/null | grep -q "BH905ND69G.*device" && break
    sleep 2
done
"$ADB" devices | grep -q "BH905ND69G.*device" || { echo "[!] adb 没等到"; exit 1; }
echo "[+] $(date +%T) adb 上线，开跑！"

# 循环重试直到写完（settings provider 可能要几秒才起）
for try in 1 2 3 4 5 6 7 8; do
    R1=$("$ADB" $DEV shell "id" 2>&1)
    echo "[$try] id: $R1"
    "$ADB" $DEV shell "settings put global device_provisioned 1; settings put secure user_setup_complete 1; settings put global adb_enabled 1; echo SET_OK" 2>&1 | grep -q SET_OK && echo "[+] settings 已写" && break
    sleep 2
done

for try in 1 2 3; do
    "$ADB" $DEV shell "mkdir -p /data/misc/adb && chmod 700 /data/misc/adb && chown system:system /data/misc/adb && echo MK_OK" 2>&1 | grep -q MK_OK && break
    sleep 1
done
"$ADB" $DEV push "/c/Users/Cisco He/.android/adbkey.pub" /data/misc/adb/adb_keys 2>&1
"$ADB" $DEV shell "chmod 640 /data/misc/adb/adb_keys && chown system:system /data/misc/adb/adb_keys && ls -la /data/misc/adb/" 2>&1
"$ADB" $DEV shell "setprop persist.sys.usb.config mtp,adb; getprop persist.sys.usb.config" 2>&1
"$ADB" $DEV shell "settings get global device_provisioned; settings get secure user_setup_complete; settings get global adb_enabled" 2>&1
echo "[*] $(date +%T) 完成——现在它死不死都不重要了"
