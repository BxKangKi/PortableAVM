@echo off
setlocal EnableExtensions DisableDelayedExpansion
call "%~dp0..\build-windows.bat" %*
exit /b %errorlevel%
