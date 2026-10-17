# Packaged UWP AAC/M4A progressive gate

2026-10-18 (Asia/Tokyo), D98, in progress. The user authorizes the packaged
UWP follow-up after native probe PR #80 merges as `377b7ce`; all 17 final-head
checks pass at `b105333`. Native D97 audio-only playback is not packaged proof.

Test-host implementation `153a840` adds explicit `type=audio/mp4|video/mp4`
for progressive scripts, an `.m4a` picker entry and sanitized source release
counters. The form supplies audio/mp4 for `.m4a` when progressive is selected.
Explicit types with remux/automatic delivery and unsupported explicit values
are rejected before opening a file. Existing defaults remain video/mp4; no
native core/API, decoder, Screenbox or transport implementation changes.
The source observer runs after its brokered streams close; callback failures
cannot prevent that completed cleanup. It logs only counts and remains optional
for the shared offline validation adapter.

## Build and runtime checkpoint

The current codec-free x64 UWP build passes the app C runtime, AppContainer,
no desktop Credential Manager import and no Botan DLL checks. VS 2026 MSBuild
builds the actual Native AOT MSIX without warnings/errors. Package 0.1.30.0 uses
the default internetClient/privateNetworkClientServer capabilities on a Private
Windows network; it adds no internetClientServer. Both native DLLs match build
output and no codec DLLs are present. Registration updates 0.1.29.0 in place,
preserving identity, PasswordVault credentials, grants and other app data.
The running executable/AppContainer and both loaded native hashes match the
inspected package. Local negative scripts reject the unsupported MIME and
explicit audio type with HLS before file access or a receiver request.

The fixture is D97's original 64-second stereo AAC-LC M4A, zero video streams,
SHA-256 `6f951e78d72ea8be4a687bc8dd1b12f0c1843dbaba41a56e3edde265d4c17d16`.
No private media is used. File picker access must be granted manually, remembered
under the fixed label audio-only. Subsequent scripts use that brokered grant,
existing PasswordVault profiles and uniquely model-selected receivers; names,
addresses, PINs, URLs and source paths are not logged.

## Pending manual gates

Receiver availability/version confirmation, owned-file picker grant and all
packaged listening observations are pending. Coordinate Mac consent cancellation
through the existing Stop button, then audible controls/natural end/separate Stop
on Mac and Apple TV. Verify source release count and process/native hashes after
the runs. Use written instructions/user observation, without Computer Use visual
confirmation. Keep attempts and final head/binary/firmware scope explicit.

This gate starts no Screenbox audio-file implementation or release. Only the
test host gains explicit progressive test options. Audio-only HLS, conversion,
other codecs, HomePod/RAOP, discrete converted surround, Chromecast validation
and user-deferred remote-Stop classification remain separate/deferred scopes.
