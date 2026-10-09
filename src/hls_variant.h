// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_HLS_VARIANT_H
#define SEND_AIRPLAY2_HLS_VARIANT_H
#include "mp4_demux.h"
#include <cstdint>
#include <string>
#include <vector>

namespace send_airplay2::detail {
/** The CODECS entry for a remuxed track, from its sample entry: avc1.PPCCLL
 * (RFC 6381 3.3, from avcC), hvc1.<profile>.<compatibility>.<tier+level>
 * [.<constraints>] (ISO/IEC 14496-15 Annex E.3, from hvcC), mp4a.40.<object
 * type> (RFC 6381 3.3, from the esds AudioSpecificConfig), ac-3 or ec-3. The
 * avc3/hev1 four-character codes are kept as the prefix.
 * @throws RemuxException (malformed) when the configuration box is missing or
 *         shorter than the fields read. */
[[nodiscard]] std::string codec_string(const Mp4Track& track);

/// One subtitle rendition of the multivariant playlist.
struct SubtitleRendition {
    std::string uri;  ///< Its media playlist's resource name.
    std::string name; ///< Unique within the group.
    std::string language;
    bool default_track = false;
    bool forced = false;
    bool hearing_impaired = false;
};

/// What EXT-X-STREAM-INF declares about the one variant.
struct VariantStream {
    std::string uri; ///< The audio/video media playlist's resource name.
    std::uint64_t peak_bits_per_second = 0;
    std::uint64_t average_bits_per_second = 0;
    std::uint32_t width = 0; ///< Display width; 0 omits RESOLUTION.
    std::uint32_t height = 0;
    std::uint64_t frame_rate_milli = 0; ///< Frames per 1000 s; 0 omits FRAME-RATE.
    std::string codecs;
};

/** The multivariant playlist (RFC 8216bis 4.4.6; Apple HLS 4.5-4.7, 5.11, 9.1,
 * 9.2, 9.11-9.15): EXT-X-VERSION 7, EXT-X-INDEPENDENT-SEGMENTS, one
 * EXT-X-MEDIA TYPE=SUBTITLES per rendition in GROUP-ID "subs" (LANGUAGE,
 * NAME, DEFAULT, AUTOSELECT=YES, FORCED, and the accessibility
 * CHARACTERISTICS for hearing-impaired tracks), and one EXT-X-STREAM-INF with
 * BANDWIDTH, AVERAGE-BANDWIDTH, CODECS, RESOLUTION, FRAME-RATE and
 * SUBTITLES="subs". Lines end in LF. */
[[nodiscard]] std::string multivariant_playlist(const VariantStream& variant,
                                                const std::vector<SubtitleRendition>& subtitles);
} // namespace send_airplay2::detail
#endif
