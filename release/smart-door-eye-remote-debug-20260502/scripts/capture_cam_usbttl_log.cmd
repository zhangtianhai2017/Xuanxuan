@echo off
setlocal
echo Use this only when ESP32-CAM is connected alone to USB-TTL.
echo Do not keep USB-TTL and XIAO driving the CAM RX/TX at the same time during normal system testing.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0capture_serial_log.ps1" -Label CAM_USBTTL -Baud 115200
pause
