# Automatic audio receiver gate (D88)

**Checkpoint: resumed by the user on 2026-10-15 after target-device network
adjustments.** Mac discovery succeeds. The user confirms macOS 26.7.1 and
tvOS 26.6 are unchanged. Native Mac AAC-copy and DTS-to-stereo-AAC checks passed by user
observation: synchronized flashes/beeps, channel pattern, start near 45 seconds,
pause/resume, forward/backward seeks and natural end. The driver exited 0 and
reported copied AAC stereo. Its first capture omitted end/cleanup/control lines
and loaded-module evidence; a corrected AAC repeat and the DTS run verify all
seven loaded hashes, successful controls, `media_end`, cleanup and one source
release with no failed reads. Packaged Mac AAC and DTS reached `MediaEnd` and
released their cast slots with six loaded hashes and an AppContainer token
verified; manual observation is pending. One earlier packaged consent attempt
was receiver-rejected and cleaned up before playback. Native Apple TV DTS and
an AAC repeat reached natural end; an initial AAC attempt disconnected during
the forward seek and is retained as a failure. Apple TV manual observations,
the packaged nine-codec matrix and Stop/cancellation checks remain pending.
Original controls and the local native driver are under ignored `build-review`;
UWP script/cache/packaging changes are uncommitted. Do not change pairing or
network settings as an incidental diagnostic action.

The user's Proceed starts `codex/automatic-audio-receiver-gate` from merged
PR #68, `f6c85ef`. All 16 final-head checks passed at `9cad595` in run
`38689760888`. D87's bounded brokered measurements are offline evidence.
This gate adds manual receiver observations, beginning with native AAC copy
and DTS-core 5.1 converted to stereo AAC on the Mac, then Apple TV and packaged
UWP. The user reports both receivers available. Results apply only to the
recorded receiver, firmware, host and media case; the matrix remains incomplete.

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

The network-enabled UWP development host needs script-only automatic/forced
stereo options and its optional codec DLLs/notices; existing UI/default delivery
and the published package/Screenbox pin remain unchanged. Use actual binary
hashes/AppContainer tokens and retain fixed category/status logs without device
identifiers, addresses, credentials, paths or URLs. Receiver consent/PIN entry
and observation remain manual. Do not use Computer Use for visual confirmation.
Audio-only speakers and discrete converted surround remain hardware-deferred.

[Sanitized in-progress evidence](validation/automatic-audio-receiver-windows-2026-10-15.json)
keeps failed attempts distinct from retries and observer reports distinct from
status telemetry. The private film used in D87 performance checks is not cast.
