// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_STANDALONE_AUDIO_H
#define SEND_AIRPLAY2_STANDALONE_AUDIO_H
#include "media_track_inventory.h"

namespace send_airplay2::detail {
/// True only for the explicitly admitted single-stream standalone containers.
[[nodiscard]] bool is_standalone_audio(SourceContainer container) noexcept;
/** Private audio-only probe; existing public/video inventory behavior is unchanged.
 * Detects native FLAC, RIFF/RF64 WAVE or Ogg Opus/Vorbis, otherwise uses the existing
 * MP4/Matroska inventory. Standalone audio has one stable source identity, 1.
 * FLAC/WAVE reads only headers; Ogg performs its separately bounded <=64 MiB
 * full-page CRC scan (ogg_audio.h). Borrowed callbacks only; no file/network I/O.
 * The FLAC/WAVE bounds below apply before decoding; MP4/Matroska delegates
 * its legacy probe after a four-byte signature read.
 * Bounds: immutable source <=1 TiB, 4096 metadata/chunk headers, FLAC metadata
 * <=16 MiB, 65536 seek points, 128 MiB requested bytes, 1048576 callbacks,
 * <=64 KiB per callback. Context pointers must be valid; calls are synchronous.
 * Admits FLAC 16/24-bit and WAVE signed PCM16/24/32 or float32/64, 8..192 kHz,
 * 1..8 channels with an explicit supported layout for surround. Refuses unknown
 * FLAC duration, pictures, malformed tables, compressed WAVE, reduced valid-bit
 * packing and RF64 extra size tables. Duration must be positive and <7 days.
 * @throws RemuxException for malformed/unsupported/limited input; source callback
 * exceptions propagate; cancellation/deadline/read failures throw runtime_error.
 */
[[nodiscard]] MediaTrackInventory probe_audio_only_tracks(const MediaSource& source,
                                                          const MediaReadContext& context);
} // namespace send_airplay2::detail
#endif
