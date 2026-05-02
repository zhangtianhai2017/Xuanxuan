@echo off
setlocal
cd /d "%~dp0.."
echo Smart door eye remote debug bootstrap.
echo This script will clone or update the project Git repository, then start the remote debug agent.
echo Keep the final agent window open.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup_and_run_remote_agent.ps1" -ConfigPath "%CD%\\bootstrap_config.json"
pause
