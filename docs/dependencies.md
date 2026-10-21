# Dependency licenses and source provenance

The core, bindings, native protocol adapters and fixture authorship use
Apache-2.0. Keep [LICENSE](../LICENSE) and the separate notices for dependencies
and adapted build tooling. Record revisions and licenses before adding external
implementations or changing the codec profile.

## Runtime and build dependencies

| Component | Use | License / source |
| --- | --- | --- |
| OpenSSL 3.5+ | EVP crypto, private randomness, HKDF, X25519, Ed25519, AEAD; `libcrypto`, not TLS `libssl` | [Apache-2.0](https://openssl-library.org/source/license/) |
| Botan 3.12+ | C FFI SRP-6a and Ed25519 key validation | [BSD-2-Clause](https://github.com/randombit/botan/blob/3.12.0/license.txt) |
| Boost 1.92+ | Beast HTTP and Asio I/O; implementation-private headers | [BSL-1.0](https://www.boost.org/LICENSE_1_0.txt) |
| libsecret 0.19+ | Optional Linux Secret Service adapter | [LGPL-2.1-or-later](https://gitlab.gnome.org/GNOME/libsecret); system shared library |
| Security/CoreFoundation | macOS login-keychain adapter | System frameworks |
| FFmpeg 9.0.2 | Optional dynamic audio decoding, resampling and AAC encoding | Restricted LGPL-2.1-or-later profile below |

The vcpkg manifest baseline is `434307da09bc05b2c86996dccc8b2351fc0d5d37`.
Adapted port files retain the vcpkg MIT notice in
[LICENSE-vcpkg.txt](../vcpkg-overlays/LICENSE-vcpkg.txt). The Botan overlay
changes UWP linkage/target/modules, not Botan implementation source. No Botan,
OpenSSL or Boost implementation is copied into project source files.

The C# binding has no PackageReference. Its .NET SDK reference packs are build
inputs; hosts provide the runtime. Test/generation tools such as Python,
cryptography, protobuf and an FFmpeg executable are separate from native runtime
dependencies and must not be bundled incidentally.

## Optional FFmpeg profile

The [overlay](../vcpkg-overlays/ports/ffmpeg) starts from the pinned MIT vcpkg
port and pins `ffmpeg/ffmpeg` tag `n9.0.2`, archive SHA-512:

```text
21bf3fbcdfd2f41ea6edeab40433fecfe362b09ecaa177a652471b54f9357011b4f74dab18245ab1c26f1a473b94205490baecb42289afd2effd603cd0b22b59
```

Only dynamic avformat, avcodec, avutil and swresample are used. The curated
configuration disables GPL, nonfree, version-3 additions, programs, network,
external codec libraries and accelerators. Native decoders cover the admitted
AAC/AC-3/E-AC-3/DTS-core/FLAC/Opus/Vorbis/TrueHD and PCM configurations; output
uses native AAC. Runtime component versions/configuration must match build headers.
The overlay's allowed feature set is narrower than the upstream port's catalog.

Project-specific patches include `0053-wav-guid-dependency.patch` for WAV's
shared GUID symbols without enabling Wave64 demuxing, and
`0054-matroska-track-number.patch` to expose original TrackNumber securely to the
adapter. The overlay's remaining pinned patches are retained as build inputs.
The native wrappers and adapters are original project code; FFmpeg modifications
retain their upstream license.

Redistributed codec packages include original source archive, exact overlay and
patches, manifest/triplet, installed configuration/SPDX records, license notices
and rebuild instructions. `AUDIO-PROFILE.json` records hashes/configuration; the
four DLLs remain separate. See the packaged
[source instructions](../bindings/csharp/SendAirPlay2/FFMPEG-SOURCE.md) and
[FFmpeg licensing guidance](https://ffmpeg.org/legal.html). A passing build or
binary check is not a complete distribution or Store certification audit.

## Protocol research provenance

Protocol facts were researched from MIT-licensed
[`postlund/pyatv`](https://github.com/postlund/pyatv/tree/b277a4c8222ecdcbaab8a24e3e713ca44765adb4)
at `b277a4c8222ecdcbaab8a24e3e713ca44765adb4`, and the URL-playback reference
fork `robkochman/pyatv@8144c77c6cecbed4f9ba2adb5a350ad86a8f6604`.
The verified MIT license blob is `c27c9705f92fb19e805a82225f6f9ab5c17966f3`.

- `auth/hap_session.py`, `support/chacha20.py`, `auth/hap_tlv8.py` and
  `auth/hap_srp.py`: record layouts, nonce directions, TLV fields, transcripts
  and HKDF labels.
- `protocols/airplay/auth/hap.py`, `server_auth.py`: pairing/verification shape.
  The native implementation additionally requires authenticated server proofs,
  strong accessory keys, signatures and final state/status validation.
- `protocols/airplay/ap2_session.py`, `channels.py`, `mrp_connection.py`,
  `auth/hap_channel.py`, `protocols/raop/packets.py` and timing/protocol modules:
  SETUP order, event/data channel labels, event replies and timing layouts.
- `protocols/mrp/protocol.py`, `messages.py`, `player_state.py` and protobuf
  schemas: field numbers/enums, message framing and control correlation.
  Project codecs are original; reference-generated messages are only fixture oracles.
- The reference fork's `protocols/raop/protocols/airplayv2.py`: URL queue/command
  envelopes, headers and correlation fields.

Only protocol facts/layouts/constants are used; no pyatv implementation or schema
source is linked or vendored. Its polling/closure behavior is not treated as
proof of receiver intent.

URL fallback wire names `seek`/`stop` and CMTime request fields were cross-checked
against GPL-3.0 [DiPlay](https://github.com/shihabal3amri/DiPlay), including
`VideoInCar.kt` revision `e59f6025b3183a5b26563937501bad3fd40b7f92` and its
documentation. No GPL code/text was copied. The item-UUID requirement was
confirmed from receiver behavior/message-name research; no Apple binaries or
private log captures are redistributed.

## Specifications and independent fixtures

Native readers/codecs use format facts from RFC 6762/6763 (mDNS/DNS-SD), RFC
9110/9112 (HTTP), RFC 8216 (HLS), RFC 9559 and Matroska element/codec mappings,
ISO BMFF, ETSI TS 102 366 and ITU-T H.265 SPS VUI syntax. No external container
parser implementation is copied. HEVC range tests combine original encoded
test-pattern metadata and hand-authored bounded syntax fixtures.

Crypto known answers come from RFC 8439 section 2.8.2, RFC 7748 section 6.1 and
RFC 8032 section 7.1. Independent Python integer/hash/AEAD/plist/protobuf oracles
generate public synthetic transcripts. See [fixture provenance](../tests/fixtures/README.md).

Encoded audio/video fixtures are repository-authored signals with generator,
tool configuration and hashes in adjacent provenance manifests. An external
encoder used to generate a fixture is not a redistributed runtime component.
Keep those manifests and literal vectors when refactoring tests.
