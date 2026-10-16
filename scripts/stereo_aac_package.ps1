# SPDX-License-Identifier: Apache-2.0
# Opt-in package inputs. No DLL is loaded or source archive executed here.
function Get-StereoAacPackageProfile {
    param([string]$Root, [string]$Directory, [string]$Share, [string]$SourceArchive)

    $codecNames = @('avcodec-63.dll', 'avformat-63.dll', 'avutil-61.dll', 'swresample-7.dll')
    foreach ($name in $codecNames) {
        if (-not (Test-Path -LiteralPath (Join-Path $Directory $name) -PathType Leaf)) {
            throw "Required codec runtime DLL missing: $name"
        }
    }
    foreach ($name in @('copyright', 'vcpkg.spdx.json', 'vcpkg-spdx-resources.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Share $name) -PathType Leaf)) {
            throw "FFmpeg provenance/notice file missing: $name"
        }
    }
    $prefix = Split-Path -Parent (Split-Path -Parent $Share)
    $modules = @()
    foreach ($name in $codecNames) {
        $staged = (Get-FileHash -LiteralPath (Join-Path $Directory $name)).Hash.ToLowerInvariant()
        $installed = (Get-FileHash -LiteralPath (Join-Path $prefix "bin/$name")).Hash.ToLowerInvariant()
        if ($staged -ne $installed) { throw "$name does not match the selected FFmpeg prefix" }
        $modules += [ordered]@{ name = $name; sha256 = $staged }
    }

    # Bind the installed port record to the source/patches we will redistribute.
    $overlay = Join-Path $Root 'vcpkg-overlays/ports/ffmpeg'
    $spdx = Get-Content -LiteralPath (Join-Path $Share 'vcpkg.spdx.json') -Raw | ConvertFrom-Json
    foreach ($file in Get-ChildItem -LiteralPath $overlay -File) {
        $record = @($spdx.files | Where-Object { $_.fileName -eq "./$($file.Name)" })
        if ($record.Count -ne 1) { throw "Installed FFmpeg provenance omits $($file.Name)" }
        $checksum = @($record[0].checksums | Where-Object { $_.algorithm -eq 'SHA256' })
        if ($checksum.Count -ne 1 -or $checksum[0].checksumValue -ne (Get-FileHash -LiteralPath $file.FullName).Hash) {
            throw "Installed FFmpeg overlay differs: $($file.Name)"
        }
    }
    $port = Get-Content -LiteralPath (Join-Path $overlay 'portfile.cmake') -Raw
    $expectedArchiveHash = [regex]::Match($port, 'SHA512\s+([a-f0-9]{128})').Groups[1].Value
    if (-not $expectedArchiveHash -or -not (Test-Path -LiteralPath $SourceArchive -PathType Leaf)) {
        throw 'The pinned FFmpeg source archive is required; pass -FfmpegSourceArchive'
    }
    $archiveHash = (Get-FileHash -LiteralPath $SourceArchive -Algorithm SHA512).Hash.ToLowerInvariant()
    if ($archiveHash -ne $expectedArchiveHash) { throw 'FFmpeg source archive SHA-512 mismatch' }

    $binary = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $Directory 'avcodec-63.dll')))
    $configurations = @($binary.Split([char]0) | Where-Object {
        $_.Contains('--disable-everything') -and $_.Contains('--enable-decoder=')
    } | Sort-Object -Unique)
    if ($configurations.Count -ne 1) { throw 'Expected one embedded FFmpeg build configuration' }
    $configuration = $configurations[0]
    foreach ($flag in @('gpl', 'nonfree', 'version3', 'network', 'programs', 'autodetect')) {
        if ($configuration -notmatch "(?:^|\s)--disable-$flag(?:\s|$)" -or
            $configuration -match "(?:^|\s)--enable-$flag(?:=|\s|$)") {
            throw "FFmpeg configuration must disable $flag"
        }
    }
    $encoders = [regex]::Matches($configuration, '(?:^|\s)--enable-encoder=([^\s]+)')
    if ($encoders.Count -ne 1 -or $encoders[0].Groups[1].Value -ne 'aac' -or
        $configuration -match '(?:^|\s)--enable-encoders(?:\s|$)') {
        throw 'FFmpeg configuration must use only the native AAC encoder'
    }
    return [pscustomobject]@{
        Files = $codecNames; Share = $Share; SourceArchive = $SourceArchive
        Record = [ordered]@{
            schema_version = 1; package_profile = 'stereo-aac'; ffmpeg_version = '9.0.2'
            source_url = 'https://github.com/ffmpeg/ffmpeg/archive/refs/tags/n9.0.2.tar.gz'
            source_sha512 = $archiveHash; runtime_license = 'LGPL-2.1-or-later'
            configuration = $configuration; modules = $modules
            vcpkg_revision = '434307da09bc05b2c86996dccc8b2351fc0d5d37'
        }
    }
}

function Copy-StereoAacPackageSources {
    param([string]$Root, [string]$Stage, $Profile)
    $source = Join-Path $Stage 'source/ffmpeg'
    New-Item -ItemType Directory -Force $source | Out-Null
    Copy-Item -LiteralPath $Profile.SourceArchive -Destination (Join-Path $source 'ffmpeg-n9.0.2.tar.gz')
    # Exact overlay plus vcpkg notices; no external source is added to tracked C++.
    Copy-Item -LiteralPath (Join-Path $Root 'vcpkg-overlays/ports/ffmpeg') -Destination (Join-Path $source 'overlay') -Recurse
    Copy-Item -LiteralPath (Join-Path $Root 'vcpkg-overlays/LICENSE-vcpkg.txt') -Destination $source
    Copy-Item -LiteralPath (Join-Path $Root 'tools/audio-codec-spike/triplets/x64-uwp-codec-release.cmake') -Destination $source
    Copy-Item -LiteralPath (Join-Path $Root 'tools/audio-codec-spike/vcpkg.json') -Destination (Join-Path $source 'codec-manifest.json')
    Copy-Item -LiteralPath (Join-Path $Root 'bindings/csharp/SendAirPlay2/FFMPEG-SOURCE.md') -Destination (Join-Path $source 'README.md')
    foreach ($name in @('copyright', 'vcpkg.spdx.json', 'vcpkg-spdx-resources.json')) {
        Copy-Item -LiteralPath (Join-Path $Profile.Share $name) -Destination $source
    }
    $Profile.Record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Stage 'AUDIO-PROFILE.json') -Encoding utf8
}
