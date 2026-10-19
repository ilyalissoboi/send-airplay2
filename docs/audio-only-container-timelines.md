# D105: private edited MP4 and Matroska audio-only timelines

2026-10-19 (Asia/Tokyo), implementation/validation in progress. The user
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
inside the existing seven-day window bound. Existing lazy AAC authoring, quota, cancellation and
joined Stop cleanup remain. Native/public automatic routing, API/binding/CLI,
codec profile, packages and Screenbox are unchanged. No receiver/app test starts.

## Original fixtures and validation

The new generator uses repository-authored stereo tones and existing Apache-2.0
MP4/EBML helpers. It records the actual external development encoder, hashes,
source counts and authored edits in audio-timelines/provenance.json. The pinned
runtime remains FFmpeg 9.0.2 port version 6 with the existing four libraries and
network-disabled LGPL profile; no new dependency or third-party implementation.

Positive files cover PCM offset/cut/initial silence, 44.1-kHz FLAC edits,
48/44.1-kHz Matroska FLAC, PCM and CodecDelay/final-discard PCM. The trimmed MKV
deliberately advertises an unrelated container duration to exercise packet-derived
end calculation. Negatives cover nonzero origin, internal discard and excessive
first skip. Native tests cover rational rounding/coverage, invalid edits and
literal wide-clock/seven-day boundaries. Decoder tests compare independently
authored tone phase/offsets, cold tail, backward/forward windows and AAC output
frame counts; cached reads and exactly-once cleanup remain covered.

Focused native and codec checks pass at an intermediate checkpoint; final full
static/shared/codec suites, UWP compilation and formatting are pending.
These are offline checks, not new receiver interoperability or long-file evidence.

## Next work

After review/merge, define the finite automatic audio-only delivery profile and
connect public reporting/CLI/binding with explicit stereo permission and clear
refusal paths. New Matroska codec/timeline subsets need their own duration/trim
evidence. Then validate native and packaged generated audio-only HLS before
Screenbox routing/queues. Preserve direct MP3/AAC evidence and all speaker,
discrete-surround, Chromecast, remote-Stop and long-input/cache-quota deferrals.
