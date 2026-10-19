// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_OGG_AUDIO_H
#define SEND_AIRPLAY2_OGG_AUDIO_H
#include "media_track_inventory.h"

namespace send_airplay2::detail {
/** Private single-logical-stream Ogg Opus/Vorbis inventory, identity 1.
 * Synchronously borrows callbacks/context; checks every page CRC and scans at
 * most 64 MiB, 65536 pages, 1048576 callbacks (each <=64 KiB). A packet is at
 * most 1 MiB. Requires BOS, consecutive sequence numbers, one serial, complete
 * headers and final EOS at physical EOF. Chaining/grouping are not admitted.
 * Granules supply a positive duration <7 days; Opus subtracts pre-skip once.
 * Decoder validation of packet contents, audible origin/end and seek coverage
 * remains required. This bounded full scan is not a large-file index.
 * @throws RemuxException for malformed/unsupported/oversized input;
 * invalid_argument for missing callbacks, runtime_error for cancelled/failed
 * reads. Callback exceptions propagate; no callbacks are retained on return.
 */
[[nodiscard]] MediaTrackInventory probe_ogg_audio_tracks(const MediaSource& source,
                                                         const MediaReadContext& context);
} // namespace send_airplay2::detail
#endif
