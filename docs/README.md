# Documentation

These guides describe the current source and its supported contracts. Public
headers are authoritative for ABI details; released packages retain the API
version and behavior of their tagged source.

## Build and contribute

- [Contribution workflow](../CONTRIBUTING.md)
- [Build options and tests](building.md)
- [Architecture and source map](design.md)
- [Dependency licenses and provenance](dependencies.md)
- [Packages and release workflow](nuget-package.md)
- [UWP native build](uwp-native-build.md) and [packaged example](uwp-host.md)
- [Unfinished work and research](roadmap.md)

## Integrate

- [C API](public-api.md) and [C# binding](csharp-binding.md)
- [Credential callbacks](credential-interface.md) and [OS credential stores](credential-storage.md)
- [Discovery](discovery.md)
- [HTTP media server](media-server.md)
- [Video remux, subtitles and HDR](hls.md)
- [Video audio selection and conversion](audio-transcoding.md)
- [Audio-only delivery](automatic-audio-only.md)

## Protocol internals

- [TLV and encrypted records](pairing-transport.md)
- [PIN pairing](pin-pairing.md) and [peer verification](peer-verification.md)
- [Receiver transport](receiver-transport.md)
- [Playback session lifecycle](session-design.md) and [MRP controls](mrp-controls.md)
- [macOS receiver consent](macos-receiver-consent.md)

## Validate

- [Compatibility summary and limits](receiver-validation.md)
- [Manual receiver procedure](manual-validation.md)
- [Authentication E2E runner](e2e-runner.md)
- [Reference sender comparison](reference-baseline.md)
- [Authentication fixture provenance](../tests/fixtures/README.md)
- [Audio fixture generators](../tools/audio-only-probe/README.md) and [MP3 probe](../tools/mp3-probe/README.md)

Completed implementation plans, session handoffs, superseded candidate records
and machine-specific validation logs are available in Git history. They are not
part of the current contributor instructions. Release assets and versioned
release notes are available from [GitHub Releases](https://github.com/ilyalissoboi/send-airplay2/releases).
