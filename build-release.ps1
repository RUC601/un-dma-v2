param([switch]$Tests)
$ErrorActionPreference = 'Stop'

$cmake = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Visual Studio Community 2026 CMake was not found at: $cmake"
}

Push-Location $PSScriptRoot
try {
    $configurePreset = if ($Tests) { 'selftests' } else { 'vs2026' }
    $buildPreset = if ($Tests) { 'selftests' } else { 'release' }
    & $cmake --preset $configurePreset
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & $cmake --build --preset $buildPreset
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
