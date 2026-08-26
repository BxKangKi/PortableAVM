@echo off
setlocal EnableExtensions
set "SCRIPT_DIR=%~dp0"
echo [PortableAVM] Visual Studio 2026 detection diagnostics
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%find-vs2026.ps1" -VerboseProbe
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
  echo [PortableAVM] Probe found a Visual Studio 2026 installation.
) else (
  echo [PortableAVM] Probe did not find a Visual Studio 2026 installation.
)
exit /b %RC%
