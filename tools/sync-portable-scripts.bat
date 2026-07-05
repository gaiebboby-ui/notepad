@echo off
setlocal
cd /d "%~dp0"
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=%NOTEPAD_PORTABLE_DIR%"
if "%TARGET%"=="" (
	echo Usage: sync-portable-scripts.bat "C:\path\to\Notepad"
	echo    or set NOTEPAD_PORTABLE_DIR
	exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy-portable.ps1" -TargetDir "%TARGET%" -ScriptsOnly
if errorlevel 1 exit /b 1
echo OK: scripts copied to %TARGET%
exit /b 0
