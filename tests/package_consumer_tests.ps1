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
    # Compile the actual API 6 contract from the restored assembly. These members
    # are absent in API 5; compiling only AutomaticAudioHls would miss stale binding
    # assets. Never execute this consumer or load its UWP native runtime here.
    $consumer = @'
using System;
using SendAirPlay2;

internal static class Program
{
    private static AudioDecision CheckOriginalAudio(MediaSource source) =>
        AudioDelivery.Check(source, TimeSpan.FromSeconds(30),
            permission: AudioConversionPermission.PreserveOriginal,
            profile: AudioDeliveryProfile.AppleAudioV1);

    private static void Main()
    {
        var options = new CastOptions
        {
            Delivery = CastDelivery.AutomaticAudioHls,
            AudioProfile = AudioDeliveryProfile.AppleAudioV1,
            ConversionPermission = AudioConversionPermission.AllowStereo
        };
        AudioDeliveryAction action = AudioDeliveryAction.Progressive;
        AudioDeliveryReason reason = AudioDeliveryReason.ProfileProgressive;
        AudioFamily family = AudioFamily.Mp3;
        Console.WriteLine($"{options.AudioProfile} {action} {reason} {family}");
    }
}
'@
    [IO.File]::WriteAllText((Join-Path $work 'Program.cs'), $consumer)
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
    Write-Host 'PASS: restored audio package compiles the API 6 audio-only contract and copies all six exact native DLLs'
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

        # A dependency-only package makes the default profile transitive, so a
        # guard that checks only the consumer's PackageReference items misses it.
        $wrapperId = 'SendAirPlay2.ConsumerTests.DefaultWrapper'
        $wrapperVersion = '1.0.0-' + [Guid]::NewGuid().ToString('N')
        $wrapperDirectory = Join-Path $work 'wrapper-feed'
        New-Item -ItemType Directory $wrapperDirectory | Out-Null
        $wrapperPath = Join-Path $wrapperDirectory "$wrapperId.$wrapperVersion.nupkg"
        $wrapper = [IO.Compression.ZipFile]::Open($wrapperPath, [IO.Compression.ZipArchiveMode]::Create)
        try {
            $entry = $wrapper.CreateEntry("$wrapperId.nuspec")
            $writer = [IO.StreamWriter]::new($entry.Open())
            try {
                $writer.Write(@"
<?xml version="1.0"?>
<package><metadata><id>$wrapperId</id><version>$wrapperVersion</version>
<authors>send-airplay2 tests</authors><description>Owned transitive dependency fixture.</description>
<dependencies><dependency id="SendAirPlay2" version="[$escapedDefault]" /></dependencies>
</metadata></package>
"@)
            } finally { $writer.Dispose() }
        } finally { $wrapper.Dispose() }
        $transitive = $references + "<PackageReference Include=`"$wrapperId`" Version=`"$wrapperVersion`" />"
        [IO.File]::WriteAllText($project, $prefix + $transitive + $suffix)
        # MSBuild escapes the source separator so it remains one property value.
        $sources = "$feed%3B$wrapperDirectory"
        $build = & dotnet build $project -c Release --nologo "-p:RestoreAdditionalProjectSources=$sources" 2>&1 | Out-String
        $buildExitCode = $LASTEXITCODE
        [IO.File]::WriteAllText((Join-Path $work 'transitive-conflict-build.txt'), $build)
        $assets = Get-Content -LiteralPath (Join-Path $work 'obj/project.assets.json') -Raw | ConvertFrom-Json
        $dependencies = @($assets.project.frameworks.'net10.0'.dependencies.PSObject.Properties.Name)
        $libraries = @($assets.libraries.PSObject.Properties.Name)
        if ($dependencies -contains 'SendAirPlay2' -or $dependencies -notcontains $wrapperId -or
            $libraries -notcontains "SendAirPlay2/$DefaultVersion") {
            throw "Transitive-conflict fixture did not restore the default profile only through its wrapper: $build"
        }
        if ($buildExitCode -eq 0 -or -not $build.Contains('Choose either SendAirPlay2 or SendAirPlay2.StereoAac')) {
            throw "Transitive mixed runtime packages must fail clearly: $build"
        }
        Write-Host 'PASS: resolved transitive default profile is refused without a direct PackageReference'
    }
} finally { $zip.Dispose() }
Write-Host 'Package restore/build/copy checks passed; consumer executable was not run.'
exit 0
