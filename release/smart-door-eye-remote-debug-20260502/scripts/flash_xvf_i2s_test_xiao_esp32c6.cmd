@echo off
setlocal
cd /d "%~dp0.."
echo.
echo === Flash XVF3800 I2S test firmware to XIAO ESP32C6 ===
echo This temporary test replaces the main firmware. Re-flash main firmware after the audio test.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object | ForEach-Object {Write-Host ('  ' + $_)}"
echo.
set /p PORT=Enter XIAO COM port, for example COM5: 
if "%PORT%"=="" goto noport
tools\esptool\esptool.exe --chip esp32c6 --port %PORT% --baud 921600 --before default_reset --after hard_reset erase_flash
if errorlevel 1 goto fail
tools\esptool\esptool.exe --chip esp32c6 --port %PORT% --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode qio --flash_freq 80m --flash_size 4MB 0x0 "firmware\xvf_i2s_test_xiao_esp32c6\sketch_apr18a.ino.merged.bin"
if errorlevel 1 goto fail
echo.
echo Flash OK. Open capture_xiao_log.cmd and listen for a 440 Hz tone.
pause
exit /b 0
:noport
echo No COM port entered.
pause
exit /b 2
:fail
echo.
echo Flash failed. Try another cable or COM port.
pause
exit /b 1
