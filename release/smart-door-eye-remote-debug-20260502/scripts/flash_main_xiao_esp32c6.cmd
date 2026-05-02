@echo off
setlocal
cd /d "%~dp0.."
echo.
echo === Flash MAIN firmware to XIAO ESP32C6 ===
echo This will erase flash, then write the smart door eye main firmware.
echo Use the COM port shown when the XIAO ESP32C6 is connected by USB-C.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object | ForEach-Object {Write-Host ('  ' + $_)}"
echo.
set /p PORT=Enter XIAO COM port, for example COM5: 
if "%PORT%"=="" goto noport
tools\esptool\esptool.exe --chip esp32c6 --port %PORT% --baud 921600 --before default_reset --after hard_reset erase_flash
if errorlevel 1 goto fail
tools\esptool\esptool.exe --chip esp32c6 --port %PORT% --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode qio --flash_freq 80m --flash_size 4MB 0x0 "firmware\main_xiao_esp32c6\smartdooreye.ino.merged.bin"
if errorlevel 1 goto fail
echo.
echo Flash OK. Press RESET on XIAO, then run capture_xiao_log.cmd.
pause
exit /b 0
:noport
echo No COM port entered.
pause
exit /b 2
:fail
echo.
echo Flash failed. Try a lower-quality cable check, press BOOT/RESET if needed, or use another COM port.
pause
exit /b 1
