param([switch]$SkipNative)
$ErrorActionPreference='Stop'
if (-not $SkipNative) { & (Join-Path $PSScriptRoot 'build-release.ps1'); if ($LASTEXITCODE) { exit $LASTEXITCODE } }
$env:CARGO_HOME='D:\DevCache\unleashed-cargo'
$env:CARGO_TARGET_DIR='D:\DevCache\unleashed-cargo-target'
$env:npm_config_cache='D:\DevCache\npm-unleashed'
Push-Location (Join-Path $PSScriptRoot 'launcher')
try {
    & npm.cmd ci --no-audit --no-fund
    if ($LASTEXITCODE) { throw 'Launcher dependency installation failed' }
    & npm.cmd run tauri -- build --no-bundle
    if ($LASTEXITCODE) { throw 'Launcher build failed' }
    Copy-Item -LiteralPath (Join-Path $env:CARGO_TARGET_DIR 'release\unleashed-launcher.exe') -Destination (Join-Path $PSScriptRoot 'build\Release\unleashed-launcher.exe')
} finally { Pop-Location }
