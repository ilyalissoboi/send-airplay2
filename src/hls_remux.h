// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_HLS_REMUX_H
#define SEND_AIRPLAY2_HLS_REMUX_H
#include "fmp4_writer.h"
#include "mp4_demux.h"
#include "send_airplay2/media_server.h"
#include <cstdint>
#include <string>
#include <vector>

namespace send_airplay2::detail {
/// Apple HLS authoring specification 7.5: target durations SHOULD be 6 s.
inline constexpr std::uint32_t default_segment_target_seconds = 6;

/** Media segments for `movie`. Each starts at a video sync sample (Apple HLS
 * 7.4): segment k (from 0) starts at the first sync sample after segment k-1
 * whose decode time is at least k targets. Cuts follow that grid, so a long
 * GOP lengthens one segment without delaying later cuts; a segment can still
 * exceed the target by up to one GOP. Audio samples go to the segment whose
 * video decode-time span contains their decode time; samples past the last
 * boundary go to the last segment. Sequence numbers start at 1.
 * @throws RemuxException (unsupported) if the first video sample is not sync.
 */
[[nodiscard]] std::vector<SegmentLayout>
plan_segments(const Mp4Movie& movie, std::uint32_t target_seconds = default_segment_target_seconds);

/** Duration of `segment` in microseconds, from the video decode times of its
 * first sample and of the next segment's first sample (or the end of the
 * video track for the last segment), rounded to the nearest microsecond. */
[[nodiscard]] std::uint64_t segment_duration_us(const Mp4Movie& movie,
                                                const std::vector<SegmentLayout>& segments,
                                                std::size_t index);

/** EXT-X-TARGETDURATION: the longest segment duration rounded to the nearest
 * second (RFC 8216 4.3.3.1), and at least 1. */
[[nodiscard]] std::uint32_t target_duration_seconds(const Mp4Movie& movie,
                                                    const std::vector<SegmentLayout>& segments);

/** The VOD media playlist (RFC 8216; Apple HLS 8.6, 8.20, 9.12): version 7,
 * target_duration_seconds(),
 * EXT-X-PLAYLIST-TYPE:VOD, EXT-X-INDEPENDENT-SEGMENTS, EXT-X-MAP for
 * init.mp4, one EXTINF with six decimals per segment named s<N>.m4s from 0,
 * and EXT-X-ENDLIST. Lines end in LF. */
[[nodiscard]] std::string media_playlist(const Mp4Movie& movie,
                                         const std::vector<SegmentLayout>& segments);

/// A remuxed presentation, ready for MediaServer::start_resource_set.
struct RemuxedHls {
    std::string playlist_name;
    std::vector<MediaResource> resources; ///< Playlist, init.mp4, then s0.m4s, s1.m4s, ...
    std::size_t segment_count = 0;
    std::uint32_t target_duration_seconds = 0;
};

/** Build an fMP4 VOD HLS presentation (D60) over a progressive MP4/MOV source
 * without copying its media: the playlist and init segment are generated
 * here and held in memory; each media segment's moof is generated per
 * request and its samples are read from `input` on demand.
 *
 * Calls input.size() once and reads the moov through input.read_at on this
 * thread (with an internal 60-second deadline per read). The returned
 * resources own `input`; their read_at forwards the request's
 * MediaReadContext, so cancellation and deadlines apply to source reads, and
 * may return short reads. They are thread-safe when input.read_at is.
 *
 * @throws RemuxException for input that read_mp4() or plan_segments() refuse,
 *         or a segment larger than 2 GiB (too_large).
 * @throws std::runtime_error when the source fails a read; std::invalid_argument
 *         for a source without callbacks. Messages contain no paths or URLs.
 */
[[nodiscard]] RemuxedHls remux_mp4_to_hls(MediaSource input);
} // namespace send_airplay2::detail
#endif
