# SPDX-License-Identifier: Apache-2.0
# Checks the UWP (WindowsStore) build output (docs/uwp-native-build.md): each
# DLL is marked AppContainer and links the app C runtime (VCRUNTIME140_APP)
# rather than the desktop one; send_airplay2.dll has no ADVAPI32 import (the
# Credential Manager adapter is compiled out) and no Botan DLL (Botan is static).
# -Machine names the expected architecture (dumpbin's machine field). Static
# properties only: this is not Store certification (WACK) or a run test.
param(
    [Parameter(Mandatory = $true)][string]$Directory,
    [ValidateSet('x64', 'x86', 'ARM64')][string]$Machine = 'x64'
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
$crypto = Get-ChildItem -Path $Directory -Filter 'libcrypto-*.dll' | Select-Object -First 1
if (-not (Test-Path $library)) {
    throw "send_airplay2.dll not found in $Directory"
}
if (-not $crypto) {
    Fail 'libcrypto DLL was not staged beside send_airplay2.dll'
}

foreach ($dll in @($library, $crypto.FullName) | Where-Object { $_ }) {
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
