@echo off
setlocal
echo.
echo === Serial ports on this computer ===
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ports=[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object; if($ports.Count -eq 0){Write-Host 'No COM ports found.'} else {$ports | ForEach-Object {Write-Host $_}; Write-Host ''; Write-Host 'Device details:'; Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Description | Format-Table -AutoSize}"
echo.
pause
