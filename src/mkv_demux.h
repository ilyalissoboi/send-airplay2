// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_MKV_DEMUX_H
#define SEND_AIRPLAY2_MKV_DEMUX_H
#include "mp4_demux.h"
#include <cstddef>
#include <cstdint>

namespace send_airplay2::detail {
/// Whether `data` starts with the EBML header ID (Matroska and WebM files).
[[nodiscard]] bool starts_like_matroska(const std::uint8_t* data, std::size_t size);

/** Read the sample tables of a Matroska file (RFC 9559, EBML RFC 8794) of
 * `file_size` bytes through `read`, as the remux's track model (D60).
 *
 * Selects the first enabled video track with V_MPEG4/ISO/AVC or
 * V_MPEGH/ISO/HEVC, and the audio track with A_AAC, A_AC3 or A_EAC3, taking a
 * default-flagged one before the first enabled one. Sample entries, track
 * headers and languages are built from the track's CodecPrivate, video size
 * and first audio frame (sample_entries.h). Other tracks are ignored.
 *
 * Every cluster's block headers are read; frame payloads are not, except the
 * first AC-3/E-AC-3 frame. Video decode times are the sorted presentation
 * times (Matroska stores no decode times); presentation is delayed by the
 * reorder delay so composition offsets stay non-negative, and an edit list
 * removes that delay. The video timescale is 10^9 / TimestampScale. Audio
 * uses its sample rate as the timescale and one codec frame per sample
 * (1024 or 960 for AAC, 1536 for AC-3, 256 x blocks for E-AC-3), following a
 * block's timestamp where it differs from that timeline by more than the
 * TimestampScale rounding (half a tick) plus one sample; a CodecDelay
 * becomes an edit list that skips the priming samples.
 *
 * Refusals (RemuxException): `unsupported` for other codecs (an audio track
 * that cannot be remuxed refuses the file rather than casting it silent),
 * ContentEncodings (compression or encryption), laced video, negative
 * timestamps, a TimestampScale that does not divide 10^9, or E-AC-3 with
 * dependent substreams; `malformed` for invalid EBML, a missing codec
 * configuration or frames outside their blocks; `too_large` beyond 16 MiB of
 * track metadata or 4,000,000 frames per track.
 */
[[nodiscard]] Mp4Movie read_mkv(const RandomReader& read, std::uint64_t file_size);
} // namespace send_airplay2::detail
#endif
