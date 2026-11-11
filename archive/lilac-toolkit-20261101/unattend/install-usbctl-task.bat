@echo off
rem RIGHT-CLICK -> Run as administrator. Installs usbctl as a SYSTEM task
rem so the agent can start it anytime with `schtasks /run` (no more UAC).
net session >nul 2>&1 || (echo NEED ADMIN - right click, run as administrator & pause & exit /b 1)
schtasks /create /tn "XperiaUsbCtl" /tr "powershell -NoProfile -ExecutionPolicy Bypass -File \"C:\Users\Cisco He\Desktop\Git\Patch\Xperia\unattend\usbctl.ps1\"" /sc onstart /ru SYSTEM /rl HIGHEST /f
schtasks /run /tn "XperiaUsbCtl"
echo Task installed and started. Check unattend\out\daemon.log in ~5 seconds.
pause
