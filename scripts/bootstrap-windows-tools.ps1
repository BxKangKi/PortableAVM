param(
    [ValidateSet('x64','arm64')]
    [string]$Architecture = 'x64',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$BuildDir = Join-Path $Root 'build'
$ToolsDir = Join-Path $BuildDir 'tools'
$DownloadsDir = Join-Path $BuildDir 'downloads'
New-Item -ItemType Directory -Force -Path $ToolsDir,$DownloadsDir | Out-Null

function Write-Step([string]$Message) {
    Write-Host "[PortableAVM] $Message"
}

function Invoke-Download([string]$Uri, [string]$OutFile) {
    Write-Step "Downloading $Uri"
    Invoke-WebRequest -UseBasicParsing -Uri $Uri -OutFile $OutFile
    if (-not (Test-Path $OutFile) -or (Get-Item $OutFile).Length -lt 1024) {
        throw "Download failed or returned an unexpectedly small file: $Uri"
    }
}

function Get-LatestGitPortableAsset([string]$Arch) {
    $release = Invoke-RestMethod -Headers @{ 'User-Agent'='PortableAVM-bootstrap' } -Uri 'https://api.github.com/repos/git-for-windows/git/releases/latest'
    $pattern = if ($Arch -eq 'arm64') { '^PortableGit-.*-arm64\.7z\.exe$' } else { '^PortableGit-.*-64-bit\.7z\.exe$' }
    $asset = $release.assets | Where-Object { $_.name -match $pattern } | Select-Object -First 1
    if (-not $asset) { throw "Could not locate a Portable Git asset for $Arch." }
    return $asset
}

function Get-LatestCMakeZipAsset([string]$Arch) {
    $release = Invoke-RestMethod -Headers @{ 'User-Agent'='PortableAVM-bootstrap' } -Uri 'https://api.github.com/repos/Kitware/CMake/releases/latest'
    $pattern = if ($Arch -eq 'arm64') { '^cmake-.*-windows-arm64\.zip$' } else { '^cmake-.*-windows-x86_64\.zip$' }
    $asset = $release.assets | Where-Object { $_.name -match $pattern } | Select-Object -First 1
    if (-not $asset) { throw "Could not locate a CMake Windows ZIP asset for $Arch." }
    return $asset
}

function Install-PortableGit([string]$Arch) {
    $dest = Join-Path $ToolsDir 'git'
    $exe = Join-Path $dest 'cmd\git.exe'
    if ((Test-Path $exe) -and -not $Force) {
        Write-Step "Portable Git already exists: $exe"
        return
    }
    Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $asset = Get-LatestGitPortableAsset $Arch
    $archive = Join-Path $DownloadsDir $asset.name
    Invoke-Download $asset.browser_download_url $archive
    $hash = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
    Set-Content -Encoding ASCII -Path ($archive + '.sha256') -Value "$hash  $($asset.name)"
    Write-Step "Extracting Portable Git into build/tools/git"
    $p = Start-Process -FilePath $archive -ArgumentList @('-y',"-o$dest") -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0 -or -not (Test-Path $exe)) { throw "Portable Git extraction failed (exit $($p.ExitCode))." }
}

function Install-PortableCMake([string]$Arch) {
    $dest = Join-Path $ToolsDir 'cmake'
    $exe = Join-Path $dest 'bin\cmake.exe'
    if ((Test-Path $exe) -and -not $Force) {
        Write-Step "Portable CMake already exists: $exe"
        return
    }
    $asset = Get-LatestCMakeZipAsset $Arch
    $archive = Join-Path $DownloadsDir $asset.name
    Invoke-Download $asset.browser_download_url $archive
    $hash = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
    Set-Content -Encoding ASCII -Path ($archive + '.sha256') -Value "$hash  $($asset.name)"
    $tmp = Join-Path $BuildDir 'cmake-extract'
    Remove-Item -Recurse -Force $tmp,$dest -ErrorAction SilentlyContinue
    Expand-Archive -LiteralPath $archive -DestinationPath $tmp -Force
    $top = Get-ChildItem -Path $tmp -Directory | Select-Object -First 1
    if (-not $top) { throw 'CMake archive did not contain a top-level directory.' }
    Move-Item -Path $top.FullName -Destination $dest
    Remove-Item -Recurse -Force $tmp
    if (-not (Test-Path $exe)) { throw 'Portable CMake extraction failed.' }
}


