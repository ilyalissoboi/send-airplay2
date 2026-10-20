# D109: API 6 package readiness

2026-10-20 (Asia/Tokyo). The user authorized the next package preparation after
[PR #93](https://github.com/ilyalissoboi/send-airplay2/pull/93) merged as `ee968e9`.
All 17 checks passed at its final head `f31227b`. The
[native audio-only matrix](automatic-audio-only-native-gate.md) and
[focused packaged integration gate](automatic-audio-only-uwp-gate.md) are complete.
No additional receiver run is needed for this packaging slice.
The current readiness branch is `codex/audio-only-package-candidate`; inspect
its actual PR head/CI before continuing. Its local archives are not a release.

## Required corrections before a clean-main candidate

The binding/runtime already require API 6, but the shipping opt-in README still
advertised API 5 and video-only delivery. The consumer check compiled only older
automatic-video members, allowing a stale API-5 binding to pass. This slice:

- Updates both new package version bases to `0.6.0`, following the documented
  experimental C API convention. The packer and workflow consumer agree; old
  published identities/assets and Screenbox pins remain unchanged.
- Describes the explicit AppleAudioV1 progressive/conversion table, host store/
  permission requirements, finite bounds and speaker/surround exclusions.
- Compiles AppleAudioV1, Progressive, ProfileProgressive, Mp3 and the public
  offline-check profile argument from the actually restored package. Existing
  six-DLL copy, unsupported-RID and direct/transitive profile-conflict checks stay.
- Updates build metadata to distinguish audio-only URL/HLS media from deferred
  RAOP/speaker transport. No native C++, public API or codec/dependency change is made.

## Validation checkpoint

**Local readiness passed.** Implementation `04a3559` builds clean-attribution
`0.6.0-d109.api6.1` validation packages for both identities from unchanged native
source `ee968e9`. These are x64-only local inputs; CI separately builds the
ordinary three-architecture package. The enabled distribution has validation
hooks OFF; the earlier diagnostic runtime is retained for the negative export
check. All thirteen package-profile controls pass, including missing DLL/source/
notice, wrong prefix/profile, GPL and private-export refusals. The C# binding
CTest passes 1/1 in 4.97 seconds.

Fresh restored consumers compile the API 6 members and copy all six exact native
hashes. Unsupported architecture and direct/transitive profile conflicts refuse;
the comparison package remains codec-free. The unchanged published API-5
`0.5.0-ci.294` package is a separate negative control: the same consumer fails
with CS0117 for the new members. No consumer executable/native DLL is run.

An independent static audit passes clean binding/native attribution, all six
recorded hashes, UWP distribution properties, the pinned archive, all 22 exact
overlay files, notices and rebuild payload. All five dependency hashes match
D108's inspected runtime; its core includes diagnostic exports whereas this
distribution core excludes them. The local opt-in archive SHA-256 is
`745f435a0a4cbc5dacaf0694cb3cb9fe53697df3e7badab6af22602627d1c29e`.
The [sanitized record](validation/audio-only-package-readiness-windows-2026-10-20.json)
retains both package sizes/hashes, exact native/source scope and checks.
Actual PR-head CI remains separate from the merged base and local checks.

The source archive remains unmodified FFmpeg 9.0.2 with the already adopted
port-6 configuration. Its pinned archive hash, exact source/overlay, notices,
configuration and six native hashes are verified for the local input. Repeat
that independent audit on the clean-main candidate. No new dependency or
license is introduced. Local branch packs are validation inputs, not the
clean-main publication candidate, and no packaged consumer is executed.

## Next checkpoints

After this packaging correction is reviewed/merged, dispatch a clean-main run,
retain its exact `nuget-stereo-aac-x64` artifact, independently audit source/
runtime/consumer attribution, prepare the checksum and exact release notes,
then present that concrete candidate for publication approval. Publication
uses the existing GitHub prerelease procedure and requires separate explicit
authorization. Do not substitute a PR/local package or rewrite archive bytes.
Screenbox pin/routing/queue integration follows that prerequisite as separate work;
read its current repository instructions before edits. Speaker/RAOP, discrete
surround, Chromecast, remote-Stop and long-input/cache-quota deferrals remain.
