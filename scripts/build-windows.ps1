param(
    [ValidateSet('x64','arm64')]
    [string]$Architecture = 'x64',
    [switch]$Clean,
    [switch]$SkipDependencyBuild
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$BuildRoot = Join-Path $Root 'build'
$ToolsRoot = Join-Path $BuildRoot 'tools'
$DepsRoot = Join-Path $BuildRoot 'deps'
$OutRoot = Join-Path $BuildRoot ("windows-" + $Architecture)
$DistRoot = Join-Path $Root ("dist-windows-" + $Architecture)

$Git = Join-Path $ToolsRoot 'git\cmd\git.exe'
$CMake = Join-Path $ToolsRoot 'cmake\bin\cmake.exe'
$Python = Join-Path $ToolsRoot 'python\python.exe'

function Step([string]$Message) { Write-Host "[PortableAVM] $Message" }
function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label was not found: $Path" }
}
function Run([string]$Exe, [string[]]$Arguments, [string]$WorkingDirectory = $Root) {
    $DisplayCommand = (Split-Path -Leaf $Exe) + ' ' + ($Arguments -join ' ')
    Step $DisplayCommand
    Push-Location $WorkingDirectory
    try {
        & $Exe @Arguments
        if ($LASTEXITCODE -ne 0) {
            $ExitCode = $LASTEXITCODE
            throw "Command failed with exit code ${ExitCode}: $DisplayCommand"
        }
    } finally { Pop-Location }
}
function Run-LoggedWithRetry(
    [string]$Exe,
    [string[]]$Arguments,
    [string]$WorkingDirectory,
    [string]$LogPath,
    [int]$Attempts = 2
) {
    $DisplayCommand = (Split-Path -Leaf $Exe) + ' ' + ($Arguments -join ' ')
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null
    Remove-Item -LiteralPath $LogPath -Force -ErrorAction SilentlyContinue
    for ($Attempt = 1; $Attempt -le $Attempts; $Attempt++) {
        Step "$DisplayCommand (attempt $Attempt/$Attempts; log: $LogPath)"
        Push-Location $WorkingDirectory
        try {
            & $Exe @Arguments 2>&1 | Tee-Object -FilePath $LogPath -Append
            $ExitCode = $LASTEXITCODE
        } finally { Pop-Location }
        if ($ExitCode -eq 0) { return }
        if ($Attempt -lt $Attempts) {
            Write-Warning "Command failed with exit code $ExitCode. Retrying after a short delay; completed dependency checkouts will be reused."
            Start-Sleep -Seconds 2
        }
    }
    Write-Host ''
    Write-Host '[PortableAVM] ===== dependency sync failure tail ====='
    if (Test-Path -LiteralPath $LogPath) {
        Get-Content -LiteralPath $LogPath -Tail 160 | ForEach-Object { Write-Host $_ }
    }
    Write-Host '[PortableAVM] ========================================'
    throw "Command failed after $Attempts attempts with exit code ${ExitCode}: $DisplayCommand ; full log: $LogPath"
}
function Clone-Or-Update([string]$Url, [string]$Path, [string]$Ref) {
    if (-not (Test-Path (Join-Path $Path '.git'))) {
        if (Test-Path $Path) { Remove-Item -Recurse -Force $Path }
        Run $Git @('clone','--filter=blob:none',$Url,$Path) $DepsRoot
    }
    Run $Git @('-C',$Path,'fetch','--tags','--prune','origin') $Root
    Run $Git @('-C',$Path,'checkout','--force',$Ref) $Root
    Run $Git @('-C',$Path,'submodule','update','--init','--recursive') $Root
}
function Copy-License([string]$Source, [string]$Name) {
    if (Test-Path $Source) { Copy-Item -Force $Source (Join-Path $DistRoot "Data\Licenses\$Name") }
}

Require-File $Git 'Portable Git'
Require-File $CMake 'Portable CMake'
Require-File $Python 'Portable Python'

