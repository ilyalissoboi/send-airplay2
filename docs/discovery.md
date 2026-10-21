# Receiver discovery

The C++ scanner performs a bounded synchronous mDNS/DNS-SD scan. C and C# hosts
use the snapshot API in [receivers.h](../include/send_airplay2/receivers.h).
`sap2_discover` returns an owned list with copied metadata; free it using the
matching list function. Discovery touches no credentials and proves no playback
capability. Continuous events/cancellation and IPv6-only discovery are separate
work. See [public API](public-api.md).

## Architecture and provenance

- `discovery_transport.h` isolates native networking from parsing and tracking.
  The adapter uses Winsock/GetAdaptersAddresses on Windows and BSD sockets/
  getifaddrs on Linux, macOS and Android. Windows links the OS libraries
  `ws2_32` and `iphlpapi`. No new third-party dependency was selected.
- `discovery_core.cpp` parses records and assembles device snapshots without OS
  calls. Tests use synthetic, in-tree fixtures rather than receiver captures.
- `discovery.cpp` owns the bounded scan, query backoff and interface refresh.
- `cli.cpp` formats a final snapshot as readable text or schema-versioned JSON.

All implementation and fixtures were authored for this repository under its
existing Apache-2.0 license. No implementation source was copied from pyatv,
UxPlay, an RFC or another project. Protocol references consulted:
[RFC 6762](https://www.rfc-editor.org/rfc/rfc6762) (mDNS queries, known answers,
cache-flush/goodbye grace and coexistence) and
[RFC 6763](https://www.rfc-editor.org/rfc/rfc6763) (DNS-SD PTR/SRV/TXT and TXT key
semantics). These specifications are references, not imported dependencies or
claims of complete mDNS conformance.

## Parsing and lifecycle contract

The parser accepts complete, successful standard DNS responses, including
answer, authority and additional records. It supports backward name compression
with bounds on expanded names and pointer traversal. Malformed packets, truncated
responses, invalid address lengths and truncated TXT entries have no cache
effects. Unknown RR types and non-IN classes are skipped. Limits: 9000 bytes per
datagram, 128 questions, 512 resource records per packet, 2048 cached records,
128 generated questions and 64 active interfaces. Cache overflow produces a
warning; these limits can make large-network results incomplete.

Record keys include the receiving interface. TTL expiry and goodbye records
remove stale services; TTL-zero goodbyes use a one-second grace period and can be
rescued by a refresh. Cache-flush records age out old RRSet members after one
second while preserving records received within the preceding second. Newest
SRV/TXT records supply the current endpoint and metadata. Address RRSet members
are retained independently. Interface removal or address/subnet change clears
that interface's records. The CLI shows the final snapshot, not lifecycle events.

PTR triggers SRV/TXT queries; SRV triggers A/AAAA queries. Only services with
PTR, nonzero SRV port/target and at least one address are exposed. TXT is optional;
unknown metadata remains empty/null. Unresolved services are retried during the
scan and are omitted from its final resolved-device snapshot.

TXT keys are ASCII case-insensitive; invalid keys are ignored and the first
duplicate wins. A flag (`key`) is distinct from an empty value (`key=`).
Binary values are retained. DNS labels containing literal dots/backslashes or
controls are escaped in fully qualified presentation names, preserving label
boundaries. Display names retain their original bytes.

Only matching, syntactically valid advertised MAC device identities justify
merging AirPlay and RAOP services. AirPlay `deviceid` and a RAOP instance's
12-hex-digit prefix before `@` are normalized. Without identity, the instance is
scoped to its network interface and kept separate. Hostname or friendly-name
equality does not justify a merge. Merged devices retain individual service
ports, addresses, interfaces and TXT. Advertised identity is unauthenticated.

`features` (or RAOP `ft`) accepts a hexadecimal 64-bit word or low/high 32-bit
words separated by a comma. Malformed/overflow masks stay unknown. `pw=true/1`
and `pw=false/0` report advertised password requirements; no pairing protocol is
inferred from this flag or feature bits. Pairing requirements remain unknown.

## Native adapter boundaries

The adapter binds UDP 5353 with address/port reuse, joins 224.0.0.251 on each
active IPv4 multicast interface, sends multicast (QM) questions, and obtains the
receiving interface from ancillary packet information. It does not request QU
responses that could be delivered exclusively to an existing system responder.
Queries use IP multicast TTL 255. Responses must come from UDP 5353 and the
receiving interface's IPv4 subnet. Records remain untrusted LAN input.

Questions retry with exponential backoff (1, 2, 4... seconds, capped at 16),
while newly learned instances/hosts get immediate resolution questions. Known
answers are included when at least half their original TTL remains. Queries are
split into at most four questions per bounded datagram; known answers that do
not fit its 1400-byte budget are omitted. Multipacket known-answer suppression,
negative/NSEC caching and responder behavior are not implemented. The scheduler
caps remembered interface/question combinations at 8192.

Interfaces refresh once per second. Only one IPv4 address per interface is used.
The adapter does not send IPv6 multicast queries and will not discover
IPv6-only LANs. AAAA records in IPv4 responses are still reported; link-local
addresses include an interface scope. VPNs, multicast filtering, firewall policy,
an exclusive UDP 5353 listener and network isolation can prevent discovery.
There is no automatic firewall modification or fallback OS discovery backend.
Android NDK/build, multicast-lock/permission behavior and device testing remain
unverified; native source portability alone is insufficient Android validation.

## CLI output

`airplay2-cli discover [--json] [--timeout-ms 1..60000]` defaults to 5000 ms.
Exit codes: 0 for a completed scan (including an empty result), 1 for transport
failure, 2 for invalid arguments. Operational errors go to stderr; successful
JSON stdout contains one object, no progress chatter.

JSON schema version 1 contains duration, backend, response/rejected-packet
counts, warnings and devices. `compatibility_validated` is always false.
Each service includes its interface, endpoint, addresses, features as a hex
string (to avoid JSON number precision loss), nullable password requirement,
`pairing_requirement: "unknown"`, TXT values and `txt_value_hex`. TXT flags are
null and empty values are empty strings. Valid UTF-8 is preserved; controls and
invalid bytes are escaped. Hex values preserve exact binary TXT bytes regardless
of display encoding. Human-readable output also escapes untrusted strings.

Diagnostics include device identifiers, LAN addresses and public advertised TXT
data. Review and sanitize output before sharing it. No pairing secrets or
credentials are obtained by discovery.
