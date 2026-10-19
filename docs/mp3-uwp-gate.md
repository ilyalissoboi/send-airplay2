# D104: packaged UWP progressive MP3 gate

2026-10-19 (Asia/Tokyo), gate passed on both recorded receivers. The user
authorizes this slice after
native #88 merged as be5ae92, with all 17 checks passing at final head 552ebca in
[run 39201242905](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39201242905).
Both recorded receivers are available; macOS 26.7.1/tvOS 26.6 are unchanged.

## Test-host scope

The test host adds an MP3 picker entry, audio/mpeg for progressive form casts,
and explicit type=audio/mpeg scripts. Explicit MIME still requires progressive
delivery and unknown types refuse before brokered file access. Default video
behavior, native API/playback, codec profile and Screenbox are unchanged.
The default capability package advances to 0.1.32.0, with the alternate-server
manifest 0.1.33.0. Only the default codec-free x64 package is built/tested here;
registration updates in place to preserve identity, PasswordVault and file grants.

The gate uses unchanged original 64-second stereo CBR 48-kHz and VBR 44.1-kHz fixtures
from tools/mp3-probe/fixtures, with their committed provenance/hashes. The user
grants each file manually and provides written listening/prompt observations.
No Computer Use visual confirmation or private media is required.

The actual MSIX, AppContainer and loaded executable/two native modules are inspected.
For both CBR/VBR files on each receiver, the scripts request start near eight seconds,
pause/resume, seek to 32 seconds (higher BOTH marker), back to eight (RIGHT),
then near 58 and allow natural end. A separate cast from zero gets Stop after
eight seconds, followed by a three-second observation interval. Mac unanswered
consent cancellation is a separate manual Stop check. Exact source release
counts and telemetry remain separate from written user observations.

## Build and runtime checkpoint

Implementation b098646 builds the actual x64 Native AOT MSIX with zero warnings
or errors. The native AppContainer/app C runtime/no desktop credential-import
checks pass. Version 0.1.32.0 updates the existing 0.1.30.0 identity in place;
PasswordVault, grants and app data are preserved. Only internetClient and
privateNetworkClientServer are present on the existing Private network. Both
native DLL hashes match the build; no codec DLL or desktop Botan DLL is present.
The actual running AppContainer/executable/two native module paths and hashes
match the inspected package before fixture grants, before receivers, after
cancellation, after Mac checks and after the final Apple TV sequence. These are
five point-in-time captures, not continuous process monitoring.

The packaged negative scripts reject unknown MIME and type=audio/mpeg with HLS
before looking up the absent file label; neither starts a cast or source. The
user manually selects the two exact owned fixture paths as mp3-cbr/mp3-vbr.
The app logs their matching 1,537,388/268,288-byte sizes. Saved PasswordVault
profiles and model-selected discovery work without re-pairing or settings changes.

## Receiver observations and cleanup

All listening/prompt observations are written user replies; no Computer Use is
used. The user answers Yes to each complete sequence and consent check.

| Receiver/case | Written observation | Brokered source evidence |
| --- | --- | --- |
| Mac consent cancellation | Unanswered prompt closes after Stop; app responsive | Cancelled after 2,252 ms; zero reads/bytes; one release |
| Mac CBR | Requested nonzero start, marker controls, natural end and separate Stop/no delayed restart pass | 25 reads / 1,537,390 cumulative bytes, one release per cast |
| Mac VBR | Same checks pass | 6 reads / 268,290 cumulative bytes, one release per cast |
| Apple TV CBR | Same checks pass | 25 reads / 1,537,390 cumulative bytes, one release per cast |
| Apple TV VBR | Same checks pass | Natural: 9 reads / 495,682 bytes; Stop: 8 reads / 425,802 bytes; one release each |

All four playback scripts finish successfully, each with two casts. The single
intended cancellation reports cancelled and ends its script at the cast line;
this is the expected cancellation result, not a playback failure. All nine
sources release exactly once. Duplicate cancellation disposal messages do not
mean duplicate source releases. There are no timeouts or failed playback attempts
in this gate. Each explicit Stop includes a three-second post-Stop wait.

Natural-end snapshots precede disposal and retain cleaned=no; the subsequent
source observer runs after both brokered streams close. Stop reports cleaned=yes;
the cached playback state can still be Playing, with actual audio closure
confirmed separately by the user. Early Start snapshots near zero and subsequent
status near ten seconds after the requested eight-second start are retained.
Apple TV's end-position estimate is 63.6 seconds. This is not sample-exact initial
seek, endpoint or gapless playback evidence. Cumulative reads can overlap, so
their byte counts need not equal unique file size. Progressive delivery creates
no conversion fragment store and changes no quota.

The [sanitized record](validation/mp3-uwp-gate-windows-2026-10-19.json) preserves
exact package/source/binary/fixture hashes, sanitized script logs, observers and five
runtime captures. Native D103 remains historical evidence for its own bytes.

## Decision and next work

Direct progressive audio/mpeg passes this bounded stereo CBR/VBR subset through
both native and packaged brokered sources on the two recorded receivers. No AAC
fallback is demonstrated or added. No further D104 manual check is pending.

After review/merge, continue the planned edited MP4/Matroska audio-only admission
work, then automatic audio-only delivery/public reporting and native/packaged
receiver gates before broader Screenbox routing/queue checks. Public/default MP3
admission and a Screenbox MP3 feature are not implemented by these test-host
options. No package/release is published. Preserve speaker/HomePod/RAOP, discrete
surround, Chromecast, remote-Stop classification and separate long-input/cache
quota deferrals.
