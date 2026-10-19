# D105: private edited MP4 and Matroska audio-only timelines

2026-10-19 (Asia/Tokyo), private offline gate complete. The user
authorizes this slice after #89 merged as e4d1a28, with all 17 checks passed at
final head 3e59f9c in
[run 39204589746](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39204589746).
The completed native/packaged MP3 receiver gates remain separate evidence.

## Implemented subset

The private audio-only AAC owner admits FLAC/PCM in progressive MP4/MOV with
one finite unit-rate media edit, optionally preceded by one empty edit. A native
helper checks contiguous zero-based source packets, rational source coverage,
positive duration and the seven-day bound before rounding the entire presentation
once to 48 kHz. Empty edits add silence; media offsets do not add presentation
time. Complex/non-unit/zero-duration edits and intervals beyond source coverage
refuse. Wide timescale arithmetic splits whole ticks from remainders.

Calling the private decoder duration method installs the edited MP4 presentation
endpoint for subsequent windows. The decoder can emit PCM after an edit's end,
so this view clips at that checked endpoint while still requiring decoded sample
coverage. EOF here means the intentional presentation boundary, not physical EOF
of excluded encoded media. Default/video callers do not install this bound.

Matroska admission is limited to canonical indexed FLAC/PCM with contiguous
zero-based audible coverage. Existing packet counts/packed PCM sizes, CodecDelay
and signed DiscardPadding derive the endpoint. The existing sparse restart
anchors, callback/header budgets and clock precision checks stay in effect.
No host or FFmpeg container-duration estimate is trusted. Nonzero audible origin,
internal trims/gaps and other Matroska codecs refuse in this initial subset.

The audio-only owner validates the decoded tail and requires full window coverage
for these new timelines. Its extra EOF probe needs one output sample of headroom
inside the existing seven-day window bound. Existing lazy AAC authoring, quota,
cancellation and
joined Stop cleanup remain. Native/public automatic routing, API/binding/CLI,
codec profile, packages and Screenbox are unchanged. No receiver/app test starts.

## Original fixtures and validation

The new generator uses repository-authored stereo tones and existing Apache-2.0
MP4/EBML helpers. It records the actual external development encoder, hashes,
source counts and authored edits in
[fixture provenance](../tools/audio-codec-spike/fixtures/audio-timelines/provenance.json). The pinned
runtime remains FFmpeg 9.0.2 port version 6 with the existing four libraries and
network-disabled LGPL profile; no new dependency or third-party implementation.

Positive files cover PCM offset/cut/initial silence, 44.1-kHz FLAC edits,
48/44.1-kHz Matroska FLAC, PCM and CodecDelay/final-discard PCM. The trimmed MKV
deliberately advertises an unrelated container duration to exercise packet-derived
end calculation. Negatives cover nonzero origin, internal discard and excessive
first skip. Native tests cover rational rounding/coverage, invalid edits and
literal wide-clock/seven-day boundaries. Decoder tests compare independently
authored tone phase/offsets, including an in-memory edit at source tick 22051
(44.1 kHz) between output samples, cold tail, backward/forward windows and AAC output
frame counts; cached reads and exactly-once cleanup remain covered.

Runtime implementation 6b81756 passes Windows Release static/shared suites
43/43 and 44/44 (45.97/50.93 seconds). The final codec suite, including the added
fractional-offset regression, passes 17/17 (76.79 seconds). Strict Windows-host
Clang 22.1.3 syntax checks pass for all five touched translation units; all eight
touched C++ files pass clang-format dry-run/Werror and git diff --check passes.
An adjacent exception_ptr now has an explicit null initializer, preserving its
behavior while satisfying Clang's aggregate initialization warning.

Both the ordinary x64 UWP library and private codec adapter compile. Native
binary/import checks and five probe/codec DLL AppContainer/app-runtime checks
pass. The [validation record](validation/audio-only-container-timelines-windows-2026-10-19.json)
captures exact sources, fixture hashes and build/test scope. The existing cached
FFmpeg 9.0.2 port-6 dependencies are reused without changing the profile.
These are offline checks, not new receiver interoperability or long-file evidence.

## Next work

After review/merge, define the finite automatic audio-only delivery profile and
connect public reporting/CLI/binding with explicit stereo permission and clear
refusal paths. New Matroska codec/timeline subsets need their own duration/trim
evidence. Then validate native and packaged generated audio-only HLS before
Screenbox routing/queues. Preserve direct MP3/AAC evidence and all speaker,
discrete-surround, Chromecast, remote-Stop and long-input/cache-quota deferrals.
