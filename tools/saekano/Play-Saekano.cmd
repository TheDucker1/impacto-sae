@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-PC.ps1" %*
set "saekano_exit=%errorlevel%"
if not "%saekano_exit%"=="0" pause
exit /b %saekano_exit%
