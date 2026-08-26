@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "ARCH=x64"
if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ARCH=arm64"
if /I "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "ARCH=arm64"

set "CLEAN=0"
set "SKIP_DEPS=0"
set "PAVM_RC=0"

:parse
if "%~1"=="" goto :parsed
if /I "%~1"=="x64" set "ARCH=x64"& shift & goto :parse
if /I "%~1"=="arm64" set "ARCH=arm64"& shift & goto :parse
if /I "%~1"=="-Clean" set "CLEAN=1"& shift & goto :parse
if /I "%~1"=="--clean" set "CLEAN=1"& shift & goto :parse
if /I "%~1"=="-SkipDependencyBuild" set "SKIP_DEPS=1"& shift & goto :parse
if /I "%~1"=="--skip-dependency-build" set "SKIP_DEPS=1"& shift & goto :parse
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="--help" goto :usage

echo [PortableAVM] Unknown argument: %~1
set "PAVM_RC=2"
goto :usage_and_finish

:parsed
echo [PortableAVM] Target architecture: %ARCH%

call "%ROOT%\scripts\install-prerequisites-windows.bat" %ARCH%
if errorlevel 1 (
  set "PAVM_RC=%errorlevel%"
  goto :finish
)

if not exist "%ROOT%\build\tools\env.cmd" (
  echo [PortableAVM] Missing build\tools\env.cmd after bootstrap.
  set "PAVM_RC=1"
  goto :finish
)
call "%ROOT%\build\tools\env.cmd"
if errorlevel 1 (
  set "PAVM_RC=%errorlevel%"
  goto :finish
)

if exist "%ROOT%\build\tools\vs2026-env.cmd" (
  call "%ROOT%\build\tools\vs2026-env.cmd"
  if errorlevel 1 (
    set "PAVM_RC=%errorlevel%"
    goto :finish
  )
)

where cl.exe >nul 2>nul
if errorlevel 1 (
  echo [PortableAVM] Visual Studio 2026 was detected, but cl.exe is unavailable.
  echo [PortableAVM] Add the Desktop development with C++ workload to that VS 2026 installation.
  set "PAVM_RC=1"
  goto :finish
)

set "PS_ARGS=-Architecture %ARCH%"
if "%CLEAN%"=="1" set "PS_ARGS=%PS_ARGS% -Clean"
if "%SKIP_DEPS%"=="1" set "PS_ARGS=%PS_ARGS% -SkipDependencyBuild"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\build-windows.ps1" %PS_ARGS%
set "PAVM_RC=%errorlevel%"
goto :finish

:usage
echo Usage: build-windows.bat [x64^|arm64] [-Clean] [-SkipDependencyBuild]
set "PAVM_RC=0"
goto :finish

:usage_and_finish
echo Usage: build-windows.bat [x64^|arm64] [-Clean] [-SkipDependencyBuild]
goto :finish

:finish
echo.
if "%PAVM_RC%"=="0" (
  echo [PortableAVM] Build finished successfully.
) else (
  echo [PortableAVM] Build failed with exit code %PAVM_RC%.
)
echo [PortableAVM] Press any key to close this window.
pause >nul
exit /b %PAVM_RC%
