# pyatv remote-Stop source audit (D41)

Inspected on 2026-10-07 before further lifecycle work. This is source analysis
and six offline isolated-method probes, with no new receiver test or native
behavior change. Native comparison uses D40 code head
`4a18b2662150768a00154ca17e222f2e701fd192`; PR #12's actual head at audit start
was `204ac4d027719e57c8f7b5dccd443ca47e988319`.

## Finding

The inspected pyatv paths do not supply a dedicated decoder that distinguishes
receiver-remote Stop from sleep or network interruption. Upstream's URL polling
path assumes a lost connection means playback stopped. The fork used for the
tvOS 26 reference playback instead waits for terminal playback events; closing
its URL event connection alone does not finish that waiter.

| Path | Completion/disconnect behavior | Limit |
| --- | --- | --- |
| Upstream URL player | Polls `/playback-info`; catches `RuntimeError` or `ConnectionLostError` and returns normally. Missing duration ends playback after it has started, or after the initial retry budget. | The error catch also applies before playback starts. It does not identify remote Stop intent. |
| tvOS fork URL player | After observing `playing`, finishes on `idle` or `stopped`. Before playing, a 30-attempt budget raises `PlaybackError` if startup never happens. | `paused` is not terminal. After starting there is no overall end deadline in this waiter. |
| Fork URL event channel | Parses playback-state events, lowercases their state and wakes the state waiter on changes. Inherited `connection_lost` only logs the disconnect. | Event-channel EOF leaves the last playback state intact and does not wake it with a terminal error/state. |
| Separate MRP data stream | Disconnect callback reports `connection_closed()` when the transport supplies no exception, otherwise `connection_lost(exc)`. | Transport closure/error distinction is not proof of a remote Stop action. |

The upstream player is pinned at
[`postlund/pyatv@b277a4c`](https://github.com/postlund/pyatv/blob/b277a4c8222ecdcbaab8a24e3e713ca44765adb4/pyatv/protocols/airplay/player.py#L75).
GitHub's upstream head query returned that same revision on the audit date.
The tested tvOS reference fork is
[`robkochman/pyatv@8144c77`](https://github.com/robkochman/pyatv/blob/8144c77c6cecbed4f9ba2adb5a350ad86a8f6604/pyatv/protocols/airplay/player.py#L69).
Its event-state implementation is in
[`AirPlayV2`](https://github.com/robkochman/pyatv/blob/8144c77c6cecbed4f9ba2adb5a350ad86a8f6604/pyatv/protocols/raop/protocols/airplayv2.py#L111),
with the disconnect behavior inherited from
[`AbstractHAPChannel`](https://github.com/robkochman/pyatv/blob/8144c77c6cecbed4f9ba2adb5a350ad86a8f6604/pyatv/auth/hap_channel.py#L78).
The separate transport callback is in
[`AirPlayMrpConnection`](https://github.com/robkochman/pyatv/blob/8144c77c6cecbed4f9ba2adb5a350ad86a8f6604/pyatv/protocols/airplay/mrp_connection.py#L68).

This explains a source-level gap for D40's observed URL state `playing` followed
by event socket EOF. The fork's isolated URL waiter would remain pending with
that sequence until cancellation or another terminal state. This is not a
claim that a full pyatv client was tested hanging: outer-client callbacks and
shutdown were not exercised by the offline probe. The upstream polling path
also was not the successful tvOS 26 reference path; its historical HTTP 500
failure remains in [receiver-validation.md](receiver-validation.md).

## Offline evidence and reference coverage

Pinned methods were extracted with Python AST and exercised with synthetic
objects, injected polling exceptions and playback-state events. The state-wait
slice was shortened to 5 ms, with a 50-ms observation budget; pending tasks were
explicitly cancelled by the probe. No receiver, credentials, media or protocol
connection was used. These are isolated source-method observations, not full
reference-suite, native CTest or hardware results.

| Synthetic scenario | Observed result |
| --- | --- |
| Upstream polling raises `RuntimeError` | Returns without error |
| Upstream polling raises `ConnectionLostError` | Returns without error |
| Fork `playing` then `stopped` | Waiter completes |
| Fork `playing` then `idle` | Waiter completes |
| Fork `playing` then `paused` | Remains pending until probe cancellation |
| Fork `playing` then inherited event-channel `connection_lost(None)` | Remains pending with state `playing` until probe cancellation |

All six observations matched the inspected methods. The fork's
[`test_airplay_v2_player.py`](https://github.com/robkochman/pyatv/blob/8144c77c6cecbed4f9ba2adb5a350ad86a8f6604/tests/protocols/airplay/test_airplay_v2_player.py)
covers URL commands/headers, failures, event decoding, state changes/duplicates,
`playing` then `stopped`, and startup timeout. That inspected module has no case
for event-channel EOF after playing, or Stop versus sleep/network loss.

## Native consequence and next step

Keep D40's native classification unchanged. Its supervisor prioritizes channel
failures, so the observed remote Stop/Home still cleans automatically with
`connection_lost`/exit 1. The reference audit supplies no missing validated
Stop message or code that would justify relabeling that failure as normal Stop.

Further work must separate a termination-policy choice from receiver evidence.
Adopting upstream's permissive polling rule would deliberately treat some
connection failures as completion; it would not discover Stop intent. Complete
the established-session network interruption/recovery comparison when the user
can perform it, then choose and test the intended API behavior for ambiguous
peer closure. Until then, retain the conservative classification and the
separate user-observed Home result. Other PR gates are unchanged.

## Provenance

Both repositories use MIT, with the inspected license blob
`c27c9705f92fb19e805a82225f6f9ab5c17966f3` (copyright 2020 Pierre Ståhl).
Downloaded reference files stayed in ignored `build-review/`; no third-party
implementation source, schema, new dependency or native patch was added.
Apache-2.0 remains unchanged. Exact revisions, twenty verified source/license
blob records, harness hash, probe scope and observations are in the
[sanitized source-audit artifact](validation/pyatv-stop-source-audit-2026-10-07.json).
