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
implementation. Two concrete issues were reproduced and fixed:

| Finding | Trigger and effect | Fix and regression evidence |
|---|---|---|
| URL diagnostic redaction | Receiver-provided type/state strings, dictionary keys and HTTP/RTSP request targets could enter output; a state containing a newline could inject output lines. | Fixed type/state allowlists, unknown strings mapped to `other`, allowlisted key paths, fixed `URL` prefix and `URL unreadable=yes` fallback. Codec and scripted-session tests use synthetic private values/targets and require exact safe output. |
| Decoded MRP extension-payload cleanup | A malformed later batch message, allocation failure or rejected correlated reply could release decoded plaintext before a per-message/caller erasure guard was reached. | Move-only erasing `MrpMessage` ownership, plus pending-response reset at Stop. A standalone single-threaded test observes still-live payload bytes immediately before deallocation, tests move/replacement, malformed later input and injected allocation failure; compile-time checks prohibit copying. |

The cleanup and redaction regressions were first run against the old behavior;
both test targets failed with the expected scenarios. They pass after the fixes.
The session tests also verify that malformed event bodies remain acknowledged
without terminating playback. URL event-body RAII now covers exceptions while
constructing diagnostics. The payload-erasure contract covers decoded extension
buffers, not all metadata copies in generic protobuf/plist objects.

No additional blocker was identified in this review of the private experimental
slice. Known protocol states, message bytes, controls, startup policy and lifecycle
end reasons are preserved. Diagnostic text for unknown states/keys/targets changes
intentionally. No third-party implementation or dependency was introduced;
Apache-2.0 and existing provenance remain intact.

## Validation and remaining gates

- Windows 11 x64 / MSVC Release: full static and shared builds succeeded;
  CTest passed **24/24 each** (15.76/15.34 seconds).
- Offline runner contracts passed **10/10** (13.045 seconds).
- All **52 PR-changed C++ files** passed `clang-format --dry-run --Werror`;
  `git diff --check` passed.
- Loopback UDP tests failed in the restricted network sandbox and passed outside
  it; runner temporary-file tests likewise needed unrestricted local execution.
  No firewall/network settings or credential data were changed.
- Inspect CI for the actual published head, including sanitizer and all six
  platform/static/shared builds. An earlier head's success is not this gate.

This review performed **no new receiver test**. D40's runtime and the D42 Ethernet
artifact retain their exact source/binary fingerprints and selected hardware
observations. Unit/CI success is separate from receiver interoperability.

Remote Stop/Home can still report `connection_lost/exit 1`; the reference audit
and one network sample do not establish a reliable cause classifier. Intermittent
startup pause remains unresolved. The original frozen-video failure remains low
priority per D37 unless it recurs. Cooperative duration-based ownership does not
guarantee protection against concurrent same-duration takeover. Broader lifecycle,
other receiver/host, packaged application and public ABI gates remain pending.

Keep PR #12 draft until the user approves marking it ready or merging, as required
by [HANDOFF.md](HANDOFF.md#8-continuation-mechanics-and-known-obstacles).
