param([string]$RuntimeDirectory = (Join-Path $PSScriptRoot '..\build\Release'))
$ErrorActionPreference = 'Stop'
$destination = [IO.Path]::GetFullPath((Join-Path $RuntimeDirectory 'alert-voice'))
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$packages = @(
    @{ Name = 'sherpa-onnx-v2.0.1-win-x64-shared-MD-Release'; Hash = 'ce987d5bfca42c74b54f7b3bbaaa8c1febe3c5cd0fea8f94be80ea30e72f1f61'; Tag = 'v2.0.1' },
    @{ Name = 'kokoro-int8-multi-lang-v1_1'; Hash = '259c9c8bc906d6078e4e0570421885dabb68b539c2f970fcbb890a58eeaf3e9c'; Tag = 'tts-models' }
)
foreach ($package in $packages) {
    $archive = Join-Path $destination ($package.Name + '.tar.bz2')
    if (-not (Test-Path -LiteralPath $archive)) {
        $url = 'https://github.com/k2-fsa/sherpa-onnx/releases/download/' + $package.Tag + '/' + $package.Name + '.tar.bz2'
        Invoke-WebRequest -Uri $url -OutFile ($archive + '.part')
        Move-Item -LiteralPath ($archive + '.part') -Destination $archive -Force
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.Hash) {
        throw "SHA256 mismatch: $archive"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $destination $package.Name))) {
        & tar.exe -xf $archive -C $destination
        if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $archive" }
    }
}
$licenses = @(
    @{ Name = 'SHERPA-ONNX-LICENSE.txt'; Url = 'https://raw.githubusercontent.com/k2-fsa/sherpa-onnx/v2.0.1/LICENSE' },
    @{ Name = 'ONNX-RUNTIME-LICENSE.txt'; Url = 'https://raw.githubusercontent.com/microsoft/onnxruntime/v1.23.2/LICENSE' },
    @{ Name = 'ESPEAK-NG-DATA-LICENSE.txt'; Url = 'https://raw.githubusercontent.com/espeak-ng/espeak-ng/1.52.0/COPYING' }
)
foreach ($license in $licenses) {
    $licensePath = Join-Path $destination $license.Name
    if (-not (Test-Path -LiteralPath $licensePath)) { Invoke-WebRequest -Uri $license.Url -OutFile $licensePath }
}
# Keep upstream notices and source links beside the independent executable.
@'
Speech runtime: sherpa-onnx v2.0.1, Apache-2.0
https://github.com/k2-fsa/sherpa-onnx/tree/v2.0.1
Model: Kokoro v1.1-zh INT8, Apache-2.0
https://huggingface.co/hexgrad/Kokoro-82M-v1.1-zh
Model conversion and speaker mapping:
https://k2-fsa.github.io/sherpa/onnx/tts/all/Chinese-English/kokoro-multi-lang-v1_1.html
Bundled eSpeak NG phonemizer/data: GPL-3.0-or-later
https://github.com/espeak-ng/espeak-ng
Bundled ONNX Runtime: MIT
https://github.com/microsoft/onnxruntime
Original LICENSE files inside the downloaded distributions remain authoritative.
The product invokes the upstream TTS executable as a separate process.
'@ | Set-Content -LiteralPath (Join-Path $destination 'SOURCES.txt') -Encoding utf8
Write-Output "Offline Alert voices ready: $destination"
