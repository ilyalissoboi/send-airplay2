# macOS receiver consent

Saved pairing and permission to present media are separate. A paired sender can
still trigger a Mac acceptance prompt. Start/cancellation must handle that prompt
without treating pairing success as approval to play.

Recorded macOS 26.7.1 checks showed short-term consent reuse across fresh sender
processes, session identifiers and a display-name change. Longer intervals and
an equal-idle repeat chain prompted again. These observations are consistent
with an expiring receiver permission but do not establish its exact lifetime,
cache key, expiry anchor or renewal rules. Do not turn an observed interval into
a universal timeout or change sender identity to try to bypass consent.

The sender retains bounded start/cancel behavior and its per-session identifiers.
No persistent-consent API or permanent sender-identity workaround is implemented.
If a prompt is reused, a cancellation test cannot claim pending-prompt coverage.
See [manual validation](manual-validation.md) and
[receiver scope](receiver-validation.md).

## Reference boundaries

[Apple's receiver settings](https://support.apple.com/guide/mac-help/change-airdrop-continuity-settings-mchl6a407f99/26/mac/26)
describe enablement/access policy, but are not a protocol contract for consent
grant timers. [Apple's AirPlay-to-Mac guide](https://support.apple.com/guide/iphone/wirelessly-stream-video-photos-audio-mac-iphd668e80e6/15.0/ios/15.0)
describes acceptance for another account; it does not establish current cache
duration. [WWDC AirPlay material](https://developer.apple.com/videos/play/wwdc2023/10275/)
describes playback capabilities, not a third-party persistent grant mechanism.

Further investigation needs receiver evidence or explicit context comparisons.
Do not infer grant identity from a name change alone, apply Apple TV access-policy
documentation as a Mac cache specification, or change receiver settings silently.
