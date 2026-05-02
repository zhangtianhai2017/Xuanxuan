@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0capture_serial_log.ps1" -Label XIAO_MAIN -Baud 115200
pause