$PythonDir = Split-Path -Parent $Python
$Python3 = Join-Path $PythonDir 'python3.exe'
if (-not (Test-Path -LiteralPath $Python3 -PathType Leaf)) {
    Copy-Item -LiteralPath $Python -Destination $Python3 -Force
    Step "Created project-local python3.exe alias: $Python3"
}

# Always put the project-local Python first. Skia's GN scripts invoke python3
# by name, and WindowsApps may expose a non-functional Microsoft Store alias.
$env:PATH = $PythonDir + ';' + (($env:PATH -split ';' | Where-Object { $_ -and ($_ -ne $PythonDir) }) -join ';')
$env:PYTHONHOME = $PythonDir
$env:PYTHONUTF8 = '1'
$ResolvedPython3 = (Get-Command python3.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $ResolvedPython3) { throw 'python3.exe could not be resolved after configuring the portable Python PATH.' }
if (-not ([System.IO.Path]::GetFullPath($ResolvedPython3).Equals([System.IO.Path]::GetFullPath($Python3), [System.StringComparison]::OrdinalIgnoreCase))) {
    throw "python3.exe resolved outside the portable toolchain: $ResolvedPython3"
}
Step "Skia python3: $ResolvedPython3"

# Python's embeddable Windows distribution disables site.py by default in its
# pythonXY._pth file. Skia's tools/git-sync-deps expects normal Python startup
# semantics and calls the site-provided builtins.exit(). Repair older build
# caches in place so updating PortableAVM does not require deleting build/tools.
$PythonPth = Get-ChildItem -LiteralPath (Split-Path -Parent $Python) -Filter 'python*._pth' -File -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $PythonPth) { throw 'Portable Python ._pth file was not found.' }
$PthLines = Get-Content -LiteralPath $PythonPth.FullName
$PthChanged = $false
$PthHasSite = $false
$PthFixed = foreach ($Line in $PthLines) {
    if ($Line -match '^\s*import\s+site\s*$') {
        $PthHasSite = $true
        $Line
    } elseif ($Line -match '^\s*#\s*import\s+site\s*$') {
        $PthHasSite = $true
        $PthChanged = $true
        'import site'
    } else {
        $Line
    }
}
if (-not $PthHasSite) {
    $PthFixed = @($PthFixed) + 'import site'
    $PthChanged = $true
}
if ($PthChanged) {
    Set-Content -LiteralPath $PythonPth.FullName -Encoding ASCII -Value $PthFixed
    Step "Enabled Python site initialization in $($PythonPth.Name)."
}
Run $Python @('-c',"import builtins, site; assert callable(getattr(builtins, 'exit', None)), 'builtins.exit is unavailable'") $Root
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'cl.exe is not available. Run build-windows.bat from the project root so the VS 2026 environment is initialized.'
}

if ($Clean) {
    Step 'Cleaning generated Windows build and dist directories.'
    Remove-Item -Recurse -Force $OutRoot,$DistRoot -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $BuildRoot,$DepsRoot,$OutRoot | Out-Null

Step 'Validating active packaged source tree before downloading dependencies.'
$ValidationLog = Join-Path $BuildRoot 'logs\source-validation-windows.txt'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ValidationLog) | Out-Null
Run $Python @((Join-Path $Root 'scripts\validate-source.py'),'--root',$Root,'--report',$ValidationLog) $Root

$Skia = Join-Path $DepsRoot 'skia'
$SDL = Join-Path $DepsRoot 'sdl3'
$Curl = Join-Path $DepsRoot 'curl'

$SkiaRef = if ($env:PAVM_SKIA_REF) { $env:PAVM_SKIA_REF } else { 'chrome/m126' }
$SDLRef = if ($env:PAVM_SDL_REF) { $env:PAVM_SDL_REF } else { 'release-3.2.0' }
$CurlRef = if ($env:PAVM_CURL_REF) { $env:PAVM_CURL_REF } else { 'curl-8_11_1' }

Clone-Or-Update 'https://skia.googlesource.com/skia.git' $Skia $SkiaRef
Clone-Or-Update 'https://github.com/libsdl-org/SDL.git' $SDL $SDLRef
Clone-Or-Update 'https://github.com/curl/curl.git' $Curl $CurlRef

