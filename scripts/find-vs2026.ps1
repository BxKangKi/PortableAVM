param(
    [switch]$VerboseProbe
)

$ErrorActionPreference = 'SilentlyContinue'

function Trace([string]$Message) {
    if ($VerboseProbe) { Write-Host "[PortableAVM][VS probe] $Message" }
}

function Normalize-ExistingPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    $expanded = [Environment]::ExpandEnvironmentVariables($Path.Trim('"',' '))
    if (-not (Test-Path -LiteralPath $expanded -PathType Container)) { return $null }
    try { return (Resolve-Path -LiteralPath $expanded).Path } catch { return $expanded }
}

function Test-VS2026Path([string]$Path) {
    $p = Normalize-ExistingPath $Path
    if (-not $p) { return $null }

    $devcmd = Join-Path $p 'Common7\Tools\VsDevCmd.bat'
    $vcvars = Join-Path $p 'VC\Auxiliary\Build\vcvarsall.bat'
    $devenv = Join-Path $p 'Common7\IDE\devenv.exe'
    $msbuild = Join-Path $p 'MSBuild\Current\Bin\MSBuild.exe'

    # An installed VS instance should have at least one of these core entry points.
    if ((Test-Path $devcmd) -or (Test-Path $vcvars) -or (Test-Path $devenv) -or (Test-Path $msbuild)) {
        return $p
    }
    return $null
}

function Emit-First([System.Collections.Generic.List[string]]$Candidates) {
    $seen = @{}
    foreach ($candidate in $Candidates) {
        $p = Test-VS2026Path $candidate
        if (-not $p) { continue }
        $key = $p.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        Trace "accepted: $p"
        Write-Output $p
        exit 0
    }
    exit 1
}

$candidates = [System.Collections.Generic.List[string]]::new()

# 1. If this cmd/PowerShell already came from a VS 2026 developer environment,
#    trust it first. Do not care which exact MSVC toolset version it selected.
if ($env:VSINSTALLDIR) {
    if (($env:VisualStudioVersion -like '18.*') -or ($env:VSINSTALLDIR -match '[\\/]18[\\/]')) {
        Trace "environment VSINSTALLDIR=$env:VSINSTALLDIR"
        $candidates.Add($env:VSINSTALLDIR)
    }
}

# 2. Visual Studio Installer instance state. This is particularly useful for
#    custom installation paths and avoids depending on vswhere behavior.
$instanceRoots = @(
    (Join-Path $env:ProgramData 'Microsoft\VisualStudio\Packages\_Instances'),
    (Join-Path $env:LOCALAPPDATA 'Microsoft\VisualStudio\Packages\_Instances')
) | Where-Object { $_ -and (Test-Path $_) }

foreach ($root in $instanceRoots) {
    Trace "instance root: $root"
    Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        $state = Join-Path $_.FullName 'state.json'
        if (-not (Test-Path $state)) { return }
        try {
            $j = Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
            $version = [string]$j.installationVersion
            $path = [string]$j.installationPath
            if ($version -like '18.*' -and $path) {
                Trace "installer state: $version -> $path"
                $candidates.Add($path)
            }
        } catch {}
    }
}

# 3. vswhere, but deliberately without workload/component requirements.
$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
) | Where-Object { $_ -and (Test-Path $_) }

foreach ($vswhere in ($vswhereCandidates | Select-Object -Unique)) {
    Trace "vswhere: $vswhere"
    try {
        $lines = & $vswhere -all -products '*' -prerelease -version '[18.0,19.0)' -property installationPath 2>$null
        foreach ($line in $lines) {
            if ($line) { $candidates.Add([string]$line) }
        }
    } catch {}

    # Fallback: query all instances and pair installationVersion/path ourselves.
    try {
        $json = & $vswhere -all -products '*' -prerelease -format json 2>$null | Out-String
        if ($json.Trim()) {
            foreach ($inst in ($json | ConvertFrom-Json)) {
                if (([string]$inst.installationVersion) -like '18.*' -and $inst.installationPath) {
                    $candidates.Add([string]$inst.installationPath)
                }
            }
        }
    } catch {}
}

# 4. Registry SxS keys. Query both registry views because VS Installer and the
#    shell do not always use the same bitness.
$registryPaths = @(
    'HKLM:\SOFTWARE\Microsoft\VisualStudio\SxS\VS7',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\SxS\VS7',
    'HKCU:\SOFTWARE\Microsoft\VisualStudio\SxS\VS7'
)
foreach ($rk in $registryPaths) {
    try {
        $item = Get-ItemProperty -Path $rk -ErrorAction Stop
        $value = $item.'18.0'
        if ($value) {
            Trace "registry $rk -> $value"
            $candidates.Add([string]$value)
        }
    } catch {}
}

# 5. Microsoft-documented default VS 2026 layout. Scan all editions, including
#    Insiders and BuildTools, and both Program Files roots for compatibility.
$pfRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ } | Select-Object -Unique
foreach ($pf in $pfRoots) {
    $vs18 = Join-Path $pf 'Microsoft Visual Studio\18'
    if (Test-Path $vs18) {
        Trace "default root: $vs18"
        Get-ChildItem -LiteralPath $vs18 -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            $candidates.Add($_.FullName)
        }
    }
}

Emit-First $candidates
