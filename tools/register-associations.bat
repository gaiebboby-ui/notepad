@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register-associations.ps1" -Extended %*
if errorlevel 1 (
	echo.
	echo FAILED. See message above.
	echo Example: register-associations.bat -ExePath "C:\Apps\Notepad\Notepad.exe"
	pause
	exit /b 1
)
echo.
pause
