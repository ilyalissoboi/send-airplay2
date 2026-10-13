# Stereo AAC HLS authoring policy and audit (D73)

The latest Proceed authorizes this private increment after PR #53 merged as
`25b84a1`. It defines metadata/admission policy and validates it offline before
public playback/storage integration. Shipping delivery defaults, packages and
receiver evidence are unchanged; speaker audio/discrete surround remain deferred.

## Requirements and engineering decisions

[RFC 8216 sections 4.1 and 4.3.4.2](https://www.rfc-editor.org/rfc/rfc8216.html#section-4.3.4.2)
define peak rate over contiguous segment groups spanning 0.5–1.5 target durations,
including container bytes. Playable rendition peaks/averages are summed, with the
largest subtitle alternative. Before encoding completes, the RFC recommends a
representative period encoded with the same settings.
[Apple's authoring specification](https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html)
requires both bandwidth attributes and VOD measured values within 10% of declarations;
it recommends nominal six-second segments and common audio/video targets.

The adopted lazy policy measures the first audio segment using the exact serving
encode/fragment settings. It uses a separate decoder, discards encoded output and
retains only byte count/duration/rates. Calibration must not perturb the serving
decoder's startup/seek history: reusing it exposed changed first-window output
and an empty repeated-window outcome in the synthetic fixture. Isolation preserves
the established serving lifecycle, without claiming a general decoder-history fix.
The first period may be unrepresentative. Metadata is explicitly an estimate,
not guaranteed whole-VOD accuracy or an upper bound. There is no full conversion,
cache sealing, padding to fabricate bitrate, or mutable master-refresh workaround.

Copied progressive MP4 video and generated WebVTT sizes are exact, including their
container/header bytes. Their rates use finalized common playlist targets. Indexed
MKV video retains cue-cluster byte estimates; unread indexed subtitles are marked
unmeasured. `authoring()` exposes immutable private provenance without I/O.
Existing shipping remux bitrate behavior is retained; only private stereo composition
uses the new calculator. Both bandwidth attributes remain in its master playlist.

Any copied-video segment over 20 seconds is refused, including the final segment.
This engineering cap matches the existing indexed-MKV span budget. It permits the
owned 16.5-second GOP with three bounded audio segments and common target 17;
an owned encoded 33-second GOP fails startup and clears host storage before sealing.
It is an explicit departure from the nominal six-second recommendation, not a
receiver compatibility claim. No video re-encoding or silent fallback is added.

## Calculation and resource limits

The private calculator uses EXTINF microseconds and complete segment byte counts,
excluding init files and HTTP overhead. It rounds integer bitrates upward; a
complete short presentation without a qualifying span uses its average as an
explicit fallback. The same fallback supplies a first-period estimate when that
period is shorter than half the common target. Bounds are 201602 segments, seven
days, 1 TiB aggregate bytes, targets 1–20 seconds and two million inspected groups.
Malformed/overflow/budget cases fail; checks precede unsafe arithmetic.

AAC remains 48 kHz/stereo/192 kbit/s ABR with the existing downmix and epoch bounds.
Startup now performs one additional bounded first-period decode/encode and opens
an isolated decoder. The synthetic hour test keeps metadata below 32 MiB and
raises its startup payload allowance from 1 MiB to 4 MiB. No fragments are sealed
at startup. Cold generation retains its 4 MiB payload bound per requested fragment.

## Offline evidence and limits

Literal tests distinguish grouped peak from the highest individual short-segment
rate, include the exact 1.5-target boundary, verify upward rounding/large arithmetic,
and refuse malformed/overflow/CPU-budget inputs. GOP tests retain copied payloads,
audio duration, decoder lifecycle and once-only cleanup. All original fixture hashes
are retained; the new rejected-GOP fixture records original generation/provenance.

Complete offline fixture audits measure served video/audio/WebVTT sizes, sum playable
rates and compare both master declarations with measured whole-VOD values:

| Input | Peak declared / measured (bit/s) | Average declared / measured | Within 10% |
|---|---:|---:|---|
| DTS MP4 | 196072 / 196775 | 195567 / 195957 | Yes |
| DTS indexed MKV | 1622915 / 196775 | 1621266 / 195957 | **No** |
| Short AAC MP4 | 101876 / 101902 | 101371 / 40931 | **No** |
| Edited short AAC MP4 | 90425 / 90386 | 89920 / 36747 | **No** |

Regular AAC/AC-3/E-AC-3 MP4 and DTS MP4 with subtitles/B-frames also pass the recorded
tolerance audit. The early-ending sources demonstrate why measuring the first
period cannot predict later silence. MKV cluster spans include original source
audio/overhead, not just copied video. Do not relabel these failed audits as passes.
The [artifact](validation/stereo-aac-authoring-windows-2026-10-13.json) records tested
source/fixture hashes, all audit rows and local validation scope.

These are native offline checks, not Apple's Media Streaming Validator, full
authoring conformance, lip-sync/receiver seeking or packaged-builder runtime proof.
Whole-VOD accuracy for arbitrary lazy sources remains a limitation. A future strict
authoring path needs a complete preparation/audit or another validated estimator;
it must preserve the user's startup, memory and explicit-delivery requirements.

Next: integrate the explicit stereo delivery/selection/cache contracts through
playback/C ABI/bindings/CLI with failure supervision, carrying estimate provenance
and these limits. Then check real-file/host performance, a new packaged runtime
and manually observed Apple TV/Mac playback. Ask before merging or publishing.

Implementation checkpoint: [PR #54](https://github.com/ilyalissoboi/send-airplay2/pull/54)
is open. At `3cd620e`, local codec 4/4, static/shared 35/35 and 36/36, runner
10/10 and x64 UWP compilation/import checks pass. This documentation follow-up
changes no native behavior; inspect the actual final PR-head CI before merging.