# Older PortableAVM builds allowed Skia's activate-emsdk helper to persist
# EMSDK-related values. Do not inherit those project-local values into this
# build process. Persistent user settings are left untouched.
$LegacyEmsdkRoot = (Join-Path $Skia 'third_party\externals\emsdk')
foreach ($Name in @('EMSDK','EMSDK_NODE','EMSDK_PYTHON')) {
    $Value = [Environment]::GetEnvironmentVariable($Name, 'Process')
    if ($Value -and $Value.StartsWith($LegacyEmsdkRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        Step "Ignoring legacy process environment variable $Name from an older build."
        Remove-Item ("Env:" + $Name) -ErrorAction SilentlyContinue
    }
}
$env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and -not $_.StartsWith($LegacyEmsdkRoot, [System.StringComparison]::OrdinalIgnoreCase) }) -join ';'

$SkiaCpu = if ($Architecture -eq 'arm64') { 'arm64' } else { 'x64' }
$SkiaOut = Join-Path $Skia ("out\PortableAVM-" + $Architecture)
$SkiaLib = Join-Path $SkiaOut 'skia.lib'

if (-not $SkipDependencyBuild -or -not (Test-Path $SkiaLib)) {
    # git-sync-deps launches one checkout thread per dependency. A transient
    # network failure in any thread makes the upstream script finish with the
    # generic "Thread failure detected" exception. Do not repeat that expensive
    # network operation on every incremental PortableAVM build.
    $SkiaDepsFile = Join-Path $Skia 'DEPS'
    Require-File $SkiaDepsFile 'Skia DEPS'
    $SkiaDepsFingerprint = (Get-FileHash -LiteralPath $SkiaDepsFile -Algorithm SHA256).Hash.ToLowerInvariant()
    $SkiaDepsStamp = Join-Path $DepsRoot 'skia-deps.sha256'
    $SkiaDepsLog = Join-Path $BuildRoot 'logs\skia-git-sync-deps.log'
    $RequiredSkiaDeps = @(
        (Join-Path $Skia 'buildtools'),
        (Join-Path $Skia 'third_party\externals\expat'),
        (Join-Path $Skia 'third_party\externals\libjpeg-turbo'),
        (Join-Path $Skia 'third_party\externals\libpng'),
        (Join-Path $Skia 'third_party\externals\libwebp'),
        (Join-Path $Skia 'third_party\externals\wuffs'),
        (Join-Path $Skia 'third_party\externals\zlib')
    )
    $RequiredSkiaDepsPresent = -not ($RequiredSkiaDeps | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Container) } | Select-Object -First 1)
    $StampedFingerprint = if (Test-Path -LiteralPath $SkiaDepsStamp -PathType Leaf) { (Get-Content -LiteralPath $SkiaDepsStamp -Raw).Trim().ToLowerInvariant() } else { '' }
    $SkiaDepsReady = $RequiredSkiaDepsPresent -and ($StampedFingerprint -eq $SkiaDepsFingerprint)

    # Older PortableAVM builds predate the dependency stamp. A completed skia.lib
    # plus all bundled dependencies proves that this checkout was already synced;
    # bootstrap the stamp instead of contacting every dependency repository again.
    if (-not $SkiaDepsReady -and -not (Test-Path -LiteralPath $SkiaDepsStamp -PathType Leaf) -and (Test-Path -LiteralPath $SkiaLib -PathType Leaf) -and $RequiredSkiaDepsPresent) {
        Set-Content -LiteralPath $SkiaDepsStamp -Encoding ASCII -NoNewline -Value $SkiaDepsFingerprint
        $SkiaDepsReady = $true
        Step 'Adopted existing completed Skia dependency checkout; no dependency network sync is required.'
    }

    if ($SkiaDepsReady) {
        Step 'Skia dependencies already match the current DEPS fingerprint; skipping git-sync-deps.'
    } else {
        Step 'Synchronizing Skia dependencies without Emscripten activation.'
        # Skia's git-sync-deps normally runs bin/activate-emsdk. On Windows that
        # uses --permanent and modifies the current user's environment. PortableAVM
        # is intentionally portable, and CPU-raster Skia does not need Emscripten.
        $PreviousSkipEmsdk = $env:GIT_SYNC_DEPS_SKIP_EMSDK
        $env:GIT_SYNC_DEPS_SKIP_EMSDK = '1'
        try {
            Run-LoggedWithRetry $Python @('tools\git-sync-deps') $Skia $SkiaDepsLog 2
            Set-Content -LiteralPath $SkiaDepsStamp -Encoding ASCII -NoNewline -Value $SkiaDepsFingerprint
        } finally {
            if ($null -eq $PreviousSkipEmsdk) { Remove-Item Env:GIT_SYNC_DEPS_SKIP_EMSDK -ErrorAction SilentlyContinue }
            else { $env:GIT_SYNC_DEPS_SKIP_EMSDK = $PreviousSkipEmsdk }
        }
    }

    # Use Skia's own official fetch helpers rather than depot_tools wrappers.
    # This avoids requiring depot_tools bootstrap files such as python3_bin_reldir.txt.
    $GnExe = Join-Path $Skia 'bin\gn.exe'
    # Skia's bin/fetch-ninja intentionally installs Ninja under third_party/ninja.
    # Do not assume bin/ninja.exe: that path is not used by chrome/m126.
    $NinjaCandidates = @(
        (Join-Path $Skia 'third_party\ninja\ninja.exe'),
        (Join-Path $Skia 'bin\ninja.exe')
    )
    $NinjaExe = $NinjaCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not (Test-Path -LiteralPath $GnExe -PathType Leaf)) {
        Step 'Fetching Skia GN binary.'
        Run $Python @('bin\fetch-gn') $Skia
    }
    if (-not $NinjaExe) {
        Step 'Fetching Skia Ninja binary.'
        Run $Python @('bin\fetch-ninja') $Skia
        $NinjaExe = $NinjaCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    }
    Require-File $GnExe 'Skia GN'
    if (-not $NinjaExe) {
        throw ('Skia Ninja was not found after fetch-ninja. Checked: ' + ($NinjaCandidates -join ', '))
    }
    Require-File $NinjaExe 'Skia Ninja'
    Step "Using Skia Ninja: $NinjaExe"

    # Avoid passing GN's quoted string values through PowerShell/native argv.
    # PowerShell can strip the quotes around target_cpu, turning target_cpu="x64"
    # into the invalid GN expression target_cpu=x64. Write args.gn directly and
    # let `gn gen` read it from the output directory instead.
    New-Item -ItemType Directory -Force -Path $SkiaOut | Out-Null
    $GnArgsFile = Join-Path $SkiaOut 'args.gn'
    $GnArgsLines = @(
        'is_official_build=true',
        'is_debug=false',
        'skia_enable_gpu=false',
        # is_official_build defaults these third-party libraries to system
        # installations. PortableAVM must not depend on machine-wide SDKs, so
        # build the copies fetched by Skia's git-sync-deps instead.
        'skia_use_system_libjpeg_turbo=false',
        'skia_use_system_libpng=false',
        'skia_use_system_libwebp=false',
        'skia_use_system_zlib=false',
        'skia_use_system_expat=false',
        # PortableAVM only uses Skia as a raster UI renderer; PDF output is not
        # needed and would pull in additional third-party build surface.
        'skia_enable_pdf=false',
        ('target_cpu="' + $SkiaCpu + '"')
    )
    Set-Content -LiteralPath $GnArgsFile -Encoding ASCII -Value $GnArgsLines
    Step "Skia GN args file: $GnArgsFile"
    Get-Content -LiteralPath $GnArgsFile | ForEach-Object { Write-Host "  $_" }
    Run $GnExe @('gen',$SkiaOut) $Skia

    # Preserve the complete Skia build output. Ninja's final
    # "subcommand failed" line is only a summary; the useful compiler
    # diagnostic can be hundreds of lines earlier when building in parallel.
    $LogRoot = Join-Path $BuildRoot 'logs'
    New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
    $SkiaNinjaLog = Join-Path $LogRoot 'skia-ninja.log'
    $SkiaNinjaVerboseLog = Join-Path $LogRoot 'skia-ninja-failure-verbose.log'
    Remove-Item -LiteralPath $SkiaNinjaLog,$SkiaNinjaVerboseLog -Force -ErrorAction SilentlyContinue

    Step "Building Skia; full output: $SkiaNinjaLog"
    Push-Location $Skia
    try {
        & $NinjaExe '-C' $SkiaOut 'skia' 2>&1 | Tee-Object -FilePath $SkiaNinjaLog
        $NinjaExit = $LASTEXITCODE
    } finally { Pop-Location }

    if ($NinjaExit -ne 0) {
        Write-Warning "Skia parallel build failed with exit code $NinjaExit. Re-running one job verbosely to expose the actual compiler error."
        Push-Location $Skia
        try {
            & $NinjaExe '-C' $SkiaOut '-j1' '-v' 'skia' 2>&1 | Tee-Object -FilePath $SkiaNinjaVerboseLog
            $VerboseExit = $LASTEXITCODE
        } finally { Pop-Location }

        Write-Host ''
        Write-Host '[PortableAVM] ===== Skia compiler failure tail ====='
        if (Test-Path -LiteralPath $SkiaNinjaVerboseLog) {
            Get-Content -LiteralPath $SkiaNinjaVerboseLog -Tail 160 | ForEach-Object { Write-Host $_ }
        } elseif (Test-Path -LiteralPath $SkiaNinjaLog) {
            Get-Content -LiteralPath $SkiaNinjaLog -Tail 160 | ForEach-Object { Write-Host $_ }
        }
        Write-Host '[PortableAVM] ======================================'
        throw "Skia build failed. Full logs: $SkiaNinjaLog ; $SkiaNinjaVerboseLog"
    }
}
if (-not (Test-Path $SkiaLib)) { throw "Skia library was not produced: $SkiaLib" }