function Enable-PortablePythonSite {
    $dest = Join-Path $ToolsDir 'python'
    $pth = Get-ChildItem -LiteralPath $dest -Filter 'python*._pth' -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $pth) {
        throw "Portable Python ._pth file was not found under $dest."
    }

    $lines = Get-Content -LiteralPath $pth.FullName
    $changed = $false
    $hasImportSite = $false
    $fixed = foreach ($line in $lines) {
        if ($line -match '^\s*import\s+site\s*$') {
            $hasImportSite = $true
            $line
        } elseif ($line -match '^\s*#\s*import\s+site\s*$') {
            $hasImportSite = $true
            $changed = $true
            'import site'
        } else {
            $line
        }
    }
    if (-not $hasImportSite) {
        $fixed = @($fixed) + 'import site'
        $changed = $true
    }
    if ($changed) {
        Set-Content -LiteralPath $pth.FullName -Encoding ASCII -Value $fixed
        Write-Step "Enabled Python site initialization in $($pth.Name)."
    }

    $python = Join-Path $dest 'python.exe'
    $python3 = Join-Path $dest 'python3.exe'

    # Skia/GN invokes `python3` on Windows. The official embeddable Python ZIP
    # only contains python.exe, so Windows would otherwise fall through to the
    # Microsoft Store App Execution Alias under WindowsApps. Keep the alias
    # project-local by copying the tiny launcher executable next to python.exe.
    if (-not (Test-Path -LiteralPath $python3 -PathType Leaf)) {
        Copy-Item -LiteralPath $python -Destination $python3 -Force
        Write-Step "Created project-local python3.exe alias: $python3"
    }

    & $python -c "import builtins, site; assert callable(getattr(builtins, 'exit', None)), 'builtins.exit is unavailable'"
    if ($LASTEXITCODE -ne 0) {
        throw 'Portable Python site initialization check failed.'
    }
    & $python3 -c "import sys; assert sys.version_info[:2] == (3, 13)"
    if ($LASTEXITCODE -ne 0) {
        throw 'Portable python3.exe alias check failed.'
    }
}

function Install-PortablePython([string]$Arch) {
    # Pin the 3.13 maintenance line because depot_tools/Skia tooling is well tested with it.
    $version = '3.13.15'
    $dest = Join-Path $ToolsDir 'python'
    $exe = Join-Path $dest 'python.exe'
    if ((Test-Path $exe) -and -not $Force) {
        Write-Step "Portable Python already exists: $exe"
        return
    }
    $suffix = if ($Arch -eq 'arm64') { 'arm64' } else { 'amd64' }
    $name = "python-$version-embeddable-$suffix.zip"
    $uri = "https://www.python.org/ftp/python/$version/$name"
    $archive = Join-Path $DownloadsDir $name
    Invoke-Download $uri $archive
    $hash = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
    Set-Content -Encoding ASCII -Path ($archive + '.sha256') -Value "$hash  $name"
    Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Expand-Archive -LiteralPath $archive -DestinationPath $dest -Force
    if (-not (Test-Path $exe)) { throw 'Portable Python extraction failed.' }
}

Install-PortableGit $Architecture
Install-PortableCMake $Architecture
Install-PortablePython $Architecture
Enable-PortablePythonSite

$envBat = Join-Path $ToolsDir 'env.cmd'
$envText = @"
@echo off
rem Generated by PortableAVM. These variables affect only the calling cmd.exe session.
set "PAVM_PROJECT_ROOT=$Root"
set "PAVM_BUILD_ROOT=$BuildDir"
set "PAVM_TOOLS_ROOT=$ToolsDir"
set "PATH=$ToolsDir\git\cmd;$ToolsDir\git\usr\bin;$ToolsDir\cmake\bin;$ToolsDir\python;%PATH%"
set "PYTHONHOME=$ToolsDir\python"
set "PYTHONUTF8=1"
"@
Set-Content -LiteralPath $envBat -Encoding ASCII -Value $envText

Write-Step 'Portable toolchain is ready.'
Write-Host "  Git:    $ToolsDir\git"
Write-Host "  CMake:  $ToolsDir\cmake"
Write-Host "  Python: $ToolsDir\python"
Write-Host "  Env:    $envBat"
