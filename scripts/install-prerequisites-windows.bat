@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT=%%~fI"
set "TOOLS=%ROOT%\build\tools"

set "ARCH=x64"
if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ARCH=arm64"
if /I "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "ARCH=arm64"
if /I "%~1"=="x64" set "ARCH=x64"
if /I "%~1"=="arm64" set "ARCH=arm64"

echo [PortableAVM] Bootstrapping portable Git, CMake and Python under:
echo   %TOOLS%
echo.

where powershell.exe >nul 2>nul || (
  echo [PortableAVM] Windows PowerShell was not found.
  exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%bootstrap-windows-tools.ps1" -Architecture %ARCH%
if errorlevel 1 exit /b %errorlevel%

call "%TOOLS%\env.cmd"

echo.
echo [PortableAVM] Looking for an installed Visual Studio 2026 instance...
call :find_vs2026
if defined VSINSTALL goto :vs_ready

echo [PortableAVM] Visual Studio 2026 was not found by any detection method.
echo [PortableAVM] Downloading the official Visual Studio 2026 Build Tools bootstrapper...
set "VSBOOT=%TEMP%\PortableAVM-vs2026-buildtools.exe"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -UseBasicParsing -Uri 'https://aka.ms/vs/18/stable/vs_buildtools.exe' -OutFile '%VSBOOT%'"
if errorlevel 1 exit /b %errorlevel%

echo [PortableAVM] Installing Visual Studio 2026 Build Tools with Desktop C++ workload...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$p=Start-Process -FilePath '%VSBOOT%' -Verb RunAs -Wait -PassThru -ArgumentList '--wait','--passive','--norestart','--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended'; exit $p.ExitCode"
if errorlevel 1 exit /b %errorlevel%
del /q "%VSBOOT%" >nul 2>nul

call :find_vs2026
if not defined VSINSTALL (
  echo [PortableAVM] Visual Studio 2026 setup completed, but no usable installation directory could be located.
  echo [PortableAVM] Run scripts\diagnose-vs2026.bat and inspect the probe output.
  exit /b 1
)

goto :vs_ready

:find_vs2026
set "VSINSTALL="
for /f "usebackq delims=" %%I in (`powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%find-vs2026.ps1"`) do (
  if not defined VSINSTALL set "VSINSTALL=%%I"
)
exit /b 0

:vs_ready
echo [PortableAVM] Visual Studio 2026: %VSINSTALL%
set "VSDEVCMD=%VSINSTALL%\Common7\Tools\VsDevCmd.bat"
set "VCVARSALL=%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat"

rem Create a wrapper that affects only the cmd.exe process which CALLs it.
rem No machine/user environment variables are changed.
>"%TOOLS%\vs2026-env.cmd" echo @echo off
if exist "%VSDEVCMD%" (
  >>"%TOOLS%\vs2026-env.cmd" echo call "%VSDEVCMD%" -no_logo -arch=%ARCH%
) else if exist "%VCVARSALL%" (
  >>"%TOOLS%\vs2026-env.cmd" echo call "%VCVARSALL%" %ARCH%
) else (
  echo [PortableAVM] Visual Studio 2026 is installed and will be used, but no developer environment script was found.
  echo [PortableAVM] Installation: %VSINSTALL%
  echo [PortableAVM] Repair that VS installation if C++ tools are needed.
  goto :done
)

call "%TOOLS%\vs2026-env.cmd" >nul 2>nul
where cl.exe >nul 2>nul
if errorlevel 1 (
  echo [PortableAVM] Visual Studio 2026 was found and selected.
  echo [PortableAVM] cl.exe is not present in its current developer environment.
  echo [PortableAVM] Add Desktop development with C++ to this same VS installation if required.
) else (
  for /f "delims=" %%I in ('where cl.exe') do (
    echo [PortableAVM] MSVC selected by Visual Studio: %%I
    goto :compiler_reported
  )
)

:compiler_reported
:done
echo.
echo [PortableAVM] Prerequisites are ready.
echo [PortableAVM] Git/CMake/Python remain project-local under build\tools.
echo [PortableAVM] Visual Studio/MSVC version numbers are not hard-coded.
echo [PortableAVM] Detection accepts VS 2026 from custom paths, Installer state, registry, vswhere, or default folders.
exit /b 0
