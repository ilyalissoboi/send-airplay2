# Dependencies and reference provenance

The project remains Apache-2.0. Record revisions and licensing here before
incorporating external code or adding dependencies.

## OpenSSL

The pairing/transport foundation links `OpenSSL::Crypto` (OpenSSL 3.5 or newer)
through CMake's `FindOpenSSL`; TLS/`libssl` is not used. ChaCha20-Poly1305 and
HKDF-SHA512 use OpenSSL EVP APIs; the project does not implement these primitives.
The peer-verification slice also uses EVP X25519 key agreement and pure Ed25519
signatures, with fresh ephemeral seeds from `RAND_priv_bytes`. No new dependency
was introduced by that slice. PIN development now adds Botan key validation to
the shared signature-verification adapter; see below.
OpenSSL 3 is Apache-2.0: [upstream license](https://openssl-library.org/source/license/).
Redistributors must retain its license/notices alongside the native library.
No OpenSSL source is copied into the tracked repository.

The private codecs are not called by public discovery/range APIs. A historical local
Windows Release `dumpbin /dependents` check showed that unused crypto sections
were removed from `send_airplay2.dll`, while `control_tests.exe` imports
`libcrypto-3-x64.dll`. The peer-verification tests also exercise libcrypto and
Botan key validation.
Thus these slices require OpenSSL at build time and exercise
it in the codec test executable; runtime packaging for a public authenticated
session still needs proof once that path calls these codecs. The public media
server now calls OpenSSL's RNG for bearer URLs, so a shared build using that API
does require the libcrypto runtime; the media tests exercise this DLL loading on
Windows. This does not establish packaged/UWP runtime loading.

The Windows reproducible package manifest pins vcpkg at
`434307da09bc05b2c86996dccc8b2351fc0d5d37`, whose OpenSSL port resolves 3.6.5.
vcpkg is an MIT-licensed build tool, not a runtime component. Its port records
the upstream release archive checksum and packaging patches. The ignored local
checkout is `build-tools/vcpkg`. Native Unix builds can use the host's maintained
OpenSSL package. Keep the dependency on a supported release; 3.5 is the upstream
LTS series, while the pinned 3.6 series requires an update before its November
2026 end of support. [Upstream release lifecycle](https://openssl-library.org/source/)

## UWP builds: Botan overlay port (D55)

`vcpkg-overlays/ports/botan` copies vcpkg's MIT-licensed `botan` port files
from the pinned vcpkg commit `434307da09bc05b2c86996dccc8b2351fc0d5d37` and
changes them only for UWP targets: static linkage, Botan's `--os=uwp`, a
minimized module set (`ffi`, `srp6`, `sha2_64`, `system_rng`, `ed25519`) and no
`botan-cli`. vcpkg's copyright and MIT permission notice is reproduced in
[`vcpkg-overlays/LICENSE-vcpkg.txt`](../vcpkg-overlays/LICENSE-vcpkg.txt)
(vcpkg's `LICENSE.txt` at that commit, unchanged); the repository's Apache-2.0
license does not cover those files. Botan's source and version are unchanged
(3.12.0, BSD-2-Clause); only UWP builds pass the overlay. See [uwp-native-build.md](uwp-native-build.md).
Those builds also use vcpkg's community `x64-uwp` triplet for OpenSSL and Boost.

## Botan SRP and key validation

The PIN-pairing slice selects Botan 3.12.0 through its C FFI (`ffi`, `srp6`,
`sha2_64`, `system_rng`, `ed25519` modules), pinned by the existing vcpkg baseline. The core
remains C++17 and includes only Botan's C-compatible `ffi.h`; Botan's own build
requires C++20. SRP-6a arithmetic uses the maintained backend, not in-tree modular
arithmetic or OpenSSL's deprecated SRP API. OpenSSL continues to provide HAP
SHA512 transcript hashing, HKDF, AEAD and Ed25519.
Before verifying Ed25519 signatures, the adapter enforces canonical public-key
encoding and excludes the identity, then uses Botan's public-key check for
curve/prime-order subgroup membership. OpenSSL signature verification alone
can accept trivial forgeries with weak public keys. Inspected Botan's
`src/lib/pubkey/ed25519/ed25519_key.cpp` and `src/lib/ffi/ffi_pkey.cpp` for
validation/FFI error semantics; no curve arithmetic was copied or implemented.

Botan is [BSD-2-Clause licensed](https://github.com/randombit/botan/blob/3.12.0/license.txt).
Redistributions must include its license/notices and the applicable native runtime.
No Botan implementation source is copied into the repository. Inspected release
3.12.0: `src/lib/ffi/ffi_srp6.cpp`, `src/lib/misc/srp6/srp6.cpp`, `src/lib/ffi/ffi.h`
and its generated CMake target template. The C agreement API returns a padded raw
shared integer, not HAP's SHA512 session key; the adapter must normalize/hash it
and verify proofs. [SRP API](https://botan.randombit.net/handbook/api_ref/srp.html).

Alternatives investigated but not adopted: OpenSSL SRP (deprecated since 3.0,
no replacement); libimobiledevice's Stanford-derived SHA512 client (permissive
license, but inspected code has unchecked allocation/arithmetic returns and
non-constant-time proof comparison requiring broader hardening). That candidate
was inspected at `fa0f79190142bc309307967c058f89c1b36eb6b8`, not copied or linked.
Windows UWP/Android packaging and hardware interoperability remain validation
gates (UWP: built through the overlay port above and receiver-tested in D55);
vcpkg's curated Botan port excludes the UWP triplet, so a packaged-host proof must
resolve that integration constraint before Screenbox work.

## Boost HTTP media serving

The media-server slice uses Boost.Beast for HTTP/1 parsing/serialization and
Boost.Asio for socket operations, timers and bounded worker execution. The user
explicitly selected Boost instead of an in-tree HTTP implementation. Boost 1.92.0
is selected by the existing vcpkg baseline; the `boost-beast` and `boost-asio`
ports record upstream `boost-1.92.0` archive hashes. Header-only `Boost::beast` and
`Boost::asio` targets are private: Boost types do not appear in the public API.
The core remains C++17.

Boost uses the [Boost Software License 1.0](https://www.boost.org/LICENSE_1_0.txt).
Keep applicable license/copyright notices when redistributing dependency sources.
The project retains Apache-2.0. No upstream example or implementation source is
copied into tracked project code. Consulted upstream parser/header-limit,
HTTP serialization and Asio asynchronous I/O interfaces; media callbacks, range
policy and lifecycle logic are original project code. The vcpkg ports may install
additional Boost modules; no stackful coroutine, TLS or Boost filesystem API is
used. Android/UWP builds and hardware media serving remain separate gates.

## Protocol research

Inspected pyatv revision `b277a4c8222ecdcbaab8a24e3e713ca44765adb4`:

- `pyatv/auth/hap_session.py`: control record shape and incremental framing.
- `pyatv/support/chacha20.py`: independent directional counters and nonce layout.
- `pyatv/auth/hap_tlv8.py`: pairing tags and fragment encoding.
- `pyatv/auth/hap_srp.py`: pair-verification transcript order, HKDF labels and
  named nonces, reinspected for the peer-verification and PIN/SRP slices.
- `pyatv/protocols/airplay/auth/hap.py`: AirPlay pair-verify message sequence.
  Its unchecked final response is not adopted: this project requires HTTP 200,
  state M4 and no error before releasing connection keys.
  Pair-setup also requires HTTP 200/state M2/M4/M6, a verified server SRP proof
  and a verified accessory signature before accepting credentials.
- `pyatv/protocols/airplay/server_auth.py`: candidate M4 state acknowledgement
  and sender/receiver control-key direction. Its omitted controller verification
  is not used as a cryptographic test oracle.

Session channel keys (event channel and data streams) follow the same pyatv
revision: `pyatv/protocols/airplay/ap2_session.py` (blob
`aa5f1408f11d91a8be7545e9f706f28d2cde1424`) and
`pyatv/protocols/raop/protocols/airplayv2.py` (blob
`3d61a34e00559554477cd2ff97d9c337a9e7ccff`) give the salts and infos.
`pyatv/auth/hap_channel.py` (blob `1cf14fe1ef4964b3582fc46fc7574b333a2823a7`)
gives the `(salt, output_info, input_info)` order, from which the reversed
event-channel direction follows. Only these label strings are used, as
protocol constants.

The session's event-channel reply and NTP timing responder follow the same
revision:

- `pyatv/protocols/airplay/channels.py` (blob
  `a5730293f53e62df54df5c5a4c7f713d3a49d917`): the event reply fields
  (`200 OK`, `Content-Length: 0`, `Audio-Latency: 0`, with `Server` and `CSeq`
  echoed).
- `pyatv/protocols/raop/packets.py` (blob
  `2ff08f27917ee326a767aa0dae65d3cd5d02d88b`): the 32-byte big-endian timing
  packet layout.
- `pyatv/protocols/raop/protocols/__init__.py` (blob
  `fbaefd44609807200fd2ef367bfd0df01b443b7e`): the reply fields (type 0xd3,
  sequence 7, the reference time echoing the request's send time).
- `pyatv/protocols/raop/timing.py` (blob
  `91516688de86d263ef873743a91f692f2edd830c`): the NTP epoch offset and
  microsecond fraction.

Only these layouts and constants are used.

The minimal native remote-control session reuses the existing original
authentication, event and SETUP code. The pinned MIT `ap2_session.py` reference
above was reinspected on 2026-10-07 before implementation. It orders remote
SETUP/event, RECORD and data-stream SETUP; the first native H5 experiment omits
the latter two to test the SETUP/event contribution. No source was copied or
dependency added. That minimum is an engineering experiment, not a validated
replacement for the reference's complete remote-control/MRP sequence.

Session message shapes (`session_messages.*`) follow these sources; only body
keys, fixed identifiers, header names and version strings are used:

- `pyatv/protocols/raop/protocols/airplayv2.py` at the same pyatv revision: the
  base SETUP body.
- `pyatv/protocols/airplay/ap2_session.py` (blob
  `aa5f1408f11d91a8be7545e9f706f28d2cde1424`): the remote-control-only SETUP
  and the data-stream SETUP.
- `pyatv/support/rtsp.py` (blob `56d7c6ed4a50ea38294a6180ecf4ce119a3a4cd6`):
  the DACP-ID, Active-Remote and Client-Instance headers, `AirPlay/550.10`,
  and plist bodies serialized with sorted keys.
- The unmerged fix `robkochman/pyatv@8144c77c6cecbed4f9ba2adb5a350ad86a8f6604`
  (MIT), `pyatv/protocols/raop/protocols/airplayv2.py` (blob
  `c4358ede5a0685fffe6bd7ec0817a190af4190f6`): `sessionCorrelationUUID`, the
  URL control stream, the `/command` envelope, commands and headers, and the
  `AirPlay/870.14.1` agent.

pyatv is MIT licensed. It is a reference only; no implementation source was
copied or linked. Tests use independently generated, synthetic inputs; no receiver
credentials or captured private transcripts are committed. These references
describe a candidate protocol path, not interoperability on the user's receiver.

ChaCha20-Poly1305 test data comes from
[RFC 8439 section 2.8.2](https://www.rfc-editor.org/rfc/rfc8439.html#section-2.8.2).
The test records vector provenance at the point of use. Additional control-record
and HKDF fixtures are generated by a separate Python `cryptography`/HMAC oracle
using public synthetic keys, with generation instructions recorded
in `tests/fixtures/README.md`.
The Python AEAD API can share an OpenSSL backend; independence here refers to the
framing/fixture generator. The RFC known answer independently checks AEAD bytes.

Peer-verification primitive vectors use [RFC 7748 section 6.1](https://www.rfc-editor.org/rfc/rfc7748.html#section-6.1)
and [RFC 8032 section 7.1](https://www.rfc-editor.org/rfc/rfc8032.html#section-7.1).
OpenSSL API contracts were checked against its [X25519](https://docs.openssl.org/3.5/man7/EVP_KEYEXCH-X25519/)
and [Ed25519](https://docs.openssl.org/3.5/man7/EVP_SIGNATURE-ED25519/) documentation.

The receiver-transport slice adds no dependency or copied implementation source.
Its native sockets, framing and lifecycle are in-tree Apache-2.0 code. Updated
pyatv reference blob/license provenance and RFC framing inputs are recorded in
[receiver-transport.md](receiver-transport.md).

The credential-storage/CLI slice adds original Apache-2.0 host code and Windows
OS `Advapi32` credential/token APIs, without a new third-party implementation or
runtime dependency. The existing RFC 8032 public-key vector is reused in synthetic
storage tests. Primary Windows API contracts and the trusted-store boundary are
recorded in [credential-storage.md](credential-storage.md).

The M6 compatibility fix adds no dependency or copied implementation. It accepts
one bounded opaque type-17 field based on live sanitized TLV headers. References
were re-inspected at pyatv revision `b277a4c8222ecdcbaab8a24e3e713ca44765adb4`:

- `pyatv/auth/hap_tlv8.py`, blob `c34e222f1879061c5dcb81a472b5548177dd15be`:
  names type 17 `Name`.
- `pyatv/auth/hap_srp.py`, blob `3453bfd1096c483c267a607d2ec29cbd4af4a1dc`:
  distinguishes metadata from the required ID/key/signature transcript.
- `pyatv/protocols/airplay/server_auth.py`, blob
  `9351273fc3707edaa06592a03b86f82340d4c85b`: commented M6 example includes
  optional type-17 receiver information. This is reference evidence, not proof
  of its contents on the user's receiver.
- MIT `LICENSE.md`, blob `c27c9705f92fb19e805a82225f6f9ab5c17966f3`.

The project does not adopt the reference's skipped authentication checks. Server
SRP proof, AEAD, strong accessory key and Ed25519 signature remain mandatory.
Metadata is ignored and erased; no OPACK decoder or general extension tolerance
is introduced. See [synthetic fixture provenance](../tests/fixtures/README.md).

The noninteractive E2E runner uses Python 3.10+ standard-library subprocess,
threading, sockets, JSON and hashing APIs as a developer tool. It adds no native
runtime/build dependency or copied implementation. Offline tests use original
public synthetic fixtures; live reports contain constructed sanitized facts only.
See [runner contracts](e2e-runner.md) and [hardware evidence](receiver-validation.md).

## Native MRP controls (D29/D31)

Before implementation, the MIT protocol references were reinspected at
`postlund/pyatv@b277a4c8222ecdcbaab8a24e3e713ca44765adb4` on 2026-10-07:
`airplay/channels.py`, `airplay/ap2_session.py`, `mrp/protocol.py`,
`mrp/messages.py`, `mrp/player_state.py` and `mrp/protobuf/*.proto` under
`pyatv/protocols`. The repository's MIT `LICENSE.md` was also inspected.
These supply wire field numbers, enum values, data-frame layout, channel labels
and the handshake sequence. They are reverse-engineered protocol evidence.
The native bounded wire/message codec and session are original Apache-2.0 code;
no third-party implementation or schema source is vendored or linked.
Synthetic fixtures use the pinned pyatv generated messages with Python protobuf
as an independent developer-only oracle; no new native dependency is added.
The pinned `airplay/__init__.py`, `airplay/mrp_connection.py`,
`auth/hap_channel.py` and `auth/hap_session.py` were subsequently inspected for
the tunnel, remote feedback and record bounds. The reference receives records
using their two-byte lengths without enforcing the outgoing 1024-byte chunk
size. Native MRP adds an explicit 16-KiB receive budget after observing a
2363-byte advertised receiver record; control/event defaults remain 1024.

URL event duration conversion follows Apple's [CMTime rational time contract](https://developer.apple.com/documentation/coremedia/cmtime-api)
and [dictionary keys](https://developer.apple.com/documentation/coremedia/cmtime-dictionary-keys),
inspected 2026-10-07. Only field/flag facts are used; no Apple implementation
is copied and no Core Media runtime dependency is introduced.

## pyatv remote-Stop source audit (D41)

On 2026-10-07, we inspected pinned upstream `b277a4c8222ecdcbaab8a24e3e713ca44765adb4`
and tvOS reference fork `8144c77c6cecbed4f9ba2adb5a350ad86a8f6604` URL players,
event channels, inherited HAP disconnect handler, HTTP connection behavior,
MRP transport/feedback and AirPlay player tests. Both MIT license files were
verified against blob `c27c9705f92fb19e805a82225f6f9ab5c17966f3`.
Reference files remain ignored; no source was vendored or new dependency added.
Twenty exact source/license blobs and six isolated offline observations are
recorded in the [artifact](validation/pyatv-stop-source-audit-2026-10-07.json).
The [analysis](pyatv-stop-reference.md) distinguishes the upstream polling
heuristic, fork event-waiter gap and MRP transport callbacks from receiver intent.

## C# binding build (D52)

The C# binding and its test runner ([csharp-binding.md](csharp-binding.md)) are
original Apache-2.0 code with no `PackageReference`. Building them uses the .NET
SDK's implicit reference packages, restored from nuget.org: `NETStandard.Library`
(MIT) for the `netstandard2.0` library and the `Microsoft.NETCore.App.Ref`
targeting pack (MIT) for the `net8.0` runner. These are compile-time reference
assemblies only; nothing from them is copied into the repository or shipped with
the native library. Hosts bring their own .NET runtime.