$Generator = 'Visual Studio 18 2026'
$CmakeArgs = @(
    '-S',$Root,
    '-B',$OutRoot,
    '-G',$Generator,
    '-A',$(if ($Architecture -eq 'arm64') { 'ARM64' } else { 'x64' }),
    '-DPAVM_BUILD_GUI=ON',
    '-DPAVM_BUILD_TESTS=ON',
    '-DPAVM_USE_BUNDLED_SDL=ON',
    '-DPAVM_USE_BUNDLED_CURL=ON',
    "-DPAVM_SKIA_ROOT=$Skia",
    "-DPAVM_SKIA_OUT=$SkiaOut",
    "-DPAVM_SDL_SOURCE_DIR=$SDL",
    "-DPAVM_CURL_SOURCE_DIR=$Curl"
)
Step 'Configuring PortableAVM with CMake.'
Run $CMake $CmakeArgs $Root

Step 'Building PortableAVM Release.'
Run $CMake @('--build',$OutRoot,'--config','Release','--parallel') $Root

Step 'Running core tests.'
Run (Join-Path $ToolsRoot 'cmake\bin\ctest.exe') @('--test-dir',$OutRoot,'-C','Release','--output-on-failure') $Root

Step 'Installing portable distribution.'
Remove-Item -Recurse -Force $DistRoot -ErrorAction SilentlyContinue
Run $CMake @('--install',$OutRoot,'--config','Release','--prefix',$DistRoot) $Root
New-Item -ItemType Directory -Force -Path (Join-Path $DistRoot 'Data\Licenses') | Out-Null
Copy-License (Join-Path $Skia 'LICENSE') 'Skia-LICENSE.txt'
Copy-License (Join-Path $SDL 'LICENSE.txt') 'SDL3-LICENSE.txt'
Copy-License (Join-Path $Curl 'COPYING') 'curl-COPYING.txt'

$qt = Get-ChildItem -Path $DistRoot -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^Qt[56].*\.dll$' }
if ($qt) { throw 'Qt runtime files unexpectedly appeared in the distribution.' }

Step "Build complete: $DistRoot"
