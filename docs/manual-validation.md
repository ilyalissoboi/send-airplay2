# Deferred receiver checks

Queue created 2026-10-07 (Asia/Tokyo) under D38. The user is unavailable for
manual testing for approximately the next hour. Run this batch when the user
returns and announces availability; do not request observations during that
window. No scheduled reminder or unattended visual pass is implied.

Use the existing Living Room receiver and local MP4 with stored native credentials.
Codex starts each checkpoint and captures sanitized startup/media/status diagnostics;
the observer uses the TV remote only at the announced checkpoints. Keep exact
CLI/source fingerprints for the completed batch. These checks target the rebuilt
D38 normal configuration: MRP enabled, 16 media slots, a one-second startup
confirmation interval and no automatic Play/seek retry.

| ID | Checkpoint | Observation to report | Status |
|---|---|---|---|
| B1 | Fresh cast from idle/Home, then fresh cast after sender Stop | Does moving video/audio begin without remote input? If the first frame stays still, report that and whether automatic deadline cleanup returns Home. Do not press Play to conceal the result. | PENDING |
| B2 | Native pause/resume, forward/backward seek and sender Stop, followed by a separate full natural-EOF run | Visible controls, normal resumed video/audio, full moving video/audio through EOF and Home after Stop/EOF. Report any buffering or freeze separately. | PENDING |
| B3 | Receiver remote Stop during established playback | Exit playback with the remote at the cue; report Home return. Correlate with retained URL/MRP diagnostics. Socket closure still conservatively means connection_lost; do not infer a normal reason from Home alone. | PENDING |
| B4 | Sleep receiver during playback, wake to Home, then fresh native cast with the same credentials | Sleep and Home after wake; moving video/audio from fresh startup; automatic cleanup/Home at near-end EOF. | PENDING |

B1/B4 help distinguish startup transitions from sustained playback failures.
The one-second interval filters short contradictory telemetry; it does not make
the receiver resume and does not prove decoder progress. Earlier G1/G2/D36 visual
results remain evidence for their own configurations. New D38 manual results
remain pending until observed.

If B1 reproduces startup pausing, capture the failed-start trace and cleanup
first. Then compare explicit `--minimal-remote` and normal MRP casts from the
same observed Home/idle condition, recording order and time since the previous
session. Keep this a diagnostic comparison: minimal remote omits MRP controls
and cannot establish its owned-item EOF. Earlier alternating unattended trials
confounded mode with order/idle state; they do not prove MRP caused the pause.

Per user decision D37, the original frozen-video failure is low priority unless
it recurs in these or later playback tests. Any recurrence restores active
investigation priority. Actual network disconnect/reconnect stays outside this
batch: the user previously said that checkpoint is unavailable. Do not alter
firewall/network/receiver settings or delete/re-pair credentials for these checks.
