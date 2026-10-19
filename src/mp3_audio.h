// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_MP3_AUDIO_H
#define SEND_AIRPLAY2_MP3_AUDIO_H
#include "send_airplay2/media_server.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace send_airplay2::detail {
struct Mp3SeekPoint {
    std::uint64_t byte_offset = 0;
    std::uint64_t coded_sample = 0; ///< Per-channel source clock, before encoder trim.
    std::uint16_t reservoir_back_bytes = 0;
};
struct Mp3EncoderTrim {
    std::uint16_t delay = 0, padding = 0; ///< Encoder metadata; not decoder synthesis latency.
};
struct Mp3AudioInventory {
    std::uint32_t sample_rate = 0, channels = 0;
    std::uint32_t samples_per_frame = 0;
    std::uint64_t audio_begin = 0, audio_end = 0; ///< MPEG extent, excluding ID3 tags.
    std::uint64_t frame_count = 0; ///< Audio frames, excluding a recognized Xing/Info frame.
    std::uint64_t coded_samples = 0;
    bool variable_bitrate = false;
    bool xing = false;
    std::optional<Mp3EncoderTrim> encoder_trim;
    std::optional<std::uint64_t> audible_samples; ///< Only set for checked gapless metadata.
    std::vector<Mp3SeekPoint> seek_points;
};
/** Private MP3 framing inventory; default/public probes and AAC routing are unchanged.
 * Synchronously borrows immutable source/context. Admits unprotected MPEG-1/2/2.5
 * layer III, 8..48 kHz, mono/stereo, indexed CBR/VBR, leading ID3v2.3/2.4 with
 * zero flags and trailing ID3v1. Skips opaque ID3 bodies; no labels are retained.
 * Checks frame extent, stable rate/version/channel count and reservoir distance,
 * not Huffman payload integrity. Xing/Info frame/byte counts must match a full
 * frame-header scan; known LAME/Lavc/Lavf trim requires a valid tag CRC.
 * Bounds: source <=1 TiB, ID3v2 <=1 MiB, 1,000,000 physical frames, <=65536
 * one-second seek locators, <=64 MiB requested bytes, <=2,100,000 callbacks,
 * <=64 KiB capacity per callback, positive coded duration <7 days.
 * Seek points are locators, NOT independently decodable restart points: MP3's
 * reservoir and decoder/resampler preroll must be reconstructed before use.
 * Unknown trim stays unknown, not zero. No decoder's synthesis delay is applied.
 * Refuses protected/free-format/changing/junk/truncated frames,
 * VBRI, repeated/flagged ID3 and unsupported gapless extensions.
 * @throws RemuxException for malformed/unsupported/limited input; invalid_argument
 * for missing callbacks, runtime_error for cancelled/expired/failed reads.
 * Callback exceptions propagate; no callback is retained after return.
 */
[[nodiscard]] Mp3AudioInventory probe_mp3_audio(const MediaSource& source,
                                                const MediaReadContext& context);
/** Locate a preceding indexed frame for an audible source-clock target.
 * Adds encoder delay only when known. Clamps past-end targets to the last
 * locator; does not establish a decoder restart or receiver seek result.
 * Requires an unmodified inventory returned by probe_mp3_audio().
 */
[[nodiscard]] Mp3SeekPoint locate_mp3_sample(const Mp3AudioInventory& inventory,
                                             std::uint64_t audible_sample);
} // namespace send_airplay2::detail
#endif
