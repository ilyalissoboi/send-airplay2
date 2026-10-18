# D99 Screenbox AAC/M4A audio-file routing

2026-10-18 (Asia/Tokyo), in progress. The user authorizes the next implementation
slice after native checkpoint PR #82 merges as `afb9ee8`; all 17 checks pass at
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
checks await the user opening Cast; no receiver result is attributed yet. The user confirms both Mac and Apple TV
available with macOS 26.7.1/tvOS 26.6 unchanged. Written manual Screenbox checks
are pending: consent cancellation, audio start at the current position, audible
controls, Stop/local paused handoff, mixed audio/video/audio natural advance,
rapid Next and Stop during a transition. No Computer Use visual confirmation is
permitted. Keep exact compiled source/binary identity and any failed attempts.

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
