# D99 review follow-up: current and queued video dimensions

2026-10-18 (Asia/Tokyo), in progress. Native #83 is merged as `e551388`, with
all 17 final-head checks passed at `9d594d8`. It has no review findings. The
user requests addressing the two actionable comments on open fork #16, reviewed
head `f4ae4b27`; the existing fork branch is continued after inspecting its actual
head. The current Screenbox instructions are read before C# changes.

Implementation `c8cc5584` preserves VLC's natural display dimensions when an
already-open video begins casting; raw track dimensions no longer override
rotation/pixel-aspect handling. M4A remains `(0, 0)`. A queued video gets its
own metadata rather than the preceding audio player's zeros. If not parsed,
a separately owned playback descriptor/brokered grant uses the existing bounded
WinRT-file parser and is disposed in `finally`. Stop and supersession cancel
metadata; the admission ticket is checked before creating a source/native cast.
Missing/invalid dimensions fail through normal paused local handoff rather than
activating a video with audio-only dimensions. SAR is applied before orientation
axis swaps, with zero/overflow guards. No local playback is opened for this read.

Both VS 2026 MSBuild Core profiles pass. Pure logic suites pass 142/142 ordinary
and 157/157 opt-in. Tests cover current natural-size preservation, not-yet-parsed
queued metadata, M4A bypass, cancellation, all eight orientations, unspecified
SAR and invalid/overflowing dimensions. No XAML type is instantiated. Both
actual MSIX/pinned payload checks pass. Development 1.0.0.14 is installed in
place with identity/data preserved; loaded-runtime and focused written
layout/queue checks are pending. Both review threads have replies identifying
the fix/validation scope and are resolved.

The original [1.0.0.13 gate](screenbox-audio-files.md), compiled at `aff57b6f`,
remains historical for its exact bytes. It does not prove this review fix's
compact-mode behavior. A focused manual comparison is proposed, without repeating
the complete audio matrix: current video proportions before/after cast, then
M4A to an unopened video with video controls/compact proportions, plus Stop during
transition. No Computer Use visual confirmation is permitted. Receiver availability
for that check is pending; do not start uncoordinated casts.

An owned 30-second anamorphic fixture derives only from D88's original AAC
flash/beep video: coded 720x576, SAR16:15, display4:3, existing AAC copied.
No new third-party implementation/dependency is introduced. MIME/delivery,
audio policy, package pins, native API, source/license provenance and publication
remain unchanged. The [separate evidence](validation/screenbox-audio-video-dimensions-windows-2026-10-18.json)
keeps current review controls distinct from original receiver observations.
