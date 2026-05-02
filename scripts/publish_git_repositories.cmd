@echo off
setlocal
cd /d "%~dp0.."
echo This publishes the full project repo and the student remote-debug repo.
echo.
set /p FULL_REPO_URL=Full project Git URL: 
set /p STUDENT_REPO_URL=Student remote-debug Git URL: 
set /p DEVICE_ID=Device id [smart-door-eye-lab-01]: 
set /p XIAO_PORT=XIAO COM port in bootstrap [COM5]: 
if "%DEVICE_ID%"=="" set DEVICE_ID=smart-door-eye-lab-01
if "%XIAO_PORT%"=="" set XIAO_PORT=COM5
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0publish_git_repositories.ps1" -FullRepoUrl "%FULL_REPO_URL%" -StudentRepoUrl "%STUDENT_REPO_URL%" -DeviceId "%DEVICE_ID%" -BootstrapPort "%XIAO_PORT%"
pause
