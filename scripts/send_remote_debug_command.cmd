@echo off
setlocal
cd /d "%~dp0.."
echo Send a remote debug command through Git.
echo.
set /p DEVICE_ID=Device id [smart-door-eye-lab-01]: 
set /p ACTION=Action [flash_main / flash_xvf_test] default flash_main: 
set /p PORT=Override COM port, empty uses student config: 
set /p ERASE=Erase before flash? [Y/n]: 
if "%DEVICE_ID%"=="" set DEVICE_ID=smart-door-eye-lab-01
if "%ACTION%"=="" set ACTION=flash_main
if /I "%ERASE%"=="n" (
  set ERASE_VALUE=false
) else (
  set ERASE_VALUE=true
)

if "%PORT%"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0send_remote_debug_command.ps1" -DeviceId "%DEVICE_ID%" -Action "%ACTION%" -Erase $%ERASE_VALUE%
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0send_remote_debug_command.ps1" -DeviceId "%DEVICE_ID%" -Action "%ACTION%" -Port "%PORT%" -Erase $%ERASE_VALUE%
)
pause
