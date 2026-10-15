@echo off
REM Wrapper around Build.ps1 so `build` works from a plain Command Prompt.
REM Forwards any extra args (e.g. -Clean, -Config Debug) through to the ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build.ps1" %*
if %errorlevel% neq 0 exit /b %errorlevel%
