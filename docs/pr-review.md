# PR #12 review (D43, 2026-10-07)

Reviewed the implementation diff against `origin/main`
(`2b0e57c9d3ee44c5afc66418c23084ffada01d59`), starting at draft PR head
`439133239d7fe73a6d4fa3acbcbc930597e4fe04`. All ten checks passed at that starting
head. This review covers the private native playback slice, rather than the
future public playback ABI or Screenbox integration.

## Scope and findings

Inspected channel key/record ownership, framing and parser bounds, URL and MRP
startup/correlation, ownership and received EOF rules, cancellation and lock
ordering, supervisor teardown, timing sockets, media serving and CLI diagnostics.
Checked the fixtures, test contracts, documentation and provenance against the
implementation. Two runtime issues were reproduced and fixed:

| Finding | Trigger and effect | Fix and regression evidence |
|---|---|---|
| URL diagnostic redaction | Receiver-provided type/state strings, dictionary keys and HTTP/RTSP request targets could enter output; a state containing a newline could inject output lines. | Fixed type/state allowlists, unknown strings mapped to `other`, allowlisted key paths, fixed `URL` prefix and `URL unreadable=yes` fallback. Codec and scripted-session tests use synthetic private values/targets and require exact safe output. |
| Decoded plaintext cleanup | A malformed later MRP batch message, allocation failure or rejected correlated reply could release decoded extension plaintext before an erasure guard was reached. An event acknowledgment failure likewise released its body before caller ownership. | Move-only erasing `MrpMessage` ownership, pending-response reset at Stop and event-body RAII until acknowledgment/ownership transfer. A standalone single-threaded test observes still-live bytes before deallocation and checks move/replacement, malformed later input, injected allocation failure and event reply failure; compile-time checks prohibit MRP copying. |

The cleanup and redaction regressions were first run against the old behavior;
both test targets failed with the expected scenarios. They pass after the fixes.
The event acknowledgment regression also failed with the old receive path and
passed with the ownership guard; `plaintext_cleanup_tests` covers both protocols.
The session tests also verify that malformed event bodies remain acknowledged
without terminating playback. URL event-body RAII now covers exceptions while
constructing diagnostics. The payload-erasure contract covers decoded extension
buffers, not all metadata copies in generic protobuf/plist objects.

Final CI at `880feb3a652f655ed8586e7ba53c8ba47e355667` passed nine checks,
including sanitizers, but macOS shared failed in the existing `silent feedback
deadline` fixture. Its silent peer was armed before startup: feedback could time
out while startup still held/waited for the control mutex or confirmed playing.
The fixture now answers feedback through startup, then arms the fault under its
mutex and checks a new post-arm request. Startup confirmation deliberately lasts
120 ms, beyond the 30 ms interval plus 80 ms timeout, so the previous setup fails
this phase separation. The production deadline and 500 ms cleanup assertion are
unchanged. The pending-feedback cancellation fixture uses the same phase boundary.
This is a test scheduling fix, not a playback policy change; see the
[failed CI record](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37583326138).

No additional blocker was identified in this review of the private experimental
slice. Known protocol states, message bytes, controls, startup policy and lifecycle
end reasons are preserved. Diagnostic text for unknown states/keys/targets changes
intentionally. No third-party implementation or dependency was introduced;
Apache-2.0 and existing provenance remain intact.

## Validation and remaining gates

- Windows 11 x64 / MSVC Release: full static and shared builds succeeded;
  CTest passed **24/24 each** on the final acknowledgment-guard source
  and feedback-fixture source (15.40/15.43 seconds).
- The session test passed five consecutive repetitions under concurrent local
  test load (21.18 seconds), including deadline and pending-feedback cancellation.
- Offline runner contracts passed **10/10** (13.045 seconds).
- All **52 PR-changed C++ files** passed `clang-format --dry-run --Werror`;
  `git diff --check` passed.
- Loopback UDP tests failed in the restricted network sandbox and passed outside
  it; runner temporary-file tests likewise needed unrestricted local execution.
  No firewall/network settings or credential data were changed.
- Inspect CI for the actual published head, including sanitizer and all six
  platform/static/shared builds. An earlier head's success is not this gate.
  Initial D43 commit `d1011ab5b6a413f2e58a5e8587060d5866af6894` passed all ten
  [checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37582693970);
  the final D43 implementation/test head
  `ab7ec0170dbc364bfbec0e52a1b71ae92a0974a5` passed all ten
  [checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37584220175),
  including the previously failing macOS shared fixture. Inspect subsequent PR heads separately.

This review performed **no new receiver test**. D40's runtime and the D42 Ethernet
artifact retain their exact source/binary fingerprints and selected hardware
observations. Unit/CI success is separate from receiver interoperability.

Remote Stop/Home can still report `connection_lost/exit 1`; the reference audit
and one network sample do not establish a reliable cause classifier. Intermittent
startup pause remains unresolved. The original frozen-video failure remains low
priority per D37 unless it recurs. Cooperative duration-based ownership does not
guarantee protection against concurrent same-duration takeover. Broader lifecycle,
other receiver/host, packaged application and public ABI gates remain pending.

The user approved marking PR #12 ready, and that status change was verified on
2026-10-07 at `ab7ec01`. It is open and unmerged. Merge still requires user approval.

## Review feedback follow-up (D44)

The automated review of `ab7ec01` raised one
[P2 documentation comment](https://github.com/ilyalissoboi/send-airplay2/pull/12#discussion_r4204085497):
the README and current required-observations table contradicted D42 by leaving
network recovery open/NOT RUN. Both now report the selected Ethernet interruption
cleanup and fresh explicit same-credential recovery as PASS. Broader network and
longer reliability remain pending, and automatic in-session reconnect/resume
remains unimplemented. Historical dated records retain their original NOT RUN
status and are superseded by D42 for this selected check.

This follow-up changes documentation only, adds the detailed
[separate-session note](CONTINUATION.md), and updates the live ready-for-review
status. It adds no runtime, test, dependency or receiver evidence. Inspect CI and
the review thread at the actual published head before merging.
