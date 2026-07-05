@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register-associations.ps1" -Extended %*
if errorlevel 1 (
	echo.
	echo FAILED. Notepad.exe must be in the same folder as this script.
	pause
	exit /b 1
)
echo.
pause
