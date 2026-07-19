@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\open_pid_console.ps1"
if errorlevel 1 pause
