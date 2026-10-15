# SPDX-License-Identifier: Apache-2.0
param(
    [Parameter(Mandatory=$true)][string]$PackagesDirectory,
    [Parameter(Mandatory=$true)][string]$AudioVersion,
    [string]$DefaultVersion
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Split-Path -Parent $PSScriptRoot
$feed = (Resolve-Path -LiteralPath $PackagesDirectory).Path
$work = Join-Path $root ("build-package-consumer-tests/" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work | Out-Null
$zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $feed "SendAirPlay2.StereoAac.$AudioVersion.nupkg"))
try {
    $names = @($zip.Entries | ForEach-Object { $_.FullName })
    $expected = @('send_airplay2.dll', 'libcrypto-3-x64.dll', 'avcodec-63.dll',
        'avformat-63.dll', 'avutil-61.dll', 'swresample-7.dll')
    $runtime = @($names | Where-Object { $_ -match '^runtimes/.+/native/.+\.dll$' })
    if ($runtime.Count -ne 6) { throw "Expected exactly six x64 native DLLs, got $($runtime.Count)" }
    foreach ($name in $expected) {
        if ($runtime -notcontains "runtimes/win-x64/native/$name") { throw "Missing packaged $name" }
    }
    foreach ($name in @('AUDIO-PROFILE.json', 'THIRD-PARTY-NOTICES.txt',
        'source/ffmpeg/ffmpeg-n9.0.2.tar.gz', 'source/ffmpeg/overlay/0054-matroska-track-number.patch',
        'buildTransitive/SendAirPlay2.StereoAac.targets')) {
        if ($names -notcontains $name) { throw "Missing distribution payload: $name" }
    }
    $project = Join-Path $work 'consumer.csproj'
    $escapedVersion = [Security.SecurityElement]::Escape($AudioVersion)
    $escapedDefault = [Security.SecurityElement]::Escape($DefaultVersion)
    $references = "<PackageReference Include=`"SendAirPlay2.StereoAac`" Version=`"$escapedVersion`" />"
    $prefix = '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><RuntimeIdentifier>win-x64</RuntimeIdentifier></PropertyGroup><ItemGroup>'
    $suffix = '</ItemGroup></Project>'
    [IO.File]::WriteAllText($project, $prefix + $references + $suffix)
    [IO.File]::WriteAllText((Join-Path $work 'Program.cs'), 'using SendAirPlay2; var options = new CastOptions { Delivery = CastDelivery.AutomaticAudioHls, ConversionPermission = AudioConversionPermission.AllowStereo }; System.Console.WriteLine(options.Delivery);')
    $build = & dotnet build $project -c Release --nologo "-p:RestoreAdditionalProjectSources=$feed" 2>&1 | Out-String
    [IO.File]::WriteAllText((Join-Path $work 'audio-build.txt'), $build)
    if ($LASTEXITCODE -ne 0) { throw "Audio consumer build failed: $build" }
    foreach ($name in $expected) {
        $entry = $zip.GetEntry("runtimes/win-x64/native/$name")
        $stream = $entry.Open()
        $hash = [Security.Cryptography.SHA256]::Create()
        try { $packaged = [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '') }
        finally { $hash.Dispose(); $stream.Dispose() }
        $copied = Join-Path $work "bin/Release/net10.0/win-x64/$name"
        if ((Get-FileHash -LiteralPath $copied).Hash -ne $packaged) { throw "Consumer runtime mismatch: $name" }
    }
    Write-Host 'PASS: restored audio package compiles against API 5 and copies all six exact native DLLs'
    $build = & dotnet build $project -c Release --nologo '-p:RuntimeIdentifier=win-arm64' "-p:RestoreAdditionalProjectSources=$feed" 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or -not $build.Contains('only win-x64 native runtime assets')) {
        throw "Missing-architecture consumer must fail clearly: $build"
    }
    Write-Host 'PASS: unsupported consumer architecture refused'
    if ($DefaultVersion) {
        $legacy = [IO.Compression.ZipFile]::OpenRead((Join-Path $feed "SendAirPlay2.$DefaultVersion.nupkg"))
        try {
            $legacyNames = @($legacy.Entries | ForEach-Object { $_.FullName })
            if (@($legacyNames | Where-Object { $_ -match '(avcodec|avformat|avutil|swresample)-\d+\.dll$|^source/ffmpeg/|^AUDIO-PROFILE.json$' }).Count) {
                throw 'Codec-free package contains opt-in codec assets'
            }
        } finally { $legacy.Dispose() }
        $both = $references + "<PackageReference Include=`"SendAirPlay2`" Version=`"$escapedDefault`" />"
        [IO.File]::WriteAllText($project, $prefix + $both + $suffix)
        $build = & dotnet build $project -c Release --nologo "-p:RestoreAdditionalProjectSources=$feed" 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0 -or -not $build.Contains('Choose either SendAirPlay2 or SendAirPlay2.StereoAac')) {
            throw "Mixed runtime packages must fail clearly: $build"
        }
        Write-Host 'PASS: default package is codec-free and mixed-profile references are refused'
    }
} finally { $zip.Dispose() }
Write-Host 'Package restore/build/copy checks passed; consumer executable was not run.'
exit 0
