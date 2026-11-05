@echo off
setlocal
cd /d "%~dp0"
title Member3 one-click start
echo ============================================================
echo  Member3 one-click start
echo  Starts FastAPI + Web dashboard + default simulator scene
echo  Does NOT reset the shared SQLite database
echo  Prerequisite: member-1 C++ server on 127.0.0.1:48080
echo ============================================================
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-member3-stack.ps1" -ServerHost 127.0.0.1 -PileId 101 -Scenario normal
set "ERR=%ERRORLEVEL%"
echo.
if not "%ERR%"=="0" (
    echo Start failed. Read the error above.
    echo Common causes:
    echo   - C++ server is not running on 127.0.0.1:48080
    echo   - runtime\charge_platform.sqlite is missing
    echo   - analytics\.venv or dashboard\node_modules is missing
    echo   - pile_simulator.exe is not built
) else (
    echo Browser: http://127.0.0.1:4173/
    echo Closing this window does NOT stop FastAPI or the dashboard.
    echo Double-click stop-member3.bat when you want to stop them.
)
echo.
pause
exit /b %ERR%
