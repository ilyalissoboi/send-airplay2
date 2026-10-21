# Manual receiver validation

Use this procedure for changed playback behavior or a reproducible regression.
Match the checks to the affected paths; unchanged lifecycle and codec matrices
do not need to be repeated for every change.

## Prepare

1. Record source commit, build profile, executable/native-library hashes, host
   platform/architecture and receiver model/firmware. A package version alone
   does not prove which DLLs are running.
2. Use an owned, unprotected fixture and record its hash and intended route.
   Prefer the [audio marker generators](../tools/audio-only-probe/README.md)
   or committed [MP3 fixtures](../tools/mp3-probe/README.md).
3. Discover/pair using the intended application's store. Confirm that the
   receiver can reach the sender's media listener. Do not substitute a desktop
   file path for a packaged file grant.
4. Agree on the sequence with the observer. Keep picture/sound/prompt observations
   distinct from sender statuses and request/read/cleanup counters.

## Focused playback sequence

| Check | Expected observation |
| --- | --- |
| Start, optionally at a nonzero position | Audible audio and moving video where expected; correct initial marker/position |
| Pause then resume | Receiver stops/continues presentation; synchronized sound/picture resumes |
| Forward and backward seeks | Correct marker/time after both; no stale audio or lost synchronization |
| Natural end | Receiver finishes without sender Stop; host reports end and drains resources |
| Separate explicit Stop | Presentation closes/silences, no delayed restart, host remains responsive |
| Host handoff when applicable | Local playback stays in the host's specified paused/stopped state |

For tone fixtures, start near eight seconds (RIGHT), seek to 32 (higher BOTH),
back to eight (RIGHT), then near 58 and let the clip finish. Use a separate
from-zero run for Stop. An audio-only cast needs no dummy video.

Repeat pending-consent cancellation only for relevant lifecycle changes or a
concrete defect. On a Mac, leave the prompt unanswered and cancel while start
is pending. Verify prompt closure, responsive host and joined cleanup. If recent
consent suppresses the prompt, record that missing coverage instead of calling
it a pending-consent pass.

For queue changes test natural audio/video transitions, rapid superseding Next
requests and Stop during transition. Verify the destination item and absence of
a delayed restart. For interruption/wake changes explicitly observe receiver
state and verify a fresh cast with retained credentials after recovery.

## Record results

Report PASS/FAIL/SKIP per affected check in the issue or PR, including failed
attempts and observer timing. Include the selected source/binary hashes and
firmware, not personal paths or a complete machine inventory. Document any
preparation/decoded-output/cleanup result separately from receiver presentation.

Never share PINs, credential records, bearer URLs, private filenames/content,
receiver names/addresses/identifiers or unfiltered logs. Prefer fixed error
categories, numeric status and bounded counters. Redact a report before sharing.
Do not change access policy, erase pairings or reset app data as an incidental
diagnostic step. Restore temporary user preferences after testing.
