# SPDX-License-Identifier: Apache-2.0
param(
    [Parameter(Mandatory=$true)][string]$AudioNative,
    [Parameter(Mandatory=$true)][string]$FfmpegShare,
    [Parameter(Mandatory=$true)][string]$SourceArchive,
    [string]$DevelopmentNative,
    [string]$CodecFreeNative
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'scripts/stereo_aac_package.ps1')
$work = Join-Path $root ("build-package-profile-tests/" + [Guid]::NewGuid().ToString('N'))
$native = Join-Path $work 'native'
$share = Join-Path $work 'codec/share/ffmpeg'
$bin = Join-Path $work 'codec/bin'
New-Item -ItemType Directory -Force $native, $share, $bin | Out-Null
$dlls = @('avcodec-63.dll', 'avformat-63.dll', 'avutil-61.dll', 'swresample-7.dll')
foreach ($name in $dlls) {
    Copy-Item -LiteralPath (Join-Path $AudioNative $name) -Destination $native
    Copy-Item -LiteralPath (Join-Path $AudioNative $name) -Destination $bin
}
foreach ($name in @('copyright', 'vcpkg.spdx.json', 'vcpkg-spdx-resources.json')) {
    Copy-Item -LiteralPath (Join-Path $FfmpegShare $name) -Destination $share
}
function Check-Profile {
    Get-StereoAacPackageProfile -Root $root -Directory $native -Share $share -SourceArchive $SourceArchive | Out-Null
}
function Expect-Rejection([string]$scenario, [string]$message, [scriptblock]$action) {
    $caught = $null
    try { & $action } catch { $caught = $_.Exception.Message }
    if (-not $caught -or -not $caught.Contains($message)) {
        throw "$scenario expected '$message', got '$caught'"
    }
    Write-Host "PASS: $scenario"
}
Check-Profile
Write-Host 'PASS: complete pinned runtime/profile/source'
foreach ($name in $dlls) {
    Remove-Item -LiteralPath (Join-Path $native $name)
    Expect-Rejection "missing $name" $name { Check-Profile }
    Copy-Item -LiteralPath (Join-Path $AudioNative $name) -Destination $native
}
Remove-Item -LiteralPath (Join-Path $share 'copyright')
Expect-Rejection 'missing notice' 'copyright' { Check-Profile }
Copy-Item -LiteralPath (Join-Path $FfmpegShare 'copyright') -Destination $share

$badArchive = Join-Path $work 'incorrect-source.tar.gz'
[IO.File]::WriteAllText($badArchive, 'owned invalid-source control')
Expect-Rejection 'source archive mismatch' 'SHA-512 mismatch' {
    Get-StereoAacPackageProfile -Root $root -Directory $native -Share $share -SourceArchive $badArchive
}
[IO.File]::WriteAllText((Join-Path $native 'avutil-61.dll'), 'owned mismatched-runtime control; never loaded')
Expect-Rejection 'runtime/prefix mismatch' 'does not match' { Check-Profile }
Copy-Item -LiteralPath (Join-Path $AudioNative 'avutil-61.dll') -Destination $native

$provenancePath = Join-Path $share 'vcpkg.spdx.json'
$provenance = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
$port = @($provenance.files | Where-Object { $_.fileName -eq './portfile.cmake' })[0]
$port.checksums[0].checksumValue = '0' * 64
$provenance | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $provenancePath
Expect-Rejection 'overlay provenance mismatch' 'overlay differs' { Check-Profile }
Copy-Item -LiteralPath (Join-Path $FfmpegShare 'vcpkg.spdx.json') -Destination $share

# Preserve PE bytes except the equal-length embedded configuration control.
# Neither mutated file is loaded: these checks only read bytes.
$codec = Join-Path $native 'avcodec-63.dll'
$bytes = [IO.File]::ReadAllBytes($codec)
$offset = [Text.Encoding]::ASCII.GetString($bytes).IndexOf('--disable-gpl')
if ($offset -lt 0) { throw 'Known runtime lacks the configuration control' }
[Array]::Copy([Text.Encoding]::ASCII.GetBytes('--enable-gpl '), 0, $bytes, $offset, 13)
[IO.File]::WriteAllBytes($codec, $bytes)
[IO.File]::WriteAllBytes((Join-Path $bin 'avcodec-63.dll'), $bytes)
Expect-Rejection 'GPL-enabled configuration' 'must disable gpl' { Check-Profile }

$pwsh = (Get-Process -Id $PID).Path
$checker = Join-Path $root 'scripts/check_uwp_binaries.ps1'
$output = & $pwsh -NoProfile -File $checker -Directory $AudioNative -Distribution 2>&1 | Out-String
if ($LASTEXITCODE -eq 0 -or -not $output.Contains('require the explicit -StereoAac')) {
    throw 'Codec-enabled runtime was not refused by the codec-free distribution profile'
}
Write-Host 'PASS: codec-free profile refuses codec-enabled runtime'
if ($DevelopmentNative) {
    $output = & $pwsh -NoProfile -File $checker -Directory $DevelopmentNative -StereoAac -Distribution 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or -not $output.Contains('private development exports')) {
        throw 'Private validation exports were not refused for distribution'
    }
    Write-Host 'PASS: distribution refuses private development exports'
}
if ($CodecFreeNative) {
    # A clean-source stamp cannot distinguish enabled and disabled builds.
    # The opt-in profile must also inspect the core's actual codec imports.
    foreach ($name in $dlls) { Copy-Item -LiteralPath (Join-Path $AudioNative $name) -Destination $native }
    Copy-Item -LiteralPath (Join-Path $CodecFreeNative 'send_airplay2.dll') -Destination $native
    Get-ChildItem -LiteralPath $CodecFreeNative -Filter 'libcrypto-*.dll' | Copy-Item -Destination $native
    $output = & $pwsh -NoProfile -File $checker -Directory $native -StereoAac -Distribution 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or -not $output.Contains('StereoAac core must import')) {
        throw 'Codec-free core was not refused by the opt-in distribution profile'
    }
    Write-Host 'PASS: opt-in profile refuses codec-free core even with staged codec DLLs'
}
Write-Host 'Package profile controls passed; no modified DLL was loaded.'
exit 0
