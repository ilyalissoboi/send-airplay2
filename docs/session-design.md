# Playback session lifecycle

`UrlPlaybackSession` combines authenticated control/event channels, timing,
feedback and playback tracking. The media server supplies its private URL.
Normal Apple TV operation also opens a separately verified remote-control/MRP
session. macOS receivers that explicitly refuse remote-control SETUP use the
implemented URL-command path instead.

## Start sequence

1. Prepare media and load trusted credentials before exposing a media URL.
2. Establish remote-control/MRP where supported. If it reports no logical
   devices, request wake and allow up to ten seconds for the receiver to settle.
3. Open and peer-verify the URL control connection. Start the route-bound timing
   responder, send base SETUP with session/correlation IDs and timing port, and
   connect the returned event port with independently derived keys.
4. Start periodic feedback, request info, issue RECORD and type-130 URL SETUP.
5. Insert the file queue item with a generated UUID and private Content-Location,
   configure item-end/date-range properties and send rate 1.
6. Wait for confirmed playing progress; loading alone is not successful start.
   If a start position is requested, apply the post-start seek under the
   appropriate ownership/control rules.

Every network phase has bounded failure/cancellation behavior. Non-2xx commands
fail rather than triggering speculative retries. The remote-control fallback is
for an explicit unsupported response, not an authentication or arbitrary network
failure. The session never silently re-pairs or adopts a replacement player.

## Threads and supervision

Control requests serialize under one mutex. Feedback normally runs every two
seconds; URL/remote event readers, MRP and timing have their own bounded I/O
owners. They publish state under synchronization, with callers polling or waiting.
The HTTP server has separate bounded source workers.

A lifecycle supervisor records the first terminal reason, cancels pending work
and performs cleanup. Concurrent Stop calls serialize the join. Readers signal
failure instead of joining themselves. Destruction requires external callers to
finish. Status remains readable after cleanup.

Teardown retains remote control while the URL session closes: stop initiating
feedback, drain in-flight work within its deadline, close URL events/control and
timing, then join/close MRP and remote events/control. The public controller
drains HTTP/conversion callbacks before releasing their host contexts. A silent
receiver can force deadline-based closure; normal ordering cannot be guaranteed
after that connection has already failed.

## Controls and termination

MRP controls require ownership of the item/player path; see [MRP](mrp-controls.md).
The URL fallback uses `setRate` for pause/resume, and `seek` with the queue item's
UUID and a CMTime dictionary. A seek without the item UUID can be accepted but
ignored by the Mac. URL Stop addresses the same item; explicit receiver EOF
ends the URL-only session naturally.

Pause is not EOF. MRP EOF uses receiver-reported paused/stopped position and
positive duration, not wall-clock extrapolation. After URL stopped/idle, allow
up to one second for reordered MRP telemetry; a position within 0.5 seconds of
the duration counts as media end. This can classify a receiver Stop in that final
half second as media end. Remote Stop/Home can otherwise appear as connection
loss, and closure alone does not establish receiver intent.

Failures and ownership loss end the cast without reconnecting, resuming or
controlling the replacement player. A new cast is an explicit host action using
retained credentials. Media-generation failure is tracked separately from
receiver channel failure. See the [C API](public-api.md) for cleanup flags and
handle lifetime, and [compatibility limits](receiver-validation.md).
