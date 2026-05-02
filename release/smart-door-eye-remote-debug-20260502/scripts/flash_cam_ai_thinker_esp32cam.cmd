@echo off
setlocal
cd /d "%~dp0.."
echo.
echo === Flash CAM firmware to AI Thinker ESP32-CAM ===
echo Before flashing:
echo   1. Connect ESP32-CAM GPIO0 to GND.
echo   2. Power ESP32-CAM with stable 5V.
echo   3. Connect USB-TTL TX/RX crossed with CAM U0R/U0T.
echo After flashing:
echo   1. Disconnect GPIO0 from GND.
echo   2. Reset or power-cycle ESP32-CAM.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object | ForEach-Object {Write-Host ('  ' + $_)}"
echo.
set /p PORT=Enter ESP32-CAM USB-TTL COM port, for example COM7: 
if "%PORT%"=="" goto noport
tools\esptool\esptool.exe --chip esp32 --port %PORT% --baud 460800 --before default_reset --after hard_reset erase_flash
if errorlevel 1 goto fail
tools\esptool\esptool.exe --chip esp32 --port %PORT% --baud 460800 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 "firmware\cam_ai_thinker_esp32cam\CameraWebServer.ino.merged.bin"
if errorlevel 1 goto fail
echo.
echo Flash OK. Disconnect GPIO0 from GND, then reset ESP32-CAM.
pause
exit /b 0
:noport
echo No COM port entered.
pause
exit /b 2
:fail
echo.
echo Flash failed. Check GPIO0-GND, 5V power, USB-TTL wiring, and COM port.
pause
exit /b 1
