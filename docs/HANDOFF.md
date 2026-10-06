# Project handoff: send-airplay2

Snapshot: 2026-10-06 (Asia/Tokyo). Audience: the next human developer or model.
Read this first, then [design.md](design.md) and
[receiver-validation.md](receiver-validation.md). This is a continuation record,
not a claim that the sender has been completed.

## 1. Goal and user requirements

The originating discussion was titled **Assess AirPlay 2 Feasibility**. The user
asked whether a library could cast media from any device to an AirPlay 2 receiver,
such as Apple TV, and provided the UxPlay AirPlay2-protocol wiki as a starting point.
They clarified the requirements as follows:

- "Any device" means a native application on mainstream Windows, Linux, macOS
  and Android. It does not mean a browser-only or every possible device solution.
- Cast supported, unprotected media to explicitly tested AirPlay receivers.
- Milestone 1: a tested standalone library that supports that use case.
- Milestone 2: integrate the library into a fork of
  [huynhsontung/Screenbox](https://github.com/huynhsontung/Screenbox), similarly to
  its existing Chromecast casting support.
- GitHub access under `ilyalissoboi` was authorized. The user requested continued
  work, then requested this documentation in the currently open PR.

The implementation repository is
[ilyalissoboi/send-airplay2](https://github.com/ilyalissoboi/send-airplay2).
It was created with an Apache-2.0 LICENSE before implementation began.

## 2. Current repository and PR state

| Item | Snapshot |
|---|---|
| Foundation PR | [#1: feat: establish portable AirPlay sender foundation](https://github.com/ilyalissoboi/send-airplay2/pull/1) |
| Foundation PR state | Merged on 2026-10-06; verified through GitHub CLI |
| Discovery PR | [#2: feat: add receiver discovery and diagnostic CLI](https://github.com/ilyalissoboi/send-airplay2/pull/2), merged on 2026-10-06 |
| Pairing/transport PR | [#3: feat: add pairing TLV8 and authenticated control record codecs](https://github.com/ilyalissoboi/send-airplay2/pull/3), merged on 2026-10-06 |
| Peer-verification PR | [#4: feat: add authenticated peer verification for existing credentials](https://github.com/ilyalissoboi/send-airplay2/pull/4), merged on 2026-10-06 |
| PIN-pairing PR | [#5: feat: add authenticated PIN pairing message flow](https://github.com/ilyalissoboi/send-airplay2/pull/5), merged on 2026-10-06; verified merge `24bb2b86a1a725ea82a4a32d5a33fd2c22ef7e9d` |
| Receiver-transport PR | [#6: feat: add bounded authenticated receiver transport](https://github.com/ilyalissoboi/send-airplay2/pull/6), merged on 2026-10-06; verified merge `8c4ce3e47a3fd6d2cf73ab4197892a9004803d99` |
| Current development slice | Windows desktop credential storage and CLI pairing/reconnect; next PR publication/check evidence below |
| Receiver-transport implementation commit | `ffbe86f3e5d4aa6bc590d30c61ec70d42720615f`; subsequent IPv6 authority fix at `979ef0829248203684939274eb3864b8241845cc` |
| Target | `main` |
| Current local branch | `codex/credential-cli`, based on verified PR #6 merge at `8c4ce3e47a3fd6d2cf73ab4197892a9004803d99` |
| Pairing/control implementation commit | `ee4afa80172d38300078fad0b5a2332898e95cd5`; later documentation commits record checks |
| Peer-verification implementation commit | `7b572a7b24d7242200e0cb1321c366a83932b3da`; all six platform/static/shared jobs and Linux ASan/UBSan passed |
| PIN-pairing implementation commit | `8770909ce66239c03664c324d966420b3a18adc0`; local static/shared checks passed; CI evidence below |
| Hardened PIN-pairing source commit | `4c7dcc4f6af6f5ebc391097dcc0998262436f761`; all platform/static/shared and sanitizer checks passed; subsequent documentation commits record evidence |
| Discovery implementation commit | `48189897ba167b44c3da7c6e4a7857bf28120498`; later commits add documentation and a C++ readability/ownership pass |
| C++ readability commit | `23f8ff08a12431fc0aba95a21aaf881aa7458674`; all platform/sanitizer CI jobs passed, subsequent commits record evidence only |
| Final foundation PR head | `034983ca095fd0803d3de2307c6d27fdf18db488` on `feat/portable-foundation` |
| Original main commit | `8c77b15d391e14b53a3591eea7d0ac6e28376813` (LICENSE only) |
| Foundation commit | `dbd654b1d92057b3208953226186c5c2b206ccff` |
| Crypto dependencies | OpenSSL 3.5+ libcrypto and Botan 3.12+ C FFI for private SRP; pinned vcpkg supplies 3.6.5/3.12.0. Public authenticated-session/packaged runtime loading is pending |
| Actual casting support | None yet |
| Receiver validation | Windows LAN discovery observed for `AppleTV14,1` advertising OS 26.6 and `Mac14,2`; pairing/playback not run, no compatibility certification |
| Screenbox changes | None; source audit only, no integration fork created in this session |

PRs #1 through #6 are merged and `main` contains the foundation/discovery/control codecs, peer verification, PIN setup and bounded receiver transport. Verify current GitHub and
local branch state before further development; the original foundation SHA is
not the final PR head.

### Local testing environment

User-provided on 2026-10-06 (Asia/Tokyo):

- Receiver: **Apple TV 4K**; subsequent LAN discovery advertised `AppleTV14,1`
  and name "Living Room". Generation not independently established.
- Firmware: **tvOS 26.6** (user-reported, consistent with advertised `osvers`/`ov`);
  exact build not yet supplied. AirPlay `srcvers` is not a tvOS build identifier.
- Intended testing host: **Windows 11 x64**; exact OS build not yet supplied.

AirPlay access settings, detailed network configuration and reference playback
results remain pending. Discovery resolved the receiver; playback compatibility
is untested.
See [receiver-validation.md](receiver-validation.md) for the test record.

## 3. Feasibility findings and evidence levels

**Assessment:** the narrowed use case is technically plausible and manageable as
an incremental interoperability project. It is not established as working by
this repository. The evidence comes from existing sender implementations and
protocol research; only real receiver tests can establish this library's support.

Separate three things when reporting progress:

1. The receiver advertises AirPlay 2 capabilities.
2. A legacy-compatible media path works on that receiver.
3. A modern authenticated AirPlay 2 session path works.

Success at one does not prove the others. Record the actual path, receiver model
and firmware in every interoperability result. Avoid blanket "AirPlay 2 supported"
claims based on compilation, mock tests or an unauthenticated `/play` response.

Media playback by URL is distinct from screen mirroring, captured system audio,
multiroom synchronization and protected-media playback. The proposed first
approach lets the receiver fetch and decode an unprotected H.264/AAC MP4 from a
sender-hosted HTTP URL. That requires the receiver to reach the sender's media
server. Audio within the MP4 does not prove standalone speaker/audio transport.

### References already examined

| Reference | Finding and intended use | Evidence status |
|---|---|---|
| [UxPlay AirPlay2-protocol wiki](https://github.com/FDH2/UxPlay/wiki/AirPlay2-protocol) | Index of pairing, crypto, protocol and implementation references | Read; useful starting point, not a complete sender specification |
| [pyatv protocols](https://pyatv.dev/documentation/protocols/) | Discovery/authentication/protocol reference | Read; not a certification of this library |
| [pyatv AirPlayV2](https://github.com/postlund/pyatv/blob/master/pyatv/protocols/raop/protocols/airplayv2.py) | Executable reference for authenticated URL playback setup | Source inspected, blob `3d61a34e00559554477cd2ff97d9c337a9e7ccff` |
| [pyatv player](https://github.com/postlund/pyatv/blob/master/pyatv/protocols/airplay/player.py) | Playback polling, startup/error handling | Source inspected, blob `e2c073cb0ba3e2a1c29f3882db88ed257bd9e2ef` |
| [Screenbox](https://github.com/huynhsontung/Screenbox) | Host architecture and integration boundary | Selected files inspected at `46aadf6b20ef5d8348a6049c16c882c22ec0f84e` |

The inspected pyatv AirPlayV2 URL path verifies the connection, performs base RTSP
SETUP, establishes an event channel, starts periodic feedback, sends RECORD,
posts a binary-plist `/play` body, and sets the playback rate. Its player polls
`/playback-info` and interprets receiver errors. This sequence is a reference for
investigation; it must not be assumed to be a minimal or universally sufficient
sequence on every receiver. Reinspect source and obtain sanitized local traces
before implementing firmware-specific behavior.

The initial assessment also identified the following candidates. These are
research leads, not dependencies selected for the native core:

- **pyatv:** reference sender and proposed local baseline tool; a Python package
  is not the intended production native library. The earlier assessment treated
  its audio coverage as partial; do not infer universal audio support.
- **UxPlay:** a receiver/reference, not a drop-in sender library.
- **pair_ap:** an authentication building-block candidate linked from the wiki
  ([ejurgensen/pair_ap](https://github.com/ejurgensen/pair_ap)); not integrated.
- **libraop:** an audio/authentication research lead linked from the wiki
  ([philippe44/libraop](https://github.com/philippe44/libraop)); not integrated.
- **airplay2-sender-cpp:** an audio candidate mentioned in the earlier assessment;
  its exact repository/revision was not retained in the available continuation
  record. Resolve its provenance before relying on it. It is not hardware validated
  or adopted here.

No external implementation code has been copied into this project. Discovery
uses in-tree native sockets, documented in [discovery.md](discovery.md). OpenSSL is
now selected for private AEAD/HKDF adapters; see [dependencies.md](dependencies.md).
No plist or audio dependency has been selected. Check each candidate's
actual revision, license, maintenance and platform support before incorporating it;
do not assume all references share this project's Apache-2.0 license.

## 4. Decision record

User requirements are distinguished from engineering choices proposed in the
assessment and used to guide this first PR. Continued work authorized the first
foundation; it did not establish hardware compatibility or freeze the API.

| ID | Decision / working direction | Status and reason |
|---|---|---|
| D01 | Native Windows/Linux/macOS/Android, tested unprotected media | User requirement |
| D02 | Standalone library before Screenbox integration | User milestone order |
| D03 | Portable C++ core, eventual stable/versioned C ABI | Proposed architecture; C++17 foundation and experimental C API implemented |
| D04 | C# wrapper for Screenbox; JNI for Android | Planned; neither wrapper implemented |
| D05 | Begin with local H.264/AAC MP4 on one specified Apple TV model/firmware | Proposed first vertical slice; user supplied Apple TV 4K / tvOS 26.6 / Windows 11 x64; generation and exact builds pending |
| D06 | CLI plus minimal packaged Windows C# host before Screenbox changes | Planned validation gate; packaged networking/file/native-loading risks must be tested early |
| D07 | Receiver-side URL playback with local HTTP serving | Planned first media path; current code only resolves ranges |
| D08 | Media source size/read-at callbacks, not filesystem paths alone | Planned for Windows StorageFile and Android content URI access; callback ABI unfinished |
| D09 | Keep standalone audio transport separate from first MP4 proof | Planned; audio backend pending |
| D10 | AirPlay beside Chromecast through a provider-neutral boundary | Planned Screenbox integration; retain existing Chromecast behavior behind its adapter |
| D11 | No initial DRM, mirroring, system-audio capture, synchronized multiroom, auto-transcoding or universal codec/receiver support | Initial scope limits from assessment; unsupported media should fail explicitly |
| D12 | Preserve repository's Apache-2.0 license | Implemented; no relicensing decision made |
| D13 | Keep implementation reviewable on PR #1 | Foundation reviewed through PR #1; now merged |
| D14 | Ignore unsupported multipart/invalid Range fields and serve full content | Implemented resolver policy; documented in design.md |
| D15 | Bounded native IPv4 mDNS scan, portable DNS-SD cache/parser, no new third-party runtime dependency | Implemented; adapter isolated, IPv6-only discovery and Android device validation pending |
| D16 | Keep protocol-specific records and merge only by matching normalized advertised device identity | Implemented; hostname/friendly name alone is insufficient, advertisements remain unauthenticated |
| D17 | Experimental C++ discovery API plus CLI before versioned C discovery ABI/event API | Implemented; shared users require compatible C++ runtime, production bindings still pending |
| D18 | OpenSSL 3.5+ EVP primitives, private bounded pairing/control codecs before receiver handshake | Engineering choice, implemented; no homegrown cryptography, public pairing API deferred until peer authentication and ownership contracts are complete |
| D19 | Implement existing-credential HAP peer verification before first-time PIN provisioning | Engineering choice, implemented privately with EVP X25519/Ed25519; pinned ID/key, strict M2/M4 schema and one-time control-key release. Credential storage and network correlation/deadlines remain gates |
| D20 | Botan 3.12 C FFI for fixed-profile HAP PIN/SRP and Ed25519 key validation | Engineering choice, implemented privately; C++17 core, maintained SRP/subgroup checks, mandatory server proof and accessory signature. vcpkg excludes UWP; resolve packaged-host integration before Screenbox work |
| D21 | Private synchronous native TCP with one outstanding request and a strict bounded HTTP/RTSP profile | Engineering choice, merged in PR #6; absolute deadlines/cancellation, terminal cleanup, HTTP ordered correlation with optional validated CSeq, mandatory RTSP CSeq, verified record transition. Hardware remains a gate |
| D22 | Private versioned credential envelope, trusted host store and Windows desktop CLI before a public auth ABI | Engineering choice, implemented on credential-cli branch; current-user/same-computer Credential Manager, create-only profile semantics for cooperating writers, no plaintext fallback or automatic re-pair. Other OS stores and packaged hosts remain gates |

The C API is pre-1.0 and explicitly experimental. "Stable C ABI" is a target,
not a promise about the current header. Define versioning, ownership, threading,
cancellation and errors before publishing production bindings.

## 5. Implemented code and verification

| File | Purpose |
|---|---|
| `include/send_airplay2/http_range.h` | Experimental length-delimited C interface, fixed-width status values, offset/length result |
| `src/http_range.cpp` | Allocation-free single byte-range resolver, overflow checks and empty-file handling |
| `tests/range_tests.cpp` | Boundary/argument/buffer cases and generated interval tests |
| `tests/c_abi_smoke.c` | Compile/link/use from a C11 caller |
| `CMakeLists.txt` | C++17 static/shared library and CTest targets |
| `.github/workflows/build.yml` | Windows/Linux/macOS static/shared build-and-test matrix |
| `docs/design.md` | Architecture, range contract, integration audit and implementation gates |
| `docs/receiver-validation.md` | Per-platform/per-firmware hardware test record; discovery observed, pairing/playback not run |
| `include/send_airplay2/discovery.h` | Experimental synchronous C++ discovery records/options/result API |
| `src/discovery*.cpp` / internal headers | Bounded DNS-SD parser/cache, scan scheduler and native socket adapter |
| `src/cli.cpp` | `airplay2-cli discover`, readable and JSON diagnostics |
| `tests/discovery_tests.cpp` / CLI fixtures | Synthetic parser/lifecycle/query tests, mutation corpus and CLI output validation |
| `docs/discovery.md` | Discovery contract, adapter limits, source provenance and local LAN observations |
| `src/pairing_tlv.*` | Bounded ordered TLV8 codec with strict fragment/separator handling |
| `src/control_crypto.*` / `src/control_records.*` | OpenSSL AEAD/HKDF, incremental authenticated records, directional counters and terminal failure |
| `tests/control_tests.cpp` / `tests/fixtures` | Independent synthetic wire/key fixtures and malformed/replay/fragmentation tests |
| `docs/pairing-transport.md` / `docs/dependencies.md` / `vcpkg.json` | Transport contracts, gates, dependency/license provenance and pinned package manifest |
| `src/pair_setup_crypto.*` / `src/pair_setup.*` | Private Botan SRP/HAP proof adapter and authenticated persistent PIN setup state machine |
| `tests/pair_setup_tests.cpp` / `tests/fixtures/pair-setup` | Independent SRP/message fixtures, rejection/cleanup cases and enrollment-to-verification round trip |
| `docs/pin-pairing.md` | Provisioning trust, bounds, ownership, dependency and remaining I/O/storage gates |
| `src/identity_crypto.*` / `src/pair_verify.*` | OpenSSL X25519/Ed25519, secret ownership and bounded existing-credential verification state machine |
| `tests/pair_verify_tests.cpp` / `tests/fixtures/pair-verify` / `docs/peer-verification.md` | RFC vectors, independent synthetic transcripts, failure/cleanup/record-handoff tests and trust contracts |
| `src/receiver_http.*` / `src/receiver_stream.*` / `src/receiver_connection.*` | Private bounded framing, native TCP ownership/deadlines/cancellation and authentication-to-record integration |
| `tests/receiver_tests.cpp` / `docs/receiver-transport.md` | Fragmented fake receiver transcripts, dynamic authenticated peers, native loopback and lifecycle/framing contracts |
| `src/credential_*`, `src/auth_*` / `tests/credential_tests.cpp` / `docs/credential-storage.md` | Private bounded credential codec, native Windows store, hidden-PIN CLI, enrollment/save/reload/reconnect orchestration and synthetic/OS persistence tests |

The range resolver handles closed, open-ended and suffix ranges for a known
64-bit representation size. It consumes an HTTP field **value**, not a complete
request; it performs no I/O. It does not implement HTTP serving, GET/HEAD parsing,
If-Range, conditional requests, authentication, URLs or session control.
See the header and design.md for status-to-response mapping and lifetime rules.

Verified at foundation commit `dbd654b1d92057b3208953226186c5c2b206ccff`:

- Local Linux GCC 13.3 static and shared CMake builds: both CTest targets pass.
- 25 boundary cases, four argument/buffer checks and 26,240 generated range cases:
  pass. These verify interval logic, not a 4-GiB file transfer or receiver seeking.
- C11 compilation and linking against C++: pass.
- AddressSanitizer and UndefinedBehaviorSanitizer: pass with
  `ASAN_OPTIONS=detect_leaks=0`. Default LeakSanitizer failed to inspect processes
  in the execution environment; leak detection was not validated.
- GitHub CI: all six Windows/Linux/macOS × static/shared jobs pass, confirmed
  2026-10-06. [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37409502023)
  and [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37409476906).
- Android NDK build/device, packaged Windows host and receiver tests: not run.

Discovery slice validation on 2026-10-06:

- Windows 11 x64, Visual Studio 2026 / MSVC 19.51: static and shared Release
  CMake builds, all six CTest targets pass. Discovery tests cover malformed and
  compressed DNS, binary/duplicate TXT, feature masks, resolution, identity
  merging, interface isolation, updates, TTL, goodbye/flush grace and cache caps;
  10,000 deterministic parser mutations also run. CLI JSON tests verify binary
  preservation, 64-bit mask formatting and escaped terminal controls.
- A 10-second LAN scan resolved `Mac14,2` AirPlay/RAOP services; a fresh
  15-second scan resolved the user's "Living Room" `AppleTV14,1`, advertised
  OS 26.6, AirPlay/RAOP port 7000, IPv4 plus scoped IPv6. Four responses, none
  rejected, no warnings. Details are sanitized in receiver-validation.md.
- Real receiver departure/interface changes, exact tvOS build, pyatv reference
  playback, pairing, native playback and packaged/Android host validation remain
  pending. Unit success and discovery do not certify playback.
- Discovery CI at implementation commit `48189897ba167b44c3da7c6e4a7857bf28120498`:
  all six Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan passed,
  verified 2026-10-06. [Push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37419114646)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37419131062).
  Commits through `f9b9002` updated documentation only; later readability changes
  require their own checks. Inspect checks for the actual PR
  head before merging. Linux/macOS receiver interoperability remains untested.

Readability pass on PR #2, 2026-10-06:

- Added a repository `.clang-format` convention, expanded control flow and clear
  DNS constants/names, separated query scheduling, service resolution and JSON
  formatting, and organized cache tests by scenario.
- Public API and internal comments describe buffer ownership, byte order,
  interface scope, monotonic time, compression traversal, cache grace, metadata
  merging and binary/UTF-8 output. Socket and Winsock resource owners explicitly
  forbid copying; cleanup order and failure unwinding are documented.
- Windows static/shared Release builds passed all six CTest targets, including
  new valid/invalid UTF-8 cases. The pre-existing synthetic fixture JSON was
  byte-for-byte unchanged before adding the new test fields. The test harness
  now decodes CLI output explicitly as UTF-8 on Windows.
- Protocol behavior and JSON schema remain the same. Actual pairing/playback
  validation is still pending; verify CI for the readability commit separately.
- A post-refactor 10-second Windows LAN scan again resolved "Living Room"
  (`AppleTV14,1`) and `Mac14,2`, each with both services: four responses, zero
  rejected packets and no warnings. This remains discovery-only evidence.
- At readability commit `23f8ff08a12431fc0aba95a21aaf881aa7458674`, all six
  Windows/Linux/macOS static/shared CI jobs and Linux ASan/UBSan passed:
  [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37420813282)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37420816897).

Pairing/control foundation, 2026-10-06:

- Started from verified merge of PR #2 at `02c953a22c9503f1bf17bc14d6814e797cb04a1b`
  on `codex/pairing-transport`. The public API still cannot pair or cast.
- Implemented bounded private TLV8, OpenSSL ChaCha20-Poly1305/HKDF-SHA512 and
  directional record reader/writer. Authentication/length/backend errors close
  the direction; keys and transient failed plaintext are wiped. Counter wrap and
  resource copying are forbidden. Socket ownership/deadlines and peer verification
  are explicitly pending; see pairing-transport.md.
- Windows static/shared Release builds passed all seven CTest targets using OpenSSL
  3.6.5 from the pinned vcpkg manifest, MSVC 19.51 / Visual Studio 2026.
  At implementation commit `ee4afa80172d38300078fad0b5a2332898e95cd5`, all six
  Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan passed in the
  [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37422659514).
  Inspect checks for the final PR head before merging. Fixtures include RFC AEAD bytes,
  Python-generated control framing/HKDF, every two-part split, byte-at-a-time reads,
  corruption/replay/EOF/nonce limits and 3,000 TLV parser mutations.
- No receiver handshake, credential change, PIN or playback operation was attempted.
  Wrong PIN/revocation/signature tests remain gates of the next handshake slice.

Readability pass on PR #3, 2026-10-06:

- Named TLV fragment/header sizes and the HKDF info limit, clarified encoded-size
  calculations, renamed the pure nonce constructor to `nonce_for_counter`, and
  made the plaintext cleanup guard explicitly non-copyable/non-movable. Touched
  sources include the standard headers for the facilities they use directly.
- Split control tests into named AEAD/HKDF, framing/size, authentication/replay,
  EOF/counter and TLV scenarios. Failure diagnostics identify the group/case and
  relevant split, mutation or byte offset. Named final-fragment layout calculations
  replace unexplained tail offsets. Existing fixture files and boundary expectations
  are unchanged; all 3,000 TLV mutations and existing record split cases remain.
- Added project-wide C++ readability requirements to AGENTS.md so subsequent
  implementation/test changes apply the same standards before PR completion.
- Windows static/shared Release builds passed all seven CTest targets; clang-format
  dry-run and `git diff --check` passed. An intentional missing-fixture invocation
  confirmed the diagnostic includes the active scenario and fixture filename.
- At final readability source commit `85e2b8d48a4bf993ba4581f539f1103ed601ba4a`,
  all six Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan passed in
  the [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37425244505)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37425248739).
  Inspect checks for the final PR head, including documentation-only updates.
- Protocol behavior, public API and receiver validation status remain unchanged.
  No new hardware pairing/playback result is claimed.

Peer verification, 2026-10-06:

- Verified PR #3 merged at `360d74661e7cc703a82b7852f425215189217ad3`; started
  `codex/peer-verification` from that main commit. Existing-credential verification
  is the next bounded slice before first-time PIN enrollment, not a public pairing API.
- Added EVP X25519/Ed25519 and erasing secret/credential owners. Receiver identity
  is pinned to trusted credentials; no discovery-derived trust or TOFU. M2 must
  authenticate before the sender signs M3. HTTP 200/state M4/no error is required
  before deriving/releasing write/read keys once. Failures permanently close and
  clear owned handshake/control keys. See peer-verification.md for complete limits.
- Windows 11 x64 / MSVC 19.51 static/shared Release builds pass all eight CTest
  targets. Added RFC 7748/8032 vectors, independent Python wire/HMAC transcript
  fixtures, identity/signature/schema/replay/rejection cases, directional record
  handoff at every split, phase cancellation and 1,000 deterministic M2 mutations.
  Fixture regeneration reproduced committed bytes; clang-format dry-run and
  `git diff --check` passed. Existing control fixtures and Apache-2.0 LICENSE are unchanged.
- At implementation commit `7b572a7b24d7242200e0cb1321c366a83932b3da`, all six
  Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan passed in the
  [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37427502668)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37427549046).
  Inspect final PR #4 head checks, including documentation-only commits, before merging.
- Continued the AGENTS.md readability requirements: named scenario groups,
  diagnostic context, explicit non-copyable ownership and Doxygen contracts.
  OpenSSL remains the only crypto dependency; no third-party source was copied.
- No receiver I/O, PIN entry, credential change or playback operation was attempted.
  Wrong PIN, actual revocation/reconnect, firmware interoperability and packaged/
  Android loading remain untested. Next: vetted PIN/SRP provisioning with verified
  server proofs/signatures, trusted credential storage, bounded HTTP/socket I/O.

PIN pairing, 2026-10-06:

- Verified PR #4 merged at `54d63b81d5b9972c12418164aac2076fda1d1ff7`; started
  `codex/pin-pairing` from main. Added private persistent HAP setup using Botan
  SRP-6a (3072-bit/g=5/SHA512), with OpenSSL transcript/HKDF/AEAD/Ed25519.
  No third-party implementation source was copied; dependency provenance and
  BSD-2-Clause notices are recorded in dependencies.md. Botan builds as C++20;
  its C FFI preserves the core's C++17 requirement.
- M4 must authenticate before M5 is emitted. M6 AEAD and accessory signature
  must authenticate before credentials are returned once. Errors/cancellation
  close and erase owned secrets; no discovery trust or plaintext/transient fallback.
  See pin-pairing.md for the bounded profile and caller/transport/storage contracts.
- Native regression tests demonstrated that a validly encrypted M6 with an
  identity public key and a trivial forged signature could otherwise be accepted.
  The shared Ed25519 verifier now enforces canonical encoding, rejects identity
  aliases and uses Botan to check curve/prime-order subgroup membership before
  signature verification. This protects both enrollment and later peer verification.
  Regression fixtures and numeric weak-key cases fail before the fix and pass after;
  no curve arithmetic was copied or implemented.
- Independent Python integer/SHA512/HMAC fixtures cover a leading-zero shared
  integer, exact sender bytes, wrong PIN, every server-proof/ciphertext byte,
  validly encrypted signature/identity/schema failures, bounds, phase cancellation,
  single-use release and erasure. Enrollment credentials complete the existing
  peer-verification oracle and derive the expected control keys.
- Windows 11 x64 / MSVC 19.51 static/shared Release builds pass all nine CTest
  targets with OpenSSL 3.6.5 and Botan 3.12.0. clang-format dry-run and
  `git diff --check` pass; fixture regeneration is reproducible.
- At hardened source commit `4c7dcc4f6af6f5ebc391097dcc0998262436f761`, all six
  Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan passed in both the
  [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37436323914)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37436328684).
  Inspect checks for the actual final PR #5 head, including documentation commits.
- The full hardened Windows Debug pair-setup suite also passes (94.70 seconds).
  Its CTest limit is five minutes to accommodate repeated 3072-bit SRP with
  unoptimized backends; corruption coverage is retained. Release suite time
  is about three seconds. GCC/Clang warnings-as-errors exposed a test-loop string
  copy, fixed with a const reference; final-head CI must be rechecked.
- No receiver connection, PIN-display request, actual PIN entry, credential save
  or playback operation was attempted. Hardware authentication remains NOT RUN.
  Next: bounded HTTP/socket I/O and trusted host credential storage, followed by
  real PIN/reconnect/revocation validation. vcpkg Botan excludes UWP; packaged
  Windows/Screenbox and Android loading require separate proof.

Reproduction from a fresh checkout (requires OpenSSL 3.5+ and Botan 3.12+; see README for the
pinned vcpkg build when the host package is unavailable):

```sh
git clone https://github.com/ilyalissoboi/send-airplay2.git
cd send-airplay2
git switch codex/pin-pairing
cmake -S . -B build-static -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-static --config Release
ctest --test-dir build-static -C Release --output-on-failure
cmake -S . -B build-shared -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-shared --config Release
ctest --test-dir build-shared -C Release --output-on-failure
```

For Linux sanitizer reproduction:

```sh
mkdir -p build-sanitized
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -Iinclude src/http_range.cpp tests/range_tests.cpp -o build-sanitized/range_tests
ASAN_OPTIONS=detect_leaks=0 ./build-sanitized/range_tests
```

Disable leak detection only where that process-inspection limitation exists.

Receiver-transport slice on `codex/receiver-transport`, 2026-10-06:

- Verified PR #5 merged at `24bb2b86a1a725ea82a4a32d5a33fd2c22ef7e9d`
  and created this branch from that exact main head. No new dependency or copied
  implementation source; RFC/pyatv blob and license provenance is in
  [receiver-transport.md](receiver-transport.md).
- Private bounded HTTP/RTSP request encoding/response parsing and native numeric
  IPv4/IPv6 TCP now integrate PIN provisioning, pinned peer verification and the
  encrypted-record transition. One outstanding request, no retries/fallback,
  absolute deadlines, <=20 ms cancellation polling and terminal socket/key cleanup.
  HTTP correlation uses ordering and checks CSeq if present; RTSP requires CSeq.
  This is a narrow receiver profile, not a general HTTP implementation.
- Windows 11 x64 / MSVC 19.51 static/shared Release: all 10 CTests pass. The new
  receiver suite exercises independent PIN transcripts, fresh-randomness synthetic
  verification, encrypted RTSP, fragmented/coalesced input, limits/injection,
  bad proof/signature/tag, replay, partial trailing records, deadline/cancellation,
  sequence exhaustion and cleanup. Real IPv4/IPv6 loopback checks native I/O,
  timeout/cancellation, EOF, refused connect and queued unsolicited input.
- Readability/ownership pass applies the AGENTS.md rules, including Doxygen
  contracts, named bounds, focused scenarios and RAII sockets/secrets/threads.
  clang-format and diff checks pass. The Windows Debug receiver suite also passes.
  IPv6 scope IDs stay in the socket address rather than HTTP Host. Final source
  is `979ef0829248203684939274eb3864b8241845cc`. At subsequent documentation head
  `3360550b1ab7307a6b8e2014438d6e26f221255b`, all 14 checks passed: six native
  Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan in each of the
  [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37441690978)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37441695377).
  Earlier Windows checks were superseded during dependency configuration; green
  merged-main setup took about 35 minutes, and its saved cache let the final checks
  finish normally. Subsequent evidence updates are documentation only; still verify
  checks at the actual PR head before merging.
- No connection to the Apple TV, PIN display/entry, credential change/save or
  playback attempted. Receiver authentication/playback and Android/UWP packaged
  loading remain untested. Next: trusted credential serialization/host storage
  and CLI pairing/reconnect, followed by explicit hardware validation.

## 6. Screenbox integration findings

At the inspected Screenbox commit:

- `CastService.CreateRendererWatcher` accepts only `VlcMediaPlayer` and returns
  a concrete `RendererWatcher`.
- `CastService.SetActiveRenderer` calls `VlcPlayer.SetRenderer`.
- `ICastService` exposes that watcher and the current concrete renderer model.
- `Renderer` owns a LibVLC `RendererItem`; it is not a general AirPlay device model.
- `Package.appxmanifest` already includes `privateNetworkClientServer`, but that
  does not prove inbound serving, discovery or native loading in a packaged app.

The working design is to wrap the existing LibVLC behavior with a Chromecast
adapter and add an AirPlay provider/session behind a common boundary. Do not try
to pass an AirPlay device to LibVLC's existing renderer setter. AirPlay should own
its remote session and media server. Controls and displayed position must follow
the active remote session, with local playback handoff/recovery defined.

Before touching Screenbox, read its current `AGENTS.md`, `.github/copilot-instructions.md`
and applicable C#/XAML instructions. The inspected instructions require Visual
Studio 2026 MSBuild for UWP builds, not `dotnet build`. They also distinguish
stateless services from stateful resource managers. Inspect RendererWatcher,
CastContext, CastControlViewModel and playback coordination before choosing where
to own a session. Follow current repository instructions rather than assuming
the recorded source snapshot is still current.

Credential-storage/CLI slice on `codex/credential-cli`, 2026-10-06:

- Verified PR #6 is merged at `8c4ce3e47a3fd6d2cf73ab4197892a9004803d99`
  and based the new branch on that commit. GitHub CLI remains available and
  authenticated; there were no open repository PRs before this slice.
- Implemented a private exact-length/version credential envelope (maximum 203
  bytes), strict Ed25519 pin validation, erasing blob owners, host store interface
  and current-user/same-computer Windows Credential Manager adapter. Profile
  names are bounded/lowercase; named user/profile mutexes serialize cooperating
  create/delete operations and refuse existing/malformed slots. No plaintext
  fallback or other-platform adapter is introduced; see decision D22 and
  [credential-storage.md](credential-storage.md).
- Added CLI `pair`, `verify` and `forget`. Hidden interactive PIN input is required
  before receiver enrollment; no PIN argument/stdin/environment route. Pair
  saves only authenticated M6 credentials, closes provisioning, releases/reloads
  the credential owner, and verifies on a fresh connection. A saved checkpoint
  distinguishes reconnect failure; saved credentials are retained for explicit
  `verify`, never automatically replaced. Forget is local-only and idempotent.
- Windows 11 x64 / MSVC 19.51 static/shared Release passes all 12 CTests (6.18 s /
  6.07 s). New codec/workflow/native-store scenarios test exact schema, truncation,
  weak keys, leading-zero PIN, cancellation, no premature/duplicate saves,
  fresh connections and retained storage after reconnect failures. Real isolated
  synthetic Windows store tests pass create/load/delete, cross-process reload,
  concurrent create refusal and malformed-slot protection. Real CLI tests reject
  PIN arguments without echo and redirected input before receiver contact.
  The independent PIN-to-peer-verification oracle now passes through the codec
  and checks exact transcript/control keys after reloading.
- No new third-party source/runtime dependency: original Apache-2.0 code uses
  Windows OS APIs (`Advapi32`); OpenSSL/Botan remain the crypto dependencies.
  Readability/RAII contracts, formatting and applicable checks are part of this PR.
- Actual console PIN entry/echo restoration, Apple TV PIN display/enrollment,
  hardware restart reconnect, wrong PIN/revocation and playback remain NOT RUN.
  Next assess this authentication gate on Living Room / Apple TV 4K / tvOS 26.6
  with the exact firmware build/access settings, then add bounded GET/HEAD media
  serving with size/read-at callbacks, >4-GiB ranges and lifecycle/cancellation.
  OS storage adapters, packaged Windows/UWP and Android loading remain pending.

## 7. What is missing and what to do next

The milestone is unfinished. **Do not interpret missing hardware access as a
reason to stop all implementation.** Build and mock/unit-test independent pieces
while arranging the hardware baseline in parallel with that work.

1. **Hardware baseline:** use the user-provided Apple TV 4K / tvOS 26.6 /
   Windows 11 x64 setup (discovery model `AppleTV14,1`). Obtain the exact
   tvOS build and AirPlay access settings without collecting secrets. Establish
   reference playback with pyatv on that LAN and record the real session path.
   Local discovery succeeded; reference and native playback have not been checked.
2. **Discovery + diagnostics:** implemented on this branch. Complete real receiver
   departure/interface-change checks and other platform/Android host coverage.
   The Windows Apple TV discovery gate has passed; see discovery.md for limits.
3. **Pairing + secure transport:** private TLV8, AEAD/HKDF and record framing are
   implemented, along with existing-credential peer verification and PIN/SRP message
   processing. Private bounded socket/deadline and framing integration is now
   implemented. Private persistent credential format, Windows desktop trusted
   storage and CLI pairing/reconnect are now implemented on the current branch.
   Next validate actual enrollment/restart reconnect, wrong PIN, authentication failure,
   counters/replay, timeouts and revocation before claiming interoperability.
4. **HTTP media server:** define size/read-at callbacks and cancellation/lifetime;
   integrate the existing resolver into bounded GET/HEAD serving. Select the LAN
   address reachable by the receiver, use per-session URLs, and test concurrent
   reads, >4-GiB positions, exact lengths, shutdown and unreachable callbacks.
5. **Session/playback:** implement authenticated setup, event/timing/feedback
   lifecycle, URL start, status, pause/resume, seek and stop. Treat receiver status
   and disconnects explicitly. Use the same MP4 as the reference baseline.
6. **Host proofs:** implement the CLI and packaged Windows C# sample; test native
   loading, brokered media access and inbound networking. Add macOS/Linux/Android
   device coverage and bindings. Record failures separately from build success.
7. **Screenbox:** after the standalone gate, create/use a dedicated fork, introduce
   the casting adapter boundary, route active-session controls, and test Chromecast
   regression and local/remote handoff.

Remaining design choices: production OS discovery fallback/IPv6-only backend,
plist libraries, session socket/event
model, additional OS credential-storage adapters, asynchronous C API and bindings,
timeouts/cancellation, capability policy, media-server access policy, unsupported
codec handling, Android packaging and audio transport. These choices remain open;
OpenSSL covers AEAD/HKDF/identity primitives and Botan covers SRP arithmetic. Keep decisions explicit
in future updates to this record.

## 8. Continuation mechanics and known obstacles

Current local environment, verified on 2026-10-06: GitHub CLI 2.102.0 is available
at `C:\Program Files\GitHub CLI\gh.exe`, authenticated as `ilyalissoboi` through
the keyring. A GitHub API read confirmed push and admin permissions for this
repository. Network verification required execution outside the sandbox; the
initial sandbox authentication error was not evidence of invalid credentials.
Authenticated command-line Git push succeeded for PR #2 in this local chat.

Historical cloud environment: public clone/read worked. Command-line `git push` failed with
`could not read Username for 'https://github.com'`; GitHub connector authorization
does not automatically configure CLI credentials. Publishing resumed successfully
through the authorized connector's Git tree/commit/branch/PR operations. Do not
ask for the same authorization again merely because a CLI lacks credentials.
Use the available supported connector, or normally configured credentials in a
human developer's environment; never place credentials in the repository.

The original local foundation commit was
`f9bec098b83de5d38caab3696f935aa87ded6be4`; the equivalent published foundation
commit is `dbd654b1d92057b3208953226186c5c2b206ccff` because it was created through
the connector. In a carried-over workspace, inspect local versus remote history
before attempting a push. A clean checkout of the PR branch avoids that divergence.
Do not force-push over unfamiliar remote changes.

No background implementation task was scheduled. CI is asynchronous; further
coding resumes when a developer/model actively continues the project.

## 9. Completion criteria and reporting rules

Milestone 1 requires the tested standalone native library across the requested
host platforms and an explicit receiver compatibility matrix. The first Apple TV
MP4 vertical slice is an intermediate gate, not the whole milestone. Milestone 2
requires the Screenbox fork integration and regression-tested Chromecast behavior.

When handing off again, update: branch/PR and current commit, actual implementation
coverage, exact tests and hardware results, new decisions and rationale, open
choices, and the next actionable change. Keep "planned", "implemented",
"build-tested" and "receiver-tested" distinct. Store sanitized fixtures/results
with provenance; do not commit pairing secrets or private media.
