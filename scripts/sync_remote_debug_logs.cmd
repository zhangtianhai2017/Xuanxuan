@echo off
setlocal
cd /d "%~dp0.."
echo Pull and show latest remote debug logs.
echo.
set /p DEVICE_ID=Device id [smart-door-eye-lab-01]: 
if "%DEVICE_ID%"=="" set DEVICE_ID=smart-door-eye-lab-01
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sync_remote_debug_logs.ps1" -DeviceId "%DEVICE_ID%"
pause
