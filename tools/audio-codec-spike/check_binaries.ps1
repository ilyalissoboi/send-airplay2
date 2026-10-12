# SPDX-License-Identifier: Apache-2.0
# Static properties only: not WACK certification or a packaged runtime test.
param([Parameter(Mandatory = $true)][string]$Directory)
$ErrorActionPreference = 'Stop'
$probeVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$probeDumpbin = & $probeVswhere -latest -products * -find 'VC/Tools/MSVC/**/bin/Hostx64/x64/dumpbin.exe' | Select-Object -First 1
if (-not $probeDumpbin) { throw 'dumpbin not found' }
$probeNames = @('sap2_codec_probe.dll', 'avformat-63.dll', 'avcodec-63.dll', 'avutil-61.dll', 'swresample-7.dll')
foreach ($probeName in $probeNames) {
    $probePath = Join-Path $Directory $probeName
    if (-not (Test-Path -LiteralPath $probePath)) { throw "Missing $probeName" }
    $probeHeaders = (& $probeDumpbin /nologo /headers $probePath) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $probeHeaders -notmatch 'App Container' -or $probeHeaders -notmatch 'machine \(x64\)') {
        throw "$probeName is not x64 AppContainer"
    }
    $probeImports = (& $probeDumpbin /nologo /dependents $probePath) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $probeImports -notmatch '(?im)^\s*VCRUNTIME140_APP\.dll\s*$' -or
        $probeImports -match '(?im)^\s*(VCRUNTIME140|ADVAPI32|USER32|GDI32)\.dll\s*$') {
        throw "$probeName has unexpected desktop dependencies or lacks the app CRT"
    }
    Write-Host "inspected $probeName (x64 AppContainer/app CRT)"
}
