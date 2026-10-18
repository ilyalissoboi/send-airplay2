# D99 Screenbox AAC/M4A audio-file routing

2026-10-18 (Asia/Tokyo), recorded host gate passed. The user authorizes the
implementation slice after native checkpoint PR #82 merges as `afb9ee8`; all 17 checks pass at
its final head `481ab01`. D97 native and D98 packaged UWP AAC/M4A gates are
complete; they do not prove this Screenbox build. Fork main is `7cea7023`.
The current fork instructions were read before editing.

Fork [PR #16](https://github.com/ilyalissoboi/Screenbox/pull/16), implementation
`aff57b6f`, adds per-item delivery/MIME selection: local `.m4a` uses progressive
`audio/mp4` in both existing package profiles. Video retains HLS remux or the
opt-in automatic audio profile. Audio-only files bypass conversion storage even
when the saved stereo permission is on. Matching uses the extension, not VLC
parse state, so it also works for unopened queue items. M4A is a container hint;
only compatible AAC/M4A is in the validated scope, not every codec/protected file
that can use that extension. There is no receiver-failure transcoding fallback.

The coordinator keeps the existing serial cleanup, supersession, cancellation,
natural-end/repeat/queue handling and paused local handoff. M4A dimensions are
zero; queued video uses its parsed size when available and the existing fallback
otherwise. The format notification becomes media-neutral. No new layout, setting,
codec/demuxer, dependency, source/license provenance or native API is introduced.
Ordinary ci.207 and opt-in ci.294 pins are unchanged. This is original host code;
existing Screenbox/native and redistributed third-party licenses are preserved.

## Validation checkpoint

Required VS 2026 MSBuild builds both Core profiles. Pure logic suites pass
122/122 ordinary and 137/137 opt-in, including routing and existing cleanup/
admission controls. No XAML types are instantiated. Both actual Release MSIX
builds and two/40 pinned-payload controls pass. Existing translation, legacy
migration and third-party trim warnings remain. Development 1.0.0.13 is
registered in place from the actual opt-in MSIX, with only the extracted manifest
version adjusted and source manifest/identity/data preserved. Loaded-runtime
checks pass for the actual pre-gate AppContainer, inspected executable and all
six loaded published ci.294 DLLs. All four fork CI checks pass at `9c5202bf`,
and all 17 native checkpoint checks pass at `5502e8e`; inspect final documentation
head CI separately. Fork release is intentionally skipped. The user confirms
both available with macOS 26.7.1/tvOS 26.6 unchanged.

## Written receiver observations

| Case | MacBook Pro | Apple TV |
| --- | --- | --- |
| Unanswered consent, Screenbox Cancel | Prompt closes; responsive, local paused, no error | Not exercised |
| M4A, conversion OFF | Current-position start near 8 s, tones, pause/resume, seek to 32/back to 8, Stop/local paused pass | Same checks pass |
| M4A, conversion ON | Same audible controls/Stop/local-paused checks pass | Same checks pass |
| Conversion ON, repeat OFF, natural audio/video/audio queue | Correct output, natural advance and local paused at queue end pass | Same checks pass |
| Rapid Next twice, then separate transition Stop | Third audio reached; Stop prevents delayed restart, local paused, responsive | Same checks pass |

The user answers "Everything worked as expected" ("working" for the Mac OFF
case) or "Yes" to the corresponding written checks. These are independent
manual observations, with no Computer Use visual confirmation or timed sender
trace. The queue procedure permits seeking near each clip's end before natural
advance; it does not establish uninterrupted playback of all 64/120-second files.
Repeat-one handling is retained in code but has no new D99 receiver observation.
No D99 playback failure is reported.

After the tests, the process check finds no Screenbox process. The user explains
"I closed screenbox manually earlier. It is now reopened." The registered
executable and six native files still match on disk; the reopened process
independently passes AppContainer/executable/six loaded pinned hashes. This is
not a same-process before/after capture or a reported crash. Conversion OFF and
paused playback were requested on reopen; no additional cast was requested.

One 120-second cache sample window observes zero directories/bytes. A separate
345-second combined enabled-audio/video window (1,306 samples, nominal 250-ms
interval) observes at most one empty child and ends at zero directories/bytes,
with no unexpected entry. These windows have no per-action timestamps and do
not establish uninterrupted case-level coverage or attribute the child to M4A.
The progressive route's cache bypass is a code property; combined sampling is
not proof that no short-lived allocation escaped observation. Final owned cache
is empty. No further D99 receiver check is pending.

The [sanitized evidence](validation/screenbox-audio-files-windows-2026-10-18.json)
records exact MSIX/executable/native/fixture hashes, user responses, observation
limits and the intentional-close/reopened-process distinction. Hardware scope
is the inspected opt-in x64 package; ordinary ci.207 passes logic/build/payload
checks only. Fork #16/native #83 await review/merge; do not merge automatically.

## Boundaries and follow-up

No new native/fork release or pin update is part of D99. Audio-only conversion
of unsupported formats is a distinct follow-up requiring an audio-only output
presentation/timeline plus any missing standalone input-container support; it
can reuse existing decoder/downmix/encoder components, but the current conversion
path derives segments from video. Existing decoder support within MP4/MKV does
not establish standalone FLAC/Ogg/Opus/WAV support. This follow-up was discussed,
not implemented by the current AAC/M4A slice.

Speaker/HomePod/RAOP, discrete converted surround, Chromecast hardware validation
and user-deferred remote-Stop classification remain separate/deferred. No
long-audio or uninterrupted-playback claim is made by the bounded owned fixture.
