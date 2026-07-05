@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register-associations.ps1" -Extended -Unregister %*
if errorlevel 1 (
	echo.
	echo FAILED.
	pause
	exit /b 1
)
echo.
pause
