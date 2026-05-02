@echo off
setlocal
cd /d "%~dp0.."
echo Starting remote debug agent. Keep this window open.
echo Edit remote_agent_config.json first: set repoRoot, xiaoPort, and deviceId.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0remote_debug_agent.ps1" -ConfigPath "%CD%\\remote_agent_config.json"
pause
