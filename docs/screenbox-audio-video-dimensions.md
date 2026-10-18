# D99 review follow-up: current and queued video dimensions

2026-10-18 (Asia/Tokyo), code merged; focused Mac gate passed.
Fork #16 is merged as `c10dc464`, with all four checks passed at final head
`a0888786` in [run 39101341534](https://github.com/ilyalissoboi/Screenbox/actions/runs/39101341534).
Merge/CI evidence is separate from the written manual observations below; no release is published.
Native #83 is merged as `e551388`, with
all 17 final-head checks passed at `9d594d8`. It has no review findings. The
user requested addressing the two actionable comments on then-open fork #16,
reviewed head `f4ae4b27`; the existing fork branch was continued after inspecting
its actual head. Current Screenbox instructions were read before C# changes.

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
place with identity/data preserved. Loaded-runtime verification and all focused
written Mac checks pass. Both review threads have replies identifying the
fix/validation scope and are resolved.

The original [1.0.0.13 gate](screenbox-audio-files.md), compiled at `aff57b6f`,
remains historical for its exact bytes. It does not prove this review fix's
compact-mode behavior. The completed focused manual comparison covers current
video proportions before/after cast, M4A to an unopened video with video
controls/compact proportions, transition Stop and rapid Next. The full audio
matrix was not repeated. Written user observations on the Mac provide the visual
confirmation; no Computer Use was used.

An owned 30-second anamorphic fixture derives only from D88's original AAC
flash/beep video: coded 720x576, SAR16:15, display4:3, existing AAC copied.
No new third-party implementation/dependency is introduced. MIME/delivery,
audio policy, package pins, native API, source/license provenance and publication
remain unchanged. The [separate evidence](validation/screenbox-audio-video-dimensions-windows-2026-10-18.json)
keeps current review controls distinct from original receiver observations.

Focused Mac check in development 1.0.0.14: the user confirms that current-video
compact proportions remain correct while casting the owned anamorphic fixture,
receiver video/audio plays, Stop closes receiver playback and local playback
stays paused. Before/after-current snapshots verify AppContainer, the inspected
MSIX executable and all six pinned ci.294 DLL hashes. These are point-in-time
captures, not continuous process monitoring. No new OS-version confirmation is
recorded in this focused exchange. The user also confirms the fresh-session
M4A-to-unopened-video check passes: natural advance, receiver video/audio,
compact video proportions, enabled Aspect ratio and paused local handoff after
Stop. An after-queued runtime snapshot again verifies the executable,
AppContainer and six pinned DLLs. The user confirms both final checks pass:
Next followed immediately by Stop during video startup closes receiver playback,
with no delayed restart after the requested 15-second observation, responsive
Screenbox and local playback paused; rapid Next twice reaches the third audio
item without a delayed return to video, followed by Stop and paused local
handoff. An after-final snapshot again matches AppContainer, the executable and
six pinned DLLs. Final point-in-time owned temporary cache count is zero
children/files/bytes. The requested app restart separates the current-video
and queued-video checks; snapshots do not establish continuous process coverage.
No new Apple TV result is claimed. No further focused manual check is pending.
