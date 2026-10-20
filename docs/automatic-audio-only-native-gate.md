# D107: native automatic audio-only receiver gate

2026-10-20 (Asia/Tokyo). **The native gate passed.** Written user observations
confirm 24 format/control/natural-end checks and two representative converted-FLAC
Stops on MacBook Pro/macOS 26.7.1 and Apple TV/tvOS 26.6. Versions are unchanged.
[PR #92](https://github.com/ilyalissoboi/send-airplay2/pull/92) records this gate;
the [validation record](validation/automatic-audio-only-native-windows-2026-10-20.json)
separates every sender attempt from its written observation.

The user authorized this slice after
[PR #91](https://github.com/ilyalissoboi/send-airplay2/pull/91) merged as `d378249`.
Its final head `3daa9a1` passed all 17 checks in
[run 39246535013](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39246535013).
The [API 6 profile and finite subset](automatic-audio-only.md) are unchanged.
This slice adds an owned fixture generator and receiver evidence. It changes no
runtime/API, codec profile, quota, package, Screenbox or publication behavior.

## Build and preparation

The Windows x64 shared native C API host uses merged `d378249` runtime source,
`native_changes=no`, and the existing enabled FFmpeg 9.0.2 port-6 backend.
Initial test checkpoint `4cd545a` and resumed checkpoint `a3ff5bd` have identical
executable/seven-DLL hashes. All 30 attempts verified actual loaded paths and
bytes against that common ledger before setup/playback. Host runtime reports
Microsoft Windows 10.0.26200, X64. The prepared PR head `4cd545a` passed all 17
CI checks in [run 39364170075](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39364170075);
inspect the final documentation/evidence head separately. CI is not receiver evidence.

The [development fixture generator](../tools/audio-only-probe/generate_automatic_fixtures.py)
reuses the repository's original 64-second pulsed LEFT/RIGHT/BOTH tone author
and checked MP4 edit author. Development FFmpeg/ffprobe versions and generated
hashes are recorded separately from the runtime backend. Generated media stays
in ignored local storage. Existing committed 48-kHz CBR and 44.1-kHz VBR MP3
fixtures retain their original bytes/provenance. No private/downloaded media or
new third-party dependency is included; original fixture authorship is Apache-2.0.

All ten new fixtures passed the actual public `--check-audio --audio-only` path
with expected selected identity, source/output families, route and stereo output;
every offline cache ended empty. Receiver discovery used unique recorded models
and saved pairings. No credentials, receiver/network settings or consent settings
were reset. Addresses, unique identifiers, PINs, bearer URLs and private media
labels are excluded from committed evidence.

## Observed results

Every row below passed on both recorded receivers. Progressive AAC and MP3 use
action/reason 4; converted inputs use action/reason 3 and stereo AAC output.
All fixtures contain audio only, with no dummy video.

| Input | Delivery | Mac | Apple TV |
| --- | --- | --- | --- |
| AAC/M4A | Original progressive AAC | PASS | PASS |
| CBR MP3, 48 kHz | Original progressive MP3 | PASS | PASS |
| VBR MP3, 44.1 kHz | Original progressive MP3 | PASS | PASS |
| Native FLAC | Converted AAC HLS | PASS | PASS |
| WAVE PCM | Converted AAC HLS | PASS on retry | PASS |
| Ogg Opus | Converted AAC HLS | PASS | PASS |
| Ogg Vorbis | Converted AAC HLS | PASS | PASS |
| MP4 FLAC | Converted AAC HLS | PASS | PASS |
| MOV PCM | Converted AAC HLS | PASS | PASS |
| Matroska FLAC | Converted AAC HLS | PASS | PASS |
| Matroska PCM | Converted AAC HLS | PASS | PASS |
| Edited MOV PCM | Converted AAC HLS | PASS | PASS |

For each natural-end case, the user confirmed audible RIGHT pulses from the
requested start near eight seconds, pause/resume, forward seek to 32 seconds
(higher BOTH marker), backward seek to eight (RIGHT), then natural end after
seeking near 58. Edited PCM has silence in [0,8), then source [8,64), preserving
marker positions after eight seconds. Separate FLAC casts from zero played LEFT
pulses and stopped after eight seconds, with no delayed restart on either receiver.

Telemetry separately confirms the expected automatic decision/original selected
ID, successful controls and natural EOF or requested Stop. Every one of the 30
attempts released its source once, reported zero failed source reads and media
cleanup, and ended with an empty cache. The final live inspection found zero test
host processes and zero cache entries. CLI counters do not expose generated-store
callback counts; source release and physical cache emptiness are the measured facts.

## Pause, retries and retained failures

Seven cases had completed before the user paused. The original Mac WAVE attempt
returned receiver HTTP 400 after 15.39 seconds, before active playback; the batch
aborted and seven following cases never started. The timing resembles historical
consent expiry, but its cause is unproven. A later batch Yes reply was retained
without counting those unrun cases as passes.

The user explicitly resumed by requesting missing tests. Only the eight missing
Mac cases (including WAVE retry) and eleven missing Apple TV cases ran. All
completed successfully. The Mac reply arrived after its whole resumed sequence.
The initial Apple TV reply arrived early; a final **All passed** confirmation
after completion covers its remaining cases. No earlier completed case was rerun.

Three consent attempts remain nonqualifying: the first timer fired after 5088 ms,
but the user requested a retry and did not confirm prompt closure. Two retries
started before the timer (1866/4087 ms, stop_fired=no); the user saw no prompt.
They establish no fresh pending-consent cancellation pass. Failed-start URL
cleaned=no remains distinct from source/media cleanup in the raw records.

The user questioned redundant cancellation retesting for unchanged code.
**No further D107 cancellation test is required.** Reuse earlier lifecycle
evidence; repeat manual cancellation only for relevant lifecycle/routing/ownership
changes or a concrete defect. Do not force consent by resetting settings or
repeat unchanged cancellation automatically with every pass.

## Limits and next gate

Written listening observations establish audible receiver behavior; sender
telemetry alone does not. Early state snapshots can be near one or two seconds
despite the requested eight-second start. Preserve these alongside the audible
marker confirmation without claiming sample-exact positioning. These bounded,
seek-based 64-second checks do not establish gapless playback, full-duration
sustained throughput or long-input/cache scaling. No Computer Use visual
confirmation was used.

After review/merge, validate packaged automatic audio-only HLS and brokered-source
ownership/cleanup, then Screenbox routing and per-item queues. Scope manual checks
to the changed paths and reuse unchanged lifecycle evidence. Speaker/RAOP and
discrete-surround hardware, Chromecast, user-deferred remote-Stop classification
and separate long-input/cache-quota evaluation remain deferred. D107 does not
publish a package or release.
