param([switch]$TestServer,[switch]$Native)
$ErrorActionPreference = 'Stop'
$runtime = Join-Path $PSScriptRoot 'build\Release'
$program = Join-Path $runtime $(if($Native -or $TestServer){'UNDMA_V2.exe'}else{'unleashed-launcher.exe'})
if (-not (Test-Path -LiteralPath $program)) { throw 'Build V2 with .\build-launcher.ps1 first' }
$runtimeArgs = if ($TestServer) { @('--test-server', '--test-server-port', '19560') } else { @() }
Push-Location $runtime
try { & $program @runtimeArgs }
finally { Pop-Location }
