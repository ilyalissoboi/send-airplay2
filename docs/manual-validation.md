# Receiver checks

Queue created 2026-10-07 (Asia/Tokyo) under D38 while the user was unavailable.
The user returned and completed this selected batch under D39 on the same date.
The [sanitized artifact](validation/native-manual-batch-windows-static-2026-10-07.json)
records six native casts with exact runtime/source fingerprints and separate
observer responses. No runtime change or unattended visual pass is implied.

The batch used the existing Living Room receiver and local MP4 with stored native credentials.
Codex captured sanitized startup/media/status diagnostics for each checkpoint;
the observer used the TV remote at the announced Stop/sleep/wake checkpoints.
Exact CLI/source fingerprints are retained in the artifact. These checks target the rebuilt
D38 normal configuration: MRP enabled, 16 media slots, a one-second startup
confirmation interval and no automatic Play/seek retry.

| ID | Checkpoint | Observation | Status |
|---|---|---|---|
| B1 | Fresh cast, then fresh cast after sender Stop/Home | Normal startup video/audio and Home after Stop were user-confirmed; the controls cast also started normally after confirmed Home. First run initial Home/untouched remote were not explicitly answered. | PASS for selected startup/presentation; condition limit retained |
| B2 | Native pause/resume, forward/backward seek and sender Stop, followed by a separate full natural-EOF run | User confirmed all requested controls/startup/Home results. Separate full clip had normal video/audio and automatic Home at EOF. Native controls and EOF cleanups exited 0. | PASS |
| B3 | Receiver remote Stop during established playback | User confirmed Home. Sender automatically cleaned up, but URL socket closed while still playing without a terminal event; connection_lost/exit 1 remains. | Home/cleanup PASS; protocol intent OPEN |
| B4 | Sleep, wake to Home, then fresh native cast with the same credentials | User confirmed screen off, wake/Home, fresh normal video/audio and Home at near-end EOF. Sleep cleaned on URL disconnect; wake cast reused credentials and exited 0 at media_end. | PASS for selected sleep/wake recovery |

B1/B4 help distinguish startup transitions from sustained playback failures.
The one-second interval filters short contradictory telemetry; it does not make
the receiver resume and does not prove decoder progress. Earlier G1/G2/D36 visual
results remain evidence for their own configurations. D39 adds results for the
submitted normal-default D38 runtime. Selected passes do not establish longer
reliability or fix the historical spontaneous startup pause. No new buffering or
freeze was reported; normal full playback was user-confirmed.

If a later startup check reproduces pausing, capture the failed-start trace and cleanup
first. Then compare explicit `--minimal-remote` and normal MRP casts from the
same observed Home/idle condition, recording order and time since the previous
session. Keep this a diagnostic comparison: minimal remote omits MRP controls
and cannot establish its owned-item EOF. Earlier alternating unattended trials
confounded mode with order/idle state; they do not prove MRP caused the pause.
No matched minimal-remote comparison was triggered in this successful batch.

Per user decision D37, the original frozen-video failure is low priority unless
it recurs in these or later playback tests. Any recurrence restores active
investigation priority. Actual network disconnect/reconnect stays outside this
batch: the user previously said that checkpoint is unavailable. Do not alter
firewall/network/receiver settings or delete/re-pair credentials for these checks.

D40 adds a separate remote-Stop/sleep comparison with fixed remote-event
observations and retained final MRP state; see the dated
[validation record](receiver-validation.md) and
[artifact](validation/native-stop-diagnostics-windows-static-2026-10-07.json).
Its first attempt was unobserved and cannot establish remote action or Home.
The repeated Stop/Home result is separate from that trace. Classification
remains conservative until receiver intent has a validated protocol signal;
MRP ownership loss, pause or connection closure alone is insufficient.

## Additional Ethernet interruption/recovery check (D42)

The user became available for the previously deferred network action on 2026-10-07.
Two casts used the unchanged D40 runtime, normal MRP/16-slot/startup defaults and
existing credentials. The observer confirmed removing the Ethernet/network cable
during the established first cast, then reconnecting and reaching Home.

| Check | Native result | Observer scope | Status |
| --- | --- | --- | --- |
| Established playback interrupted by Ethernet removal; stdin open | URL feedback timeout, automatic joined cleanup, connection_lost/exit 1; no sender Stop | Disconnection and Ethernet cable removal confirmed; video/audio before action was asked about but not reported | PASS for selected failure cleanup |
| Fresh explicit cast after reconnection/Home | Same profile, planned seek 124/Play, owned receiver-reported EOF, media_end/exit 0, automatic cleanup | Home before cast and normal video/audio/Home after EOF confirmed | PASS for selected fresh recovery |

Exact quotes, traces and runtime fingerprints are in the
[artifact](validation/native-network-recovery-windows-static-2026-10-07.json).
User action times were not synchronized with diagnostics; do not treat the time
from the cue to cleanup as disconnect-detection latency. The recovery cast ends
after a planned seek and is not a full-clip proof. No sender firewall/network
setting, credential or native source changed; staged executables were restored.
The receiver Ethernet connection was temporarily removed and restored by the user.
Broader network cases and normal remote-Stop intent remain unvalidated.
