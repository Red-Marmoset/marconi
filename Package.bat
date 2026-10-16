@echo off
REM Wrapper around Package.ps1 so `package` works from a plain Command Prompt.
REM Forwards any extra args (e.g. -Clean, -AllowDirty) through to the ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Package.ps1" %*
if %errorlevel% neq 0 exit /b %errorlevel%
