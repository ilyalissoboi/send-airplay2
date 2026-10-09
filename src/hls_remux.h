// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_HLS_REMUX_H
#define SEND_AIRPLAY2_HLS_REMUX_H
#include "fmp4_writer.h"
#include "mp4_demux.h"
#include "send_airplay2/media_server.h"
#include <atomic>
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

/** The WebVTT media playlist of text track `track` (phase 3c): the same
 * target duration and EXTINF values as the video, so its segments cover the
 * same presentation windows, segment names t<track>s<N>.vtt, VOD, ENDLIST. */
[[nodiscard]] std::string subtitle_playlist(const Mp4Movie& movie,
                                            const std::vector<SegmentLayout>& segments,
                                            std::size_t track);

/// A remuxed presentation, ready for MediaServer::start_resource_set.
struct RemuxedHls {
    /// The entry point: index.m3u8 (the media playlist) without text tracks;
    /// main.m3u8 (a multivariant playlist naming index.m3u8 and one WebVTT
    /// rendition per text track) with them.
    std::string playlist_name;
    /// The playlists, init.mp4, s0.m4s, s1.m4s, ..., and for text track T its
    /// playlist t<T>.m3u8 and segments t<T>s0.vtt, t<T>s1.vtt, ...
    std::vector<MediaResource> resources;
    std::size_t segment_count = 0;
    std::size_t text_track_count = 0;
    std::uint32_t target_duration_seconds = 0;
};

/** Build an fMP4 VOD HLS presentation (D60) over a progressive MP4/MOV or a
 * Matroska source, chosen by its first bytes, without copying its media: the
 * playlist and init segment are generated here and held in memory; each media
 * segment's moof is generated per request and its samples are read from
 * `input` on demand.
 *
 * Text subtitle tracks of a Matroska source become WebVTT renditions of a
 * multivariant playlist (phase 3c); their segments are generated here.
 *
 * Calls input.size() once and reads the moov, or the Matroska track metadata
 * and every block header, through input.read_at on this thread (with an
 * internal 60-second deadline per read). Those reads see `cancelled` (when
 * given) as their MediaReadContext's request cancellation, so setting it ends
 * the remux promptly with an exception. The returned resources own `input`;
 * their read_at forwards the request's MediaReadContext, so cancellation and
 * deadlines apply to source reads, and may return short reads. They are
 * thread-safe when input.read_at is.
 *
 * @throws RemuxException for input that read_mp4(), read_mkv() or
 *         plan_segments() refuse, or a segment larger than 2 GiB (too_large).
 * @throws std::runtime_error when the source fails a read; std::invalid_argument
 *         for a source without callbacks. Messages contain no paths or URLs.
 */
[[nodiscard]] RemuxedHls remux_to_hls(MediaSource input,
                                      const std::atomic_bool* cancelled = nullptr);
} // namespace send_airplay2::detail
#endif
