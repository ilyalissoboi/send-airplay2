# SPDX-License-Identifier: Apache-2.0
# Packs the SendAirPlay2 NuGet package (docs/nuget-package.md): the C# binding
# plus the UWP-built native libraries for each architecture given in -Native.
#
# -Native takes "arch=directory" entries (arch: x64, x86 or arm64). Each
# directory is a UWP Release build output (docs/uwp-native-build.md); its
# send_airplay2.dll and libcrypto-*.dll pass check_uwp_binaries.ps1 before they
# are placed under runtimes/win-<arch>/native. A missing directory is an error:
# pass only the architectures that were built (an app built for one the package
# lacks cannot cast). -Build first rebuilds each directory's CMake tree.
#
# Provenance: each directory needs the send_airplay2.source.txt stamp the build
# writes beside the library (cmake/write_source_stamp.cmake). A library built
# with uncommitted native changes, or from a commit whose native sources differ
# from HEAD, is refused, so BUILD-INFO.txt never attributes stale binaries to
# HEAD; it records each library's own source commit.
#
# Notices: the OpenSSL, Botan and Boost copyright files come from -VcpkgShare,
# by default the vcpkg share directory recorded in the first build's
# CMakeCache.txt. The package version is -Version, or the binding's <Version>
# plus "-" and -VersionSuffix, by default "local.<UTC timestamp>", so every local
# pack has a new version and NuGet's global package cache never serves a stale
# copy. Relative paths are from the repository root.
param(
    [string[]]$Native = @('x64=build-uwp/Release', 'x86=build-uwp-x86/Release', 'arm64=build-uwp-arm64/Release'),
    [string]$Version,
    [string]$VersionSuffix,
    [string]$OutputDirectory = 'packages-local',
    [string]$VcpkgShare,
    [switch]$Build
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root 'bindings/csharp/SendAirPlay2/SendAirPlay2.csproj'
$machines = @{ x64 = 'x64'; x86 = 'x86'; arm64 = 'ARM64' }
# The native build's inputs; CMakeLists.txt passes the same list to the stamp.
$nativePaths = @('src', 'include', 'CMakeLists.txt', 'cmake', 'vcpkg.json', 'vcpkg-overlays')

# The source commit of the library in $directory, checked against HEAD.
function Read-SourceStamp([string]$directory, [string]$arch) {
    $stamp = Join-Path $directory 'send_airplay2.source.txt'
    if (-not (Test-Path $stamp)) {
        throw "no send_airplay2.source.txt in $directory for ${arch}: rebuild it (-Build) with git available"
    }
    $fields = @{}
    foreach ($line in Get-Content $stamp) {
        $pair = $line -split '=', 2
        if ($pair.Count -eq 2) { $fields[$pair[0]] = $pair[1] }
    }
    $commit = $fields['commit']
    if (-not $commit -or $commit -eq 'unknown') { throw "$arch was built without a known source commit" }
    if ($fields['native_changes'] -ne 'no') { throw "$arch was built with uncommitted native changes" }
    & git -C $root cat-file -e "$commit^{commit}" 2>$null
    if ($LASTEXITCODE -ne 0) { throw "$arch was built from $commit, which this checkout does not have" }
    & git -C $root diff --quiet $commit HEAD -- @nativePaths
    if ($LASTEXITCODE -ne 0) {
        throw "$arch was built from $commit, whose native sources differ from HEAD; rebuild it (-Build)"
    }
    return $commit
}

function Resolve-FromRoot([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return Join-Path $root $path
}

function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $bundled = & $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' |
        Select-Object -First 1
    if (-not $bundled) { throw 'cmake not found on PATH or in Visual Studio' }
    return $bundled
}

# The CMake cache entry NAME of a build tree, or $null.
function Read-CMakeCache([string]$buildTree, [string]$name) {
    $cache = Join-Path $buildTree 'CMakeCache.txt'
    if (-not (Test-Path $cache)) { return $null }
    $line = Select-String -Path $cache -Pattern "^$name(:[A-Z]+)?=(.*)$" | Select-Object -First 1
    if ($line) { return $line.Matches[0].Groups[2].Value }
    return $null
}

# --- Architectures -------------------------------------------------------------
$entries = @()
foreach ($item in $Native) {
    $parts = $item -split '=', 2
    if ($parts.Count -ne 2 -or -not $machines.ContainsKey($parts[0])) {
        throw "-Native entries are arch=directory with arch x64, x86 or arm64: '$item'"
    }
    $directory = Resolve-FromRoot $parts[1]
    if ($Build) {
        $tree = Split-Path -Parent $directory
        if (-not (Test-Path (Join-Path $tree 'CMakeCache.txt'))) {
            throw "$tree is not a configured CMake tree; configure it first (docs/uwp-native-build.md)"
        }
        & (Find-CMake) --build $tree --config Release
        if ($LASTEXITCODE -ne 0) { throw "build of $tree failed" }
    }
    if (-not (Test-Path (Join-Path $directory 'send_airplay2.dll'))) {
        throw "no send_airplay2.dll in $directory for $($parts[0]); build it (docs/uwp-native-build.md) or leave $($parts[0]) out of -Native"
    }
    & (Join-Path $PSScriptRoot 'check_uwp_binaries.ps1') -Directory $directory -Machine $machines[$parts[0]]
    if ($LASTEXITCODE -ne 0) { throw "UWP binary checks failed for $($parts[0])" }
    $source = Read-SourceStamp $directory $parts[0]
    $entries += [pscustomobject]@{ Arch = $parts[0]; Directory = $directory; Source = $source }
}
if ($entries.Count -eq 0) { throw 'no architectures to pack' }
if (@($entries.Arch | Sort-Object -Unique).Count -ne $entries.Count) { throw 'an architecture is listed twice' }

# --- Notices ---------------------------------------------------------------------
if (-not $VcpkgShare) {
    $tree = Split-Path -Parent $entries[0].Directory
    $installed = Read-CMakeCache $tree 'VCPKG_INSTALLED_DIR'
    $triplet = Read-CMakeCache $tree 'VCPKG_TARGET_TRIPLET'
    if (-not $installed -or -not $triplet) {
        throw "cannot find the vcpkg install of $tree; pass -VcpkgShare <installed>/<triplet>/share"
    }
    $VcpkgShare = Join-Path $installed "$triplet/share"
} else {
    $VcpkgShare = Resolve-FromRoot $VcpkgShare
}
$notices = [ordered]@{
    'OpenSSL (libcrypto, Apache-2.0)' = 'openssl/copyright'
    'Botan (linked into send_airplay2.dll, BSD-2-Clause)' = 'botan/copyright'
    'Boost (Asio and Beast headers compiled into send_airplay2.dll, BSL-1.0)' = 'boost-asio/copyright'
}

# --- Version ---------------------------------------------------------------------
if (-not $Version) {
    $base = ([xml](Get-Content $project -Raw)).SelectSingleNode('/Project/PropertyGroup/Version').InnerText
    if (-not $VersionSuffix) { $VersionSuffix = "local.$([DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))" }
    $Version = "$base-$VersionSuffix"
}

# --- Stage -----------------------------------------------------------------------
$stage = Join-Path $root 'build-pack/content'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
$utf8 = New-Object Text.UTF8Encoding $false

$commit = (& git -C $root rev-parse HEAD).Trim()
$dirty = if (& git -C $root status --porcelain) { ' (with uncommitted changes)' } else { '' }
$info = @("SendAirPlay2 $Version", "Binding and package source: $commit$dirty",
    "Architectures: $($entries.Arch -join ', ')", '')
foreach ($entry in $entries) {
    $info += "win-$($entry.Arch) built from $($entry.Source) (native sources identical at the binding's commit)"
}
$info += ''
foreach ($entry in $entries) {
    $target = Join-Path $stage "runtimes/win-$($entry.Arch)/native"
    New-Item -ItemType Directory -Force $target | Out-Null
    $files = @(Get-Item (Join-Path $entry.Directory 'send_airplay2.dll')) +
        @(Get-ChildItem -Path $entry.Directory -Filter 'libcrypto-*.dll')
    foreach ($file in $files) {
        Copy-Item $file.FullName $target
        $info += "win-$($entry.Arch)/$($file.Name) SHA-256 $((Get-FileHash $file.FullName).Hash.ToLowerInvariant())"
    }
}
[IO.File]::WriteAllText((Join-Path $stage 'BUILD-INFO.txt'), ($info -join "`n") + "`n", $utf8)

$text = @('Third-party notices for the SendAirPlay2 package.',
    'The package itself is Apache-2.0 (LICENSE.txt). The native libraries contain or',
    'load the components below; their notices are reproduced from the vcpkg ports',
    'that built them.', '')
foreach ($name in $notices.Keys) {
    $file = Join-Path $VcpkgShare $notices[$name]
    if (-not (Test-Path $file)) { throw "notice not found: $file" }
    $text += ('=' * 78), $name, ('=' * 78), '', (Get-Content $file -Raw).TrimEnd(), ''
}
[IO.File]::WriteAllText((Join-Path $stage 'THIRD-PARTY-NOTICES.txt'), ($text -join "`n") + "`n", $utf8)
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-Item (Join-Path $root 'bindings/csharp/SendAirPlay2/PACKAGE.md') (Join-Path $stage 'README.md')

# --- Pack ------------------------------------------------------------------------
$output = Resolve-FromRoot $OutputDirectory
New-Item -ItemType Directory -Force $output | Out-Null
& dotnet pack $project -c Release --nologo -o $output "-p:Version=$Version" "-p:Sap2PackageContent=$stage"
if ($LASTEXITCODE -ne 0) { throw 'dotnet pack failed' }

$package = Join-Path $output "SendAirPlay2.$Version.nupkg"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($package)
try {
    $names = $zip.Entries | ForEach-Object { $_.FullName }
} finally {
    $zip.Dispose()
}
foreach ($required in @('lib/netstandard2.0/SendAirPlay2.dll', 'THIRD-PARTY-NOTICES.txt', 'BUILD-INFO.txt', 'README.md') +
    ($entries | ForEach-Object { "runtimes/win-$($_.Arch)/native/send_airplay2.dll" })) {
    if ($names -notcontains $required) { throw "package lacks $required" }
}
Write-Host "Packed $package"
Write-Host "Version $Version; architectures $($entries.Arch -join ', ')"
foreach ($missing in @('x64', 'x86', 'arm64') | Where-Object { $entries.Arch -notcontains $_ }) {
    Write-Warning "no win-$missing native library: an app built for $missing cannot cast with this package"
}
