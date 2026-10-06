# Dependencies and reference provenance

The project remains Apache-2.0. Record revisions and licensing here before
incorporating external code or adding dependencies.

## OpenSSL

The pairing/transport foundation links `OpenSSL::Crypto` (OpenSSL 3.5 or newer)
through CMake's `FindOpenSSL`; TLS/`libssl` is not used. ChaCha20-Poly1305 and
HKDF-SHA512 use OpenSSL EVP APIs; the project does not implement these primitives.
The peer-verification slice also uses EVP X25519 key agreement and pure Ed25519
signatures, with fresh ephemeral seeds from `RAND_priv_bytes`. No new dependency
or copied cryptographic implementation is introduced.
OpenSSL 3 is Apache-2.0: [upstream license](https://openssl-library.org/source/license/).
Redistributors must retain its license/notices alongside the native library.
No OpenSSL source is copied into the tracked repository.

The private codecs are not yet called by public discovery/range APIs. A local
Windows Release `dumpbin /dependents` check showed that unused crypto sections
were removed from `send_airplay2.dll`, while `control_tests.exe` imports
`libcrypto-3-x64.dll`. The peer-verification tests also exercise libcrypto.
Thus these slices require OpenSSL at build time and exercise
it in the codec test executable; runtime packaging for a public authenticated
session still needs proof once that path calls these codecs.

The Windows reproducible package manifest pins vcpkg at
`434307da09bc05b2c86996dccc8b2351fc0d5d37`, whose OpenSSL port resolves 3.6.5.
vcpkg is an MIT-licensed build tool, not a runtime component. Its port records
the upstream release archive checksum and packaging patches. The ignored local
checkout is `build-tools/vcpkg`. Native Unix builds can use the host's maintained
OpenSSL package. Keep the dependency on a supported release; 3.5 is the upstream
LTS series, while the pinned 3.6 series requires an update before its November
2026 end of support. [Upstream release lifecycle](https://openssl-library.org/source/)

## Botan SRP backend

The PIN-pairing slice selects Botan 3.12.0 through its C FFI (`ffi`, `srp6`,
`sha2_64`, `system_rng` modules), pinned by the existing vcpkg baseline. The core
remains C++17 and includes only Botan's C-compatible `ffi.h`; Botan's own build
requires C++20. SRP-6a arithmetic uses the maintained backend, not in-tree modular
arithmetic or OpenSSL's deprecated SRP API. OpenSSL continues to provide HAP
SHA512 transcript hashing, HKDF, AEAD and Ed25519.

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
gates; vcpkg's Botan port excludes the UWP triplet, so a packaged-host proof must
resolve that integration constraint before Screenbox work.

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
