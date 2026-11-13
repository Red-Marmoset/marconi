@echo off
rem Copyright (c) 2026 Red Marmoset Studios Ltd.
rem TestAiTools.bat - Tests "Marconi for AI tools" (MCP_PLAN.md) on Windows without touching your own checkouts.
rem
rem Run it from your Marconi checkout, whatever branch it's on:   TestAiTools.bat
rem When you're done with it:                                      TestAiTools.bat clean
rem
rem It puts both branches (Marconi's and the engine's claude/sweet-bell-nobab4) in their own folders under
rem %LOCALAPPDATA%\MarconiAiToolsTest, as git worktrees (extra checkouts sharing your repos' history). The engine copy
rem borrows your engine's modules, wxWidgets and BuildTool (C:\code\engine, or UNA_ENGINE_PATH) through folder links,
rem so nothing is downloaded or rebuilt twice. Then it builds and runs the tests and walks through Claude Code, the
rem AI tools window and the installer, asking before anything that changes your setup.

setlocal EnableDelayedExpansion
set "BRANCH=claude/sweet-bell-nobab4"
set "TESTDIR=%LOCALAPPDATA%\MarconiAiToolsTest"
set "MW=%TESTDIR%\marconi"
set "EW=%TESTDIR%\engine"
if defined UNA_ENGINE_PATH (set "ENGINE=%UNA_ENGINE_PATH%") else (set "ENGINE=C:\code\engine")
for /f "delims=" %%r in ('git rev-parse --show-toplevel 2^>nul') do set "MARCONI=%%~fr"
set "PS=powershell -NoProfile -ExecutionPolicy Bypass -File"
set "PASSED=0"
set "FAILED=0"
set "FAILURES="
set "REGISTERED="

if not defined MARCONI (
    echo Run this from your Marconi checkout.
    goto :end
)
if not exist "%MARCONI%\Mcp" if not exist "%MARCONI%\Modules\MarconiCore" (
    echo %MARCONI% doesn't look like Marconi. Run this from your Marconi checkout.
    goto :end
)
if not exist "%ENGINE%\Packaging\PackageProject.ps1" (
    echo No una engine at %ENGINE%. Set UNA_ENGINE_PATH.
    goto :end
)
if /i "%~1"=="clean" goto :clean

echo.
echo === Marconi for AI tools: test run
echo     Your Marconi: %MARCONI%   (left as it is)
echo     Your engine:  %ENGINE%   (left as it is)
echo     Test copies:  %TESTDIR%
echo.

rem ---------------------------------------------------------------------------------------------------------------
echo === 1. Test copies of both branches
call git -C "%MARCONI%" fetch origin %BRANCH%
if errorlevel 1 goto :gitfailed
call git -C "%ENGINE%" fetch origin %BRANCH%
if errorlevel 1 goto :gitfailed
if exist "%MW%\.git" (
    call git -C "%MW%" checkout --quiet --force --detach origin/%BRANCH%
) else (
    call git -C "%MARCONI%" worktree add --detach "%MW%" origin/%BRANCH%
)
if errorlevel 1 goto :gitfailed
if exist "%EW%\.git" (
    call git -C "%EW%" checkout --quiet --force --detach origin/%BRANCH%
) else (
    call git -C "%ENGINE%" worktree add --detach "%EW%" origin/%BRANCH%
)
if errorlevel 1 goto :gitfailed
rem The engine copy's submodules (and its Build folder, which holds BuildTool) are links to your engine's.
for /f "tokens=2" %%p in ('git -C "%EW%" config -f .gitmodules --get-regexp "\.path$"') do (
    set "SUB=%%p"
    set "SUB=!SUB:/=\!"
    rem Linked once: an empty folder (what a worktree leaves for a submodule) becomes the link.
    dir /b "%EW%\!SUB!" 2>nul | findstr . >nul
    if errorlevel 1 if exist "%ENGINE%\!SUB!\*" (
        rmdir "%EW%\!SUB!" 2>nul
        mklink /J "%EW%\!SUB!" "%ENGINE%\!SUB!" >nul
    )
)
if not exist "%EW%\Build" mklink /J "%EW%\Build" "%ENGINE%\Build" >nul
set "UNA_ENGINE_PATH=%EW%"
set "MCP=%MW%\Build\Marconi\Release\MarconiMcp.exe"
for /f "delims=" %%c in ('git -C "%MW%" log -1 --format^=%%h') do echo Marconi test copy at %%c
for /f "delims=" %%c in ('git -C "%EW%" log -1 --format^=%%h') do echo Engine test copy at %%c
echo.

rem ---------------------------------------------------------------------------------------------------------------
echo === 2. Engine tests (Process, AppDetect)
pushd "%MW%"
for %%t in (ProcessTest AppDetectTest) do (
    del "%MW%\Build\%%t\Debug\%%t.exe" 2>nul
    call %PS% "%MW%\Build.ps1" -Project "%EW%\Tests\%%t\Project.json" -Config Debug
    if not exist "%MW%\Build\%%t\Debug\%%t.exe" (
        call :fail "%%t didn't build"
    ) else (
        rem AppDetectTest reads Packaging\Apps.json from the folder it runs in.
        pushd "%EW%"
        "%MW%\Build\%%t\Debug\%%t.exe"
        if errorlevel 1 (call :fail "%%t") else (call :pass "%%t")
        popd
    )
)
popd
echo.

rem ---------------------------------------------------------------------------------------------------------------
echo === 3. Marconi's tests (two network-timing tests may fail on some networks)
pushd "%MW%"
del "%MW%\Build\MarconiCoreTest\Debug\MarconiCoreTest.exe" 2>nul
call %PS% "%MW%\Build.ps1" -Project Tests\MarconiCoreTest\Project.json -Config Debug
if not exist "%MW%\Build\MarconiCoreTest\Debug\MarconiCoreTest.exe" (
    call :fail "MarconiCoreTest didn't build"
) else (
    "%MW%\Build\MarconiCoreTest\Debug\MarconiCoreTest.exe" > "%MW%\Build\MarconiCoreTest.log" 2>&1
    set "TESTS=!errorlevel!"
    findstr /c:"FAIL" "%MW%\Build\MarconiCoreTest.log"
    if "!TESTS!"=="0" (call :pass "MarconiCoreTest") else (call :fail "MarconiCoreTest: !TESTS! failures, see %MW%\Build\MarconiCoreTest.log")
)
popd
echo.

rem ---------------------------------------------------------------------------------------------------------------
echo === 4. The app, with MarconiMcp, Apps.json and marconi.mcpb beside it
pushd "%MW%"
del "%MW%\Build\Marconi\Release\Marconi.exe" "%MW%\Build\Marconi\Release\MarconiMcp.exe" 2>nul
call %PS% "%MW%\Build.ps1"
popd
set "MISSING="
for %%f in (Marconi.exe MarconiMcp.exe Apps.json marconi.mcpb) do (
    if not exist "%MW%\Build\Marconi\Release\%%f" set "MISSING=!MISSING! %%f"
)
if defined MISSING (
    call :fail "the app build is missing:!MISSING!"
    echo The rest needs the app; stopping.
    goto :summary
)
call :pass "app build"
echo.

rem ---------------------------------------------------------------------------------------------------------------
echo === 5. Which AI tools MarconiMcp finds
"%MCP%" --status --quiet
if errorlevel 1 (call :fail "MarconiMcp --status") else (call :pass "MarconiMcp --status")
echo (one line per tool: found, where, and whether it already uses Marconi)
echo.

rem ---------------------------------------------------------------------------------------------------------------
echo === 6. Claude Code drives a computer through this build
where claude >nul 2>&1
if errorlevel 1 (
    echo Claude Code isn't on PATH in this window; skipping this step.
    goto :window
)
choice /c YN /m "Add this build's MarconiMcp to Claude Code (removed again at the end if you like)"
if errorlevel 2 goto :window
"%MCP%" --register claude-code --quiet
if errorlevel 1 (call :fail "registering with Claude Code") else (call :pass "registering with Claude Code" & set "REGISTERED=1")
call claude mcp get marconi
set "PYTHON="
where py >nul 2>&1 && set "PYTHON=py -3"
if not defined PYTHON (where python >nul 2>&1 && set "PYTHON=python")
if defined PYTHON (
    start "Fake VNC server - prints every click and key" %PYTHON% "%MW%\Tools\fake_vnc_server.py" --port 5999
    echo A fake VNC server is running in its own window on port 5999; it prints every click and key it gets.
) else (
    echo Python isn't installed, so there's no fake server; use a real computer instead.
)
echo.
echo Now start a new Claude Code session and ask, for example:
echo     Use the marconi tools: connect to 127.0.0.1::5999, take a screenshot and describe it,
echo     click in the middle of the screen, type hello, then disconnect.
echo The fake server's window should show the click and the typing. Then try a real computer, watching it at the
echo same time in a normal Marconi window. If it needs a password, give it to Claude when it asks.
echo.
choice /c YN /m "Did Claude Code connect, see the screen and click and type"
if errorlevel 2 (call :fail "Claude Code driving a computer") else (call :pass "Claude Code driving a computer")
echo.

:window
rem ---------------------------------------------------------------------------------------------------------------
echo === 7. The AI tools window
echo Marconi is starting: open Settings, then "AI tools...". It should list Claude Code, Codex and Claude Desktop
echo with what it found. Try Remove and Add (Claude Desktop's Add opens Claude to confirm the extension).
start "" "%MW%\Build\Marconi\Release\Marconi.exe"
choice /c YN /m "Did the window show your tools, and did Add and Remove work"
if errorlevel 2 (call :fail "AI tools window") else (call :pass "AI tools window")
echo.

rem ---------------------------------------------------------------------------------------------------------------
echo === 8. The installer
choice /c YN /m "Build the installer and try it now (it installs Marconi from this test copy)"
if errorlevel 2 goto :cleanup
pushd "%MW%"
call %PS% "%MW%\Package.ps1" -SkipBuild -AllowDirty
popd
set "SETUP="
for /f "delims=" %%s in ('dir /b /o-d "%MW%\Build\Installer\Marconi-*-win64-setup.exe" 2^>nul') do if not defined SETUP set "SETUP=%MW%\Build\Installer\%%s"
if not defined SETUP (
    call :fail "Package.ps1 made no installer"
    goto :cleanup
)
call :pass "Package.ps1"
echo Running !SETUP!. After installing, the page "Connect Marconi to your AI tools" should list your tools.
start /wait "" "!SETUP!"
echo.
call claude mcp get marconi 2>nul
echo (Claude Code's entry should now point at the installed MarconiMcp.exe, if you ticked it)
choice /c YN /m "Did the AI tools page appear and the ticked tools get Marconi"
if errorlevel 2 (call :fail "installer AI tools page") else (call :pass "installer AI tools page")
echo Now uninstall Marconi (Settings ^> Apps), then answer.
choice /c YN /m "Did uninstalling take Marconi out of Claude Code (claude mcp get marconi says not found)"
if errorlevel 2 (call :fail "uninstall removing it from the AI tools") else (call :pass "uninstall removing it from the AI tools")
set "REGISTERED="
echo.

:cleanup
rem ---------------------------------------------------------------------------------------------------------------
if defined REGISTERED (
    choice /c YN /m "Remove this build's MarconiMcp from Claude Code again"
    if not errorlevel 2 "%MCP%" --unregister claude-code --quiet
)

:summary
echo.
echo === Summary: %PASSED% passed, %FAILED% failed
if defined FAILURES echo Failed%FAILURES%
echo Logs: %LOCALAPPDATA%\marconi\logs (MarconiMcp's are marconi-mcp_*.log). Build output: %MW%\Build
echo To remove the test copies: TestAiTools.bat clean
goto :end

:clean
rem The links go first, so removing the engine copy can't reach into your engine.
echo Removing the test copies in %TESTDIR%
if exist "%EW%\.git" (
    for /f "tokens=2" %%p in ('git -C "%EW%" config -f .gitmodules --get-regexp "\.path$"') do (
        set "SUB=%%p"
        set "SUB=!SUB:/=\!"
        rmdir "%EW%\!SUB!" 2>nul
    )
    rmdir "%EW%\Build" 2>nul
    call git -C "%ENGINE%" worktree remove --force "%EW%"
)
if exist "%MW%\.git" call git -C "%MARCONI%" worktree remove --force "%MW%"
rmdir "%TESTDIR%" 2>nul
echo Done.
goto :end

:gitfailed
echo ERROR: git failed; see above.

:end
echo.
pause
endlocal
exit /b

:pass
set /a PASSED+=1
echo PASS: %~1
exit /b 0

:fail
set /a FAILED+=1
set "FAILURES=%FAILURES%; %~1"
echo FAIL: %~1
exit /b 0
