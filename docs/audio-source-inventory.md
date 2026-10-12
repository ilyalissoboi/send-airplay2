# Native audio source inventory and seek metadata (D69)

2026-10-12. The user's **Proceed** authorizes the first native-integration
increment after the merged [D68 codec spike](stereo-aac-codec-spike.md).
This implements private container/source foundations for the
[stereo AAC plan](stereo-aac-casting-plan.md). It does not add a playback delivery
value, public ABI/binding/CLI option or encoded HLS resource.

## Inventory and selection

`media_track_inventory.h` separates source metadata from remuxable tracks:

- MP4 identities are `tkhd.track_ID`; Matroska identities are `TrackNumber`.
  Both are retained in a 64-bit field, without substituting a filtered array
  index or generated HLS track ID. Zero or duplicate identities are malformed.
- Include enabled and disabled video, audio and other tracks, including unknown
  codecs and protected entries. Preserve the first complete MP4 sample entry
  (including subtitle/timed-metadata entries) and its full description count,
  or Matroska CodecPrivate,
  MP4 language/edit clocks, and Matroska labels, normalized language tags,
  default flags, CodecDelay, SeekPreRoll and DefaultDuration. MP4 labels/roles
  beyond its language code are not parsed in this increment.
- Automatic MP4 selection uses sole enabled audio, otherwise returns
  selection-required. Matroska additionally accepts a unique default among enabled
  audio tracks. No enabled audio yields video-only in automatic mode.
- An explicit identity must name enabled audio. Missing, disabled or non-audio
  identities return invalid-selection and never fall back. A selected unknown or
  protected codec remains the selected source, for the later decoder gate to
  refuse; inventory/selection does not advertise decoder capability.

`probe_media_tracks` borrows the existing immutable `MediaSource` on the calling
thread. It calls size once, handles short reads in capacities up to 64 KiB, and
checks the caller's absolute cancellation/deadline context around reads and after
metadata parsing. Source exceptions propagate. No callbacks are retained or run
after return, and no native filesystem/network access or new dependency is added.

The limits are 128 tracks, 4096 top-level MP4 or pre-cluster Matroska elements,
the existing 64-MiB MP4 moov/16-MiB Matroska metadata limits, and for the host
adapter 128 MiB of requested bytes and 1,048,576 read callbacks. Matroska's bounded
16-KiB reader window can fetch nearby payload bytes, but the inventory does not
scan or decode frames. Inventory retains any nonzero Matroska TimestampScale
verbatim; the native video index separately requires a scale dividing one second.
MP4 inventory records multiple descriptions, while selected audio/video indexing
retains its single-description restriction. Fragmented MP4 remains unsupported.
These are engineering bounds, not receiver limits.

## Video indexing and audio clocks

New video-source entry points reuse native MP4 and Matroska indexing without
requiring remuxable audio. They preserve supported video sample entries, source
payload locations, reorder/edit timing and text subtitle handling. Matroska has
both full-scan and existing Cues-index paths. Use a validated inventory and an
immutable source with these private low-level functions. Existing remux entry
points retain their AAC/AC-3/E-AC-3 selection policy; the new source path is a
separate foundation for the later delivery mode.

The selected MP4 audio source can be indexed by its original track identity
without an audio codec filter. Protected entries are refused. Its sample table
is validated once for positive durations, contiguous decode times and absence of
audio composition offsets. Subsequent packet-anchor lookups are logarithmic.

The MP4 seek planner translates requested presentation nanoseconds through zero
or one unit-rate media edit, optionally preceded by an empty edit, and retains
media offsets in audio ticks. It chooses a packet at or before the target minus
caller-supplied decoder preroll, records the discard target, marks initial silence
and reports EOF at the exact edit/index end. Rational comparisons avoid rounding
empty/edit boundaries to nanoseconds before converting to the audio clock.
Complex edit sequences/rates, composition offsets and arithmetic overflow are
refused instead of guessed.

Matroska seek-time planning adds CodecDelay to the desired audible timestamp,
then subtracts the larger of container SeekPreRoll and a caller-supplied decoder
requirement, saturating at zero. It retains nanosecond precision. Non-default
TrackTimestampScale/TrackOffset are marked as unsupported timeline metadata and
refused by this helper; the new video-source index also refuses them on video.
Cues/first-cluster offsets are locators, not validated audio decoder entry points.
These helpers use original container clocks; backend integration must establish
which edit/delay corrections its decoder already applied, preventing double
application of elst or CodecDelay.

## Validation and remaining work

Synthetic MP4/MKV tests cover commentary-first/reversed order with stable IDs,
ambiguous/default/disabled selection, retained unsupported/protected identities,
invalid/non-audio/duplicate/zero IDs and resource bounds. Packet offsets and video
entries are checked independently. Existing remux selection is exercised alongside
the new source path. Indexed Matroska video tests request a later segment before
the first, while its audio is marked DTS. The sample payloads are synthetic index
fixtures, not proof that a DTS movie decodes.

Host-callback tests exercise tiny short reads, size-once, cancellation before and
during I/O, absolute deadline expiry and zero/oversized reads. Timeline tests use
literal clock expectations, forward/backward targets, edit silence/end boundaries,
fractional thirds of a second, CodecDelay/preroll and overflow refusals.
Run the full static/shared CMake/CTest checks in README; the focused executable
checks are `remux_tests` and `mkv_remux_tests`.

Local Windows x64 validation: full static **35/35** and shared **36/36** CTest
passed, including the existing pure-C/ABI/C# binding checks. Touched C++ is checked
with the repository clang-format policy and whitespace checks. PR #48 merged as
`5d46479` on 2026-10-13 after all 16 checks passed at final head `c816709` in run
`38268265899`; both metadata review findings are fixed and resolved. None of these
tests is a receiver run.

Still required: bind these identities to actual decoder streams, prove compressed
audio seeks/resampler history and frame changes, generate AAC with correct
priming/padding/fMP4 timing, expose separate audio HLS resources with bounded
generation/cache, extend the public ABI/bindings/CLI, then run packaged/receiver
gates. This increment supplies metadata and packet-anchor arithmetic, not decoded
seek accuracy, new receiver interoperability or a package publication. Audio-only
speakers and preserved 5.1/7.1 output remain deferred.
