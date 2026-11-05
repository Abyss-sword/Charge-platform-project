@echo off
setlocal
cd /d "%~dp0"
title Member3 one-click stop
echo Stopping member-3 FastAPI and Web dashboard...
echo The C++ server and shared database will not be touched.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\stop-member3-stack.ps1"
echo.
pause
