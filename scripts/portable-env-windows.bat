@echo off
setlocal EnableExtensions
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT=%%~fI"
if not exist "%ROOT%\build\tools\env.cmd" (
  echo [PortableAVM] Portable tools are missing. Run scripts\install-prerequisites-windows.bat first.
  exit /b 1
)
endlocal & call "%ROOT%\build\tools\env.cmd"
