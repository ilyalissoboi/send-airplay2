# Clean-main opt-in package candidate (D90)

2026-10-16. The user's Proceed authorizes preparing and validating the candidate
following merged D89/PR #70. Publication is a separate approval, following the
existing [GitHub prerelease procedure](nuget-package.md#github-prereleases-for-the-screenbox-fork).
Screenbox implementation is a later slice; [the integration plan](screenbox-automatic-audio-plan.md)
records concrete changes without modifying that repository.

## Source and artifact selection

The candidate must come from the `nuget-stereo-aac-x64` artifact of a
`workflow_dispatch` build on merged main, rather than from a PR artifact or local
package cache. Main source is `e6acba1f34b77ddd2a370a65209748f413bf2c8a`.
[Run 38836333166](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38836333166),
run number 282, was dispatched on that commit. The candidate identity/version is
`SendAirPlay2.StereoAac` / `0.5.0-ci.282`.

Downloaded asset: `SendAirPlay2.StereoAac.0.5.0-ci.282.nupkg`, 21,306,805 bytes.
SHA-256:
`5280f0868721476b2cae9018bd8197c3de3c9f356408781209ec57fd4b9ba4ff`.
The adjacent `.nupkg.sha256` sidecar records that same digest. The package bytes
have not been modified after download.
[Sanitized validation evidence](validation/opt-in-audio-release-candidate-windows-2026-10-16.json)
records native/source hashes and the separate local/CI/publication states.

The separate codec-free artifact from the same run is a comparison input for the
consumer conflict test, not an additional publication candidate. Published
`SendAirPlay2 0.3.0-ci.207` and Screenbox's existing pin stay unchanged.

## Required validation before publication approval

- Check the workflow's actual source commit and all final checks.
- Download the two package artifacts and preserve their original bytes.
- Check clean `BUILD-INFO.txt` attribution for the binding and every native
  source stamp against the main commit; recompute every recorded DLL hash.
- Check six x64 UWP DLLs, private exports disabled, codec import closure and
  exactly one crypto runtime; require the pinned FFmpeg source archive,
  configuration, overlay/patches, notices and rebuild inputs.
- Restore/build a fresh consumer against this exact audio package. Verify API-5
  compilation, all six copied native hashes and clear unsupported-RID,
  direct-profile and transitive-profile refusals against the same-run default.
- Record whole-package SHA-256/size and prepare exact release notes before asking
  to publish. Do not infer receiver or Screenbox results from these checks.

## Proposed publication

Local candidate checks pass: clean main attribution, six recorded/copy hashes,
UWP distribution properties, pinned source archive, complete source/notice/rebuild
payload and consumer API-5/RID/direct/transitive-conflict checks. The same-run
default package is codec-free with clean binding/three-architecture source stamps
and six matching DLL hashes. All 17 final main CI checks passed at `e6acba1`,
including the independent CI package-consumer job. The UWP audio job also passes
all 12 profile controls. This completes preparation; publication approval remains
pending for the exact candidate above.

An initial local overlay audit assumed checkout bytes would match across hosts.
CI's CRLF checkout differs from the local LF checkout in 21 of 22 files. All 22
packaged files match their installed SPDX SHA-256 records exactly and match the
repository text after CRLF/LF normalization. Only that comparison assumption was
corrected; the candidate archive was not rewritten. Original source tar SHA-512
matches the pinned value without normalization.

Use a distinct tag namespace, `nuget-stereo-aac-v0.5.0-ci.282`, targeting the
source commit above. Title: `SendAirPlay2.StereoAac 0.5.0-ci.282 (x64 prerelease)`.
Attach the unchanged `.nupkg` and its SHA-256 sidecar; use the reviewed release
notes as the release body. This is a GitHub prerelease, not a nuget.org publication
or Store result.
After approval/publication, independently download the public asset and verify
its hash before any Screenbox pin change. A version/tag collision or changed
candidate bytes requires a new review, not overwriting the recorded candidate.

[Prepared release notes](releases/sendairplay2-stereo-aac-0.5.0-ci.282.md) describe
the profile, host obligations, redistribution payload and evidence limits.

No public release or tag has been created by this preparation step.
