@echo off
setlocal
cd /d "%~dp0.."
echo Starting local upload receiver. Keep this window open.
echo If Windows Firewall asks, allow access on the network you use for debugging.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_simple_upload_receiver.ps1" -Port 8088
pause
