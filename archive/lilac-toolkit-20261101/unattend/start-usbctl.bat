@echo off
rem Double-click this file. UAC will prompt on YOUR desktop (screen dims).
rem Click Yes - a PowerShell window opens running the daemon. Minimize it.
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0usbctl.ps1"
