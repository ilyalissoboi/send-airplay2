# Automatic audio AppContainer and performance gate (D84)

Authorized by the user's Proceed on 2026-10-15 (Asia/Tokyo), from merged
[PR #64](https://github.com/ilyalissoboi/send-airplay2/pull/64), `2bdd397`.
All 16 checks passed at final head `4126ca9` in run `38593843522`.
The branch is `codex/automatic-audio-uwp-runtime`. This continues ordered step 4
of [the expansion plan](automatic-audio-transcoding-plan.md). Speaker transport,
discrete converted surround, publication and Screenbox pin changes remain deferred.

## Implementation and scope

The existing offline x64 Native AOT UWP harness advances to private package
0.1.0.4. It retains D75's forced-stereo checks and adds a shared
[owned fixture catalog](../tools/uwp-host/stereo-validation/automatic-cases.tsv)
covering automatic DTS, FLAC, Opus, Vorbis, packed PCM and TrueHD conversion,
AAC/Dolby copy and video-only. The catalog gives literal action, family, channels
and segment expectations, rather than deriving them from the implementation.
PCM containers/packing variants, surround sources, coarse clocks and per-block
trim controls are represented; this is the recorded finite subset, not arbitrary
formats. The differing coded/output-rate AAC control remains outside the first
automatic copy table and is an explicit public media-unsupported check.

The new 160-byte **private** automatic runtime report is compiled only with
`SAP2_RUNTIME_VALIDATION`; the original 48-byte hook remains. Both adapt the same
production owners and source/cache callback bridges. No conversion implementation,
public API, codec profile or dependency is added. Resource scenarios cover cold
late/concurrent reads, byte-identical sealed rereads, backward/all-resource reads,
request cancellation/retry, cache/source failure, invalid identity, preservation,
preparation cancellation, quota admission and idempotent Stop. Converted retained
callbacks must refuse after Stop; copied callbacks are drained/dropped before
context release, consistent with their existing contract. Copy read failures
remain resource failures without inventing a conversion-worker cause or fallback.

The package uses brokered `StorageFile` and an exclusively reserved TemporaryFolder
child, with sibling preservation checks. Normal fixture source reads are 1021
bytes and cache reads 997. Automatic copy/pre-routing refusal releases an unused
store without Clear. The adapter now deletes its exclusively reserved, still-empty
child on unused release; it neither clears a parent/sibling nor hides unexpected
files. Accepted conversion retains one-time clear/release after drain. Public
checks exercise the real binding's borrowed source/owned store contract and
unstarted/absent-credential/invalid-option handle lifetimes without network work.

The `pick-expanded` activation asks the user to grant the repository's owned
fixture root through a folder picker. Each outside-package file must hash-match
its bundled owned counterpart before the same matrix runs. The package has no
network capability and neither grants itself broad filesystem access nor reads
credentials. Computer Use is not used for visual confirmation or picker selection.

## Bounded performance measurements

The user separately authorized read-only measurements on a local video. Its
path/name, contents and source hash are excluded from committed evidence. The
`benchmark` activation uses a manual file picker to grant access, without copying
that input into the package or repository. Auto selection still uses original
identities and fails on ambiguity; no easier track/codec is substituted.

Three independently prepared owners request first, middle and last media fragments,
with one byte-identical warm reread each. They measure bounded windows, not a
whole-movie conversion. Preparation and request phases separately report actual
host callback count, requested capacities and returned bytes, alongside elapsed
times, cold/warm request times and sealed bytes/cleanup. Benchmark source reads
are capped at 32749 bytes, below the host's 64-KiB maximum. Codec/index/deadline/
quota limits are unchanged; a refusal is recorded rather than widening limits
or retrying another route. Preparation refusals also retain their read counters.

"Cold" means a fresh prepared owner/backend window and empty per-run cache, not
an OS page-cache flush. Warm means rereading the sealed fragment in that owner.
CPU/memory observations, where collected, belong to that process/run and include
its hosting/runtime overhead. Tiny owned-fixture timing is not real-file calibration.
These checks do not establish audible correctness, A/V synchronization, receiver
playback/EOF or authoring-estimate accuracy; D73's limitations remain separate.

## Reproduction and evidence

Use [D75's build/notice/import procedure](stereo-aac-uwp-runtime.md#reproduction)
with this branch, then build/publish the private 0.1.0.4 package. Extract into a
new workspace layout and update its registration in place. Do not uninstall
Screenbox/normal UWP hosts or remove profiles. Run `sap2-stereo-runtime.exe` for
bundled checks, `sap2-stereo-runtime.exe pick-expanded` for the owned folder grant,
and `sap2-stereo-runtime.exe benchmark` for the authorized input grant.

`LocalState/stereo-runtime.txt` records fixed labels/counters and progress after
each automatic case. Verify the fresh run ID/time and actual registered executable,
AppContainer token and six loaded DLL hashes against the layout while the process
remains alive for 30 seconds after results. A benchmark refusal is labeled
`MEASURED_REFUSAL`, distinct from PASS/FAIL. Only sanitized evidence is committed.

Implementation/native controls are in progress. Packaged results, loaded-module
hash evidence and real-file measurements have not yet been claimed for D84.
Final static/shared/enabled CMake/CTest, formatting, imports and actual-head CI
remain merge gates. D75's historical PASS remains limited to its recorded fixtures.
