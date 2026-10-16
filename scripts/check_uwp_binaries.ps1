# SPDX-License-Identifier: Apache-2.0
# Checks the UWP (WindowsStore) build output (docs/uwp-native-build.md): each
# DLL is marked AppContainer and links the app C runtime (VCRUNTIME140_APP)
# rather than the desktop one; send_airplay2.dll has no ADVAPI32 import (the
# Credential Manager adapter is compiled out) and no Botan DLL (Botan is static).
# -Machine names the expected architecture (dumpbin's machine field). Static
# properties only: this is not Store certification (WACK) or a run test.
param(
    [Parameter(Mandatory = $true)][string]$Directory,
    [ValidateSet('x64', 'x86', 'ARM64')][string]$Machine = 'x64',
    [switch]$StereoAac,
    [switch]$Distribution
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$dumpbin = & $vswhere -latest -products * -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' |
    Select-Object -First 1
if (-not $dumpbin) {
    throw 'dumpbin.exe not found'
}

$failures = 0
function Fail([string]$message) {
    Write-Host "FAIL: $message"
    $script:failures++
}

$library = Join-Path $Directory 'send_airplay2.dll'
$cryptoFiles = @(Get-ChildItem -Path $Directory -Filter 'libcrypto-*.dll')
$crypto = $cryptoFiles | Select-Object -First 1
if (-not (Test-Path $library)) {
    throw "send_airplay2.dll not found in $Directory"
}
if (-not $crypto) {
    Fail 'libcrypto DLL was not staged beside send_airplay2.dll'
}
if ($Distribution -and $cryptoFiles.Count -ne 1) {
    Fail 'Distribution requires exactly one libcrypto DLL for the selected architecture'
}

$checkedDlls = @($library, $crypto.FullName) | Where-Object { $_ }
if ($StereoAac) {
    foreach ($codecName in @('avformat-63.dll', 'avcodec-63.dll', 'avutil-61.dll', 'swresample-7.dll')) {
        $codecPath = Join-Path $Directory $codecName
        if (-not (Test-Path -LiteralPath $codecPath)) { Fail "Missing $codecName" }
        else { $checkedDlls += $codecPath }
    }
    $codec = Join-Path $Directory 'avcodec-63.dll'
    if (Test-Path -LiteralPath $codec) {
        # Inspect the embedded build configuration as well as PE/import flags:
        # an older compatible-ABI DLL can pass those while omitting new decoders.
        $binaryText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $codec)))
        $decoderOption = [regex]::Match($binaryText, "--enable-decoder=(?:'([^']+)'|([^\s\x00]+))")
        $decoderList = if ($decoderOption.Groups[1].Success) { $decoderOption.Groups[1].Value } else { $decoderOption.Groups[2].Value }
        $decoders = $decoderList.Split(',')
        foreach ($required in @('aac','ac3','eac3','dca','flac','opus','vorbis','truehd',
            'pcm_s16le','pcm_s16be','pcm_s24le','pcm_s24be','pcm_s32le','pcm_s32be',
            'pcm_f32le','pcm_f32be','pcm_f64le','pcm_f64be')) {
            if ($required -notin $decoders) { Fail "Staged codec profile omits $required" }
        }
    }
}
foreach ($dll in $checkedDlls) {
    $name = Split-Path $dll -Leaf
    $headers = (& $dumpbin /nologo /headers $dll) -join "`n"
    if ($headers -notmatch 'App Container') {
        Fail "$name is not marked AppContainer"
    }
    if ($headers -notmatch "machine \($Machine\)") {
        Fail "$name is not built for $Machine"
    }
    $dependents = (& $dumpbin /nologo /dependents $dll) -join "`n"
    if ($dependents -notmatch '(?im)^\s*VCRUNTIME140_APP\.dll\s*$') {
        Fail "$name does not link VCRUNTIME140_APP.dll"
    }
    if ($dependents -match '(?im)^\s*VCRUNTIME140\.dll\s*$') {
        Fail "$name links the desktop VCRUNTIME140.dll"
    }
    Write-Host "inspected $name (expected $Machine)"
}

$libraryDependents = (& $dumpbin /nologo /dependents $library) -join "`n"
if (-not $StereoAac -and $libraryDependents -match '(?im)^\s*(avcodec|avformat|avutil|swresample)-\d+\.dll\s*$') {
    Fail 'Codec-enabled libraries require the explicit -StereoAac package/check profile'
}
if ($StereoAac) {
    foreach ($name in @('avcodec-63.dll', 'avformat-63.dll', 'avutil-61.dll', 'swresample-7.dll')) {
        if ($libraryDependents -notmatch ('(?im)^\s*' + [regex]::Escape($name) + '\s*$')) {
            Fail "StereoAac core must import $name; rebuild with SAP2_STEREO_AAC=ON"
        }
    }
}
if ($Distribution) {
    $exports = (& $dumpbin /nologo /exports $library) -join "`n"
    if ($exports -match 'sap2_runtime_validate') {
        Fail 'Distribution libraries must disable SAP2_RUNTIME_VALIDATION (private development exports)'
    }
}
if ($libraryDependents -match '(?im)^\s*ADVAPI32\.dll\s*$') {
    Fail 'send_airplay2.dll imports ADVAPI32.dll (Credential Manager must be compiled out)'
}
if ($libraryDependents -match '(?im)^\s*botan[^\s]*\.dll\s*$') {
    Fail 'send_airplay2.dll depends on a Botan DLL (Botan must be linked statically)'
}

if ($failures -ne 0) {
    exit 1
}
Write-Host 'UWP binary checks passed'
