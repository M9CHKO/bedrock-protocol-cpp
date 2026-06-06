param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$BuildDir = "",
    [string]$InstallDir = "",
    [switch]$NoInstall
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $BuildDir) { $BuildDir = Join-Path $Root "build" }
if (-not $InstallDir) { $InstallDir = Join-Path $Root "install" }

$PossibleBash = @(
    (Join-Path $Root "..\_deps\msys64\usr\bin\bash.exe"),
    "C:\msys64\usr\bin\bash.exe",
    "C:\msys64\mingw64\bin\bash.exe"
)

$Bash = $PossibleBash | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Bash) {
    throw "MSYS2 bash was not found. Install MSYS2 or keep the bundled _deps\msys64 next to this repository."
}

$RootUnix = (& $Bash -lc "cygpath -u '$Root'").Trim()
$BuildUnix = (& $Bash -lc "cygpath -u '$BuildDir'").Trim()
$InstallUnix = (& $Bash -lc "cygpath -u '$InstallDir'").Trim()

$InstallArg = if ($NoInstall) { "--no-install" } else { "" }

& $Bash -lc "cd '$RootUnix' && BUILD_DIR='$BuildUnix' INSTALL_DIR='$InstallUnix' BUILD_TYPE='$Config' ./scripts/build.sh $InstallArg"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Bedrock Protocol C++ built successfully."
if (-not $NoInstall) {
    Write-Host "Installed to: $InstallDir"
}
