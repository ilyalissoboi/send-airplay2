# macOS receiver consent investigation (D65)

Date: 2026-10-12 (Asia/Tokyo). User scope: investigate consent recurrence after
the documented and receiver-validated D62/D63/D64 Mac work. This is a research
slice, not a change to receiver access settings or sender privacy defaults.

## Public documentation

Sources were researched on 2026-10-11 before the user authorized experiments.

| Primary source | Relevant facts and limits |
| --- | --- |
| [Apple macOS 26 receiver settings](https://support.apple.com/guide/mac-help/change-airdrop-continuity-settings-mchl6a407f99/26/mac/26) | Documents receiver enablement, account/network/everyone access policies and optional password protection. Does not specify a consent-cache lifetime. |
| [Apple's original AirPlay-to-Mac guide](https://support.apple.com/guide/iphone/wirelessly-stream-video-photos-audio-mac-iphd668e80e6/15.0/ios/15.0) | For a different Apple Account, initial access requires Mac acceptance and sender verification; a code may appear. This older guide establishes that consent is expected behavior, not a contract for current cache duration. |
| [Apple WWDC23: AirPlay video/interstitials](https://developer.apple.com/videos/play/wwdc2023/10275/) | Includes Mac video receivers, HLS playback, controls and receiver capability differences. It does not document Mac session authorization internals. |
| [Oligo's receiver implementation research](https://www.oligo.security/blog/airborne) | Describes HTTP/RTSP with plist payloads and distinguishes access policy from click-to-accept behavior. Its focus is security research, not a complete receiver protocol. No implementation is copied. |
| [pyatv protocol documentation](https://pyatv.dev/documentation/protocols/) | Reverse-engineered session/authentication, encrypted events, feedback and remote-control details. Much of the evidence concerns Apple TV; it is not a macOS 26 specification. |
| [Unofficial AirPlay specification](https://openairplay.github.io/airplay-spec/) | Discovery, video requests/events and pairing/transport information. Its stated baseline is older Apple software; its completeness and applicability to this Mac require independent validation. |
| [Apple AirPlay security payload](https://developer.apple.com/documentation/devicemanagement/airplaysecurity) | Apple TV/tvOS passcode policies do not establish Mac receiver consent lifetime. |

No public source found in that search specified Mac permission-grant duration,
the identity fields that key it, a third-party persistent-grant API, the Mac's
remote-control SETUP rejection, or its explicit EOF event. Pairing success
and consent to present media must remain separate concepts in the investigation.

## Controlled experiment

Receiver: the recorded MacBook Pro (`Mac14,10`), user-confirmed macOS 26.7.1
unchanged. Sender: Windows x64, desktop CLI built from main `671e714` using the
existing CMake configuration and dependencies. Existing profile `macbook-pro`
and authorized `gas.mp4` were reused; no credentials were enrolled, exported,
deleted or modified. Receiver settings, firewall rules and privacy policy were
not changed.

The private driver discovers exactly one matching receiver in memory, runs a
fresh CLI process and sends Stop eight seconds after ready. It saves only
fixed/numeric telemetry, enforces a 70-second process deadline, and restores
staged binaries with hash verification. The executable uses an existing
firewall-covered location. Raw child output, endpoints and media URLs are not
saved. This driver is local investigation tooling, not a new library API.

Idle values below are measured from the preceding child exit to the next child
launch, after discovery/staging. They are not exact times since user acceptance;
permission can be accepted during start, before ready. Planned timing windows
sometimes slipped during interactive setup; conclusions use measured times.

### Sender identity audit

- `SenderIdentity::name` defaults to `send-airplay2`; model and OS fields keep
  the established reference defaults.
- `UrlPlaybackSession::start` generates a random locally administered device
  identifier if none was supplied. The host's real MAC is never used (D30).
- Session/correlation/item/client UUIDs and DACP/Client-Instance headers are
  generated anew; saved pairing is loaded through the normal native store.
- A temporary comparison binary changed only the synthetic display-name
  default to `send-airplay2-consent-probe`. The receiver log confirms that name
  arrived. The source was restored byte-for-byte, format checked, and the normal
  CLI rebuilt. No C++ change is proposed for shipping.

Baseline executable SHA-256:
`2184ac873431d621258f9c6560fa4c5aba0ca3cca543f453f4f3907a523210ed`.
Name-comparison executable SHA-256:
`c5a173c95c287c552777d4adc9e5e3a365abe9138132f820e3cf99cfa7a299c4`.
The latter has main `671e714` as its base plus the explicit one-field experiment;
it is not an unmodified release binary or a new published package.

## Receiver evidence

The user ran a read-only ControlCenter log capture filtered to permission/grant
messages, then provided the log locally. Retained facts are timestamps,
allowlisted request/delegate/should-ask categories, and the two synthetic client
names. Unrelated power-log permissions and raw addresses/identifiers are omitted.
In the complete captured A-K excerpt, no grant-duration/reset/expiry value was emitted.

| Run | Name | Measured idle after previous exit | Ready | Prompt evidence |
| --- | --- | --- | --- | --- |
| A | Default | Initial run | 3.750 s | Appeared; accepted |
| B | Default | 21.719 s | 1.969 s | None |
| C | Default | 124.234 s | 13.390 s | Appeared; accepted |
| D | Default | 219.563 s | 4.891 s | Appeared; accepted |
| E | Comparison name | 20.922 s | 2.047 s | None |
| F | Default | 87.734 s | 8.390 s | Appeared; accepted |
| G | Default | 62.547 s | 2.344 s | None |
| H | Default | 162.343 s | 7.594 s | Delegate/should-ask log; chain user report |
| I | Default | 45.078 s | 2.266 s | Request-only log; no delegate/should-ask sequence |
| J | Default | 45.078 s | 4.125 s | Delegate/should-ask log; chain user report |
| K | Default | 45.078 s | 2.250 s | None; corrected user report and request-only log |

All eleven processes exited 0 with `sender_stop`, cleaned up, reported no failed
reads, and restored their staged binaries. These checks target consent/start/
Stop; no additional pause/seek/EOF interoperability claim is made.

Run B and E show the request processor asking for permission internally, without
the delegate/ControlCenter `shouldAskPermission` sequence observed in A/C/D;
the user also saw no prompt in B/E. An internal request-permission log alone
therefore does not prove that a notification was displayed.

The timed H/I/J/K sequence used equal 45.078-second idle gaps and unchanged
defaults. The user reported more than one prompt in the chain. The final K
report was corrected to **no prompt**; only that corrected report is used.
The complete log shows delegate/should-ask sequences at H and J, request-only
at I and K. H-to-I permission requests were 60.640 seconds apart; I-to-J
55.517 seconds; H-to-J 116.157 seconds; J-to-K 57.475 seconds. These are
receiver request timestamps, not user acceptance times.

The delegate/should-ask sequences make H/J log-derived prompt candidates;
neither was individually confirmed by the observer. I's lack of that sequence
does not independently prove that no UI notification appeared. The user
confirmed only multiple prompts in the chain and corrected K to no prompt.
These observations and the timing pattern motivate grant age/expiry as a
working hypothesis; they do not establish that J's UI re-prompt proves an
age-based rule instead of a last-idle rule. Per-run UI attribution, a precise
lifetime and renewal behavior remain unproven.

## Interpretation and remaining limits

Short-term reuse survives a new process and the audited per-session random
identifiers. It also survives the tested display-name change. These observations
do not identify the exact cache key: saved pairing, host/network context or a
broader receiver grant remain possible. The display name or random device ID
alone is not a demonstrated reason for every repeated prompt.

Longer waits and the timed chain are consistent with an expiring receiver
permission. Exact lifetime, expiry anchor, renewal behavior and cache identity
remain unresolved: the capture exposes no numeric grant value, and it does not
vary the pairing, sender host/address, network or account context. Do not turn
these intervals into a universal timeout constant or a claim that the grant is
keyed by sender name. No sender fix or permanent identity change is justified.

The initial investigation is complete. Further investigation could inspect an
explicit receiver grant value or compare host/pairing contexts with user
authorization. Those are optional follow-ups, not automatic re-pairing or
changes to receiver access policy. The existing consent timeout/cancellation
and random per-session identifier policy are retained.

Results are specific to this Mac/firmware/Windows/profile combination. No account,
network, alternate pairing, locked/sleeping Mac or additional-receiver comparison
is inferred. See [receiver-validation.md](receiver-validation.md) and the final
dated [D65 artifact](validation/native-macos-consent-windows-2026-10-12.json)
for completed observations.
