param([string]$OutputDirectory, [switch]$SkipBuild)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
if (-not $SkipBuild) { & (Join-Path $repo 'build-launcher.ps1'); if ($LASTEXITCODE) { throw 'Product build failed' } }
$config=Get-Content (Join-Path $repo 'launcher\src-tauri\tauri.conf.json') -Raw | ConvertFrom-Json
$version=$config.version
if ($version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$') { throw 'A SemVer version is required' }
$npmVersion=(Get-Content (Join-Path $repo 'launcher\package.json') -Raw | ConvertFrom-Json).version
$cargoVersion=[regex]::Match((Get-Content (Join-Path $repo 'launcher\src-tauri\Cargo.toml') -Raw),'(?m)^version = "([^"]+)"').Groups[1].Value
if ($npmVersion -ne $version -or $cargoVersion -ne $version) { throw 'Launcher package versions differ' }
$runtime=Join-Path $repo 'build\Release'
$native=Get-Item -LiteralPath (Join-Path $runtime 'UNDMA_V2.exe')
if ($native.VersionInfo.ProductVersion -ne $version) { throw 'Native and launcher versions differ' }
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $repo ('dist\'+$version) }
$output=[IO.Path]::GetFullPath($OutputDirectory)
if ((Test-Path -LiteralPath $output) -and @(Get-ChildItem -LiteralPath $output -Force).Count) { throw 'Package output must be empty; choose a new directory' }
New-Item -ItemType Directory -Force $output | Out-Null
$files=@(Get-ChildItem -LiteralPath $runtime -File | Where-Object { $_.Name -in @('UNDMA_V2.exe','unleashed-launcher.exe','info.db') -or $_.Extension -eq '.dll' })
foreach ($file in $files) { Copy-Item -LiteralPath $file.FullName -Destination $output }
Copy-Item -LiteralPath (Join-Path $runtime 'assets') -Destination (Join-Path $output 'assets') -Recurse
$voiceRoot=Join-Path $runtime 'alert-voice'
$voiceDestination=Join-Path $output 'alert-voice'
foreach($folder in @('kokoro-int8-multi-lang-v1_1','sherpa-onnx-v2.0.1-win-x64-shared-MD-Release')) {
    if(-not (Test-Path -LiteralPath (Join-Path $voiceRoot $folder))) { throw 'Offline Alert resources are missing; run tools/setup-alert-voice.ps1 first' }
    New-Item -ItemType Directory -Force $voiceDestination | Out-Null
    Copy-Item -LiteralPath (Join-Path $voiceRoot $folder) -Destination (Join-Path $voiceDestination $folder) -Recurse
}
foreach($license in @('ESPEAK-NG-DATA-LICENSE.txt','ONNX-RUNTIME-LICENSE.txt','SHERPA-ONNX-LICENSE.txt','SOURCES.txt')) {
    Copy-Item -LiteralPath (Join-Path $voiceRoot $license) -Destination (Join-Path $voiceDestination $license)
}
$manifestFiles=@(Get-ChildItem -LiteralPath $output -File -Recurse | Sort-Object FullName | ForEach-Object {
    @{ path=[IO.Path]::GetRelativePath($output,$_.FullName).Replace('\','/'); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
if (-not ($manifestFiles.path -contains 'unleashed-launcher.exe')) { throw 'Launcher is missing from runtime' }
@{ version=$version; files=$manifestFiles } | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $output 'portable-manifest.json') -Encoding utf8NoBOM
$zip=Join-Path (Split-Path $output -Parent) ('Unleashed-V2-'+$version+'-windows-x86_64.zip')
if (Test-Path -LiteralPath $zip) { throw 'ZIP already exists; choose a new output directory' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($output,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
$signingFile=Join-Path ([Environment]::GetFolderPath('Desktop')) 'UnleashedV2-signing\unleashed-v2.key'
if (-not (Test-Path -LiteralPath $signingFile)) { throw 'The desktop signing key is missing' }
& (Join-Path $repo 'launcher\node_modules\.bin\tauri.cmd') signer sign -f $signingFile --app-version $version $zip
if ($LASTEXITCODE) { throw 'Package signing failed' }
$signature=(Get-Content -LiteralPath ($zip+'.sig') -Raw).Trim()
$manifest=@{version=$version;notes='UN V2';pub_date=[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ');platforms=@{'windows-x86_64'=@{url=('https://github.com/RUC601/un-dma-v2/releases/download/v'+$version+'/'+[IO.Path]::GetFileName($zip));signature=$signature}}}
$manifest | ConvertTo-Json -Depth 8 | Set-Content (Join-Path (Split-Path $output -Parent) 'latest.json') -Encoding utf8NoBOM
Write-Output ('Local signed package: '+$zip)
Write-Output 'No GitHub Release was published.'
