# Automatic audio receiver gate (D88)

**Recorded receiver gate completed on 2026-10-15.**
[PR #69](https://github.com/ilyalissoboi/send-airplay2/pull/69) adds the original
controls and private packaged-host harness. The branch starts at merged PR #68,
`f6c85ef`; its 16 baseline checks passed at `9cad595` in run `38689760888`.
Receiver results below belong to this Windows 11 x64 host, Mac14,10 on macOS
26.7.1 and AppleTV14,1 on tvOS 26.6. The user confirms both OS versions unchanged.

| Host and receiver | Playback observed by user | Explicit Stop | Pending-consent cancellation | Runtime verification |
| --- | --- | --- | --- | --- |
| Native shared C API, Mac | AAC copy and DTS 5.1 to stereo AAC | Passed | Passed; prompt appeared and closed | Seven loaded DLL hashes match build |
| Native shared C API, Apple TV | AAC copy on retry and DTS 5.1 to stereo AAC | Not repeated in this slice | Not repeated in this slice | Seven loaded DLL hashes match build |
| Packaged UWP 0.1.28.0, Mac | All nine original format cases | Passed | Passed using manual host Stop; prompt closed and app responsive | AppContainer token and six loaded DLL hashes verified |
| Packaged UWP 0.1.28.0, Apple TV | All nine original format cases | Passed | Not repeated in this slice | AppContainer token and six loaded DLL hashes verified |

Every successful natural-end case includes start near 45 seconds, synchronized
flashes/beeps, LEFT/RIGHT/BOTH direction, pause/resume, forward seek to 90 seconds,
backward seek to 30 seconds and seek to 115 seconds followed by natural end.
The user reports all requested checks worked as expected. These are manual
observations, not instrumented synchronization or discrete-channel measurements.

The packaged matrix copies AAC-LC, AC-3 and E-AC-3 stereo sources, and converts
DTS-core, FLAC, Opus, Vorbis and TrueHD 5.1 plus PCM stereo to two-channel AAC.
Both receivers complete all nine natural-end cases and a further converted DTS
Stop: 18 natural ends and two Stops in total. Successive cases reuse the app's
cast slot after natural cleanup; this is not a Screenbox queue test.

Telemetry reports the expected source family, original selected audio ID 2,
action/output family and two output channels for each packaged case. Accepted
conversion stores clear and release once; unused copy stores release once without
Clear. All owned cache children are removed. Native successful/Stop/cancel runs
report no failed source reads, one source release and media cleanup. Core
binaries are built from `f6c85ef` with no native source changes; package and
actual loaded hashes are in the sanitized record.

## Attempts and limits

The first native Mac AAC observer run omitted final fields/module capture from
its diagnostic whitelist; a corrected repeat supplies that telemetry. An initial
native Apple TV AAC run disconnected at the forward seek with
`url_events/disconnected`; cleanup succeeded and a manual repeat completed all
controls and natural end. Its cause remains unestablished. This is a recorded
successful retry, not an uninterrupted reliability claim or a new automatic retry.
Early Apple TV position telemetry was transient; the user's requested-start
observation is recorded separately from those samples.

Two packaged Mac attempts were receiver-rejected before playback: the initial
AAC attempt and a cancellation attempt whose manual Stop did not arrive before
consent expired. Both cleaned up. After the user signalled readiness, the UWP
retry returned `cancelled` after 2,759 ms; the unanswered prompt closed and the
app remained responsive by user report. Cache counters are clear=1, release=1,
removed=yes. Two identical log snapshots during cancellation describe the same
counters, not additional cleanup callbacks. Native cancellation returned
`cancelled` after 5,058 ms with the same prompt observation and media cleanup.

The [original fixture generator](../tools/audio-codec-spike/generate_receiver_fixtures.py)
authors a visible moving pattern, time labels, a white flash at each second and
matching 100-ms audible pulses. The 3-second LEFT/RIGHT/BOTH cycle tests stereo
direction. Six-channel sources use FL/BL, FR/BR and FC/LFE groups respectively.
The FFmpeg executable is a development-only encoder, not a new library dependency
or redistributed implementation. Original pixels, PCM, container output and
generator remain Apache-2.0. Generated media, sizes, hashes and exact encoding
commands are recorded in [fixture provenance](validation/automatic-audio-receiver-fixtures-2026-10-15.json);
generated media stay in an ignored local folder. No private
video content is used or copied into these controls.

The network-enabled UWP development host now has script-only automatic/forced
stereo options and its optional codec DLLs/notices; existing UI/default delivery
and the published package/Screenbox pin remain unchanged. Use actual binary
hashes/AppContainer tokens and retain fixed category/status logs without device
identifiers, addresses, credentials, paths or URLs. Receiver consent/PIN entry
and observation remain manual. Do not use Computer Use for visual confirmation.
Audio-only speakers and discrete converted surround remain hardware-deferred.

[Sanitized receiver evidence](validation/automatic-audio-receiver-windows-2026-10-15.json)
keeps failed attempts distinct from retries and observer reports distinct from
status telemetry. The private film used in D87 performance checks is not cast.

## Validation and next boundary

The original nine fixtures were generated and prepared successfully, their hashes
were checked, and the generator compiles as Python. Desktop/UWP enabled native
builds and the Native AOT package build pass. Fixed-field per-case checks confirm
18 packaged natural ends, two Stops, one cancelled start, two retained receiver
rejections, expected decisions and complete cache cleanup. Relative documentation
links and `git diff --check` pass. Actual PR-head CI is a separate merge check;
inspect it rather than treating the baseline's 16 checks as this head's result.

No cast is active. The development host remains installed with its existing
pairings and manual fixture grants. The private film from D87 was not cast.
These short original controls do not establish whole-film throughput, arbitrary
media support, additional architectures/receivers or discrete converted surround.
Audio-only speakers and surround-preserving output stay hardware-deferred.

After review and final-head CI, the user may merge PR #69. A separately approved
next slice can plan opt-in package/Screenbox integration of the validated backend,
including runtime dependencies/notices and host fragment storage. Keep defaults,
published package/Screenbox pin and native audio policy unchanged in this gate.
The isolated Apple TV disconnect remains an explicit follow-up observation.
