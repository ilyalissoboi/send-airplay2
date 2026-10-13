// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_STEREO_AAC_ENCODER_H
#define SEND_AIRPLAY2_STEREO_AAC_ENCODER_H
#include "audio_decoder_source.h"
#include "mp4_demux.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace send_airplay2::detail {
inline constexpr std::uint32_t stereo_aac_frame_samples = 1024;
inline constexpr std::uint32_t stereo_aac_bit_rate = 192000;
inline constexpr std::size_t stereo_downmix_max_channels = 8;
/// Native channel-mask bit order defines source interleaving. The fixed matrix
/// includes LFE at 0.5 and center/surround at sqrt(0.5), with correlated headroom.
/// Unknown speakers, masks/count disagreement and layouts above eight channels fail.
/// Returned weights are two rows of eight: left [0,8), right [8,16), unused zero.
[[nodiscard]] std::array<double, 16> stereo_downmix_matrix(std::uint64_t channel_mask,
                                                           std::uint32_t channels);
struct EncodedAacPacket {
    std::int64_t presentation_sample = 0; ///< First packet precedes epoch input by priming.
    Bytes bytes;
};
struct EncodedAacEpoch {
    Bytes audio_specific_config;
    std::uint32_t priming_samples = 0;
    std::vector<EncodedAacPacket> packets;
};
/** Borrowed synchronous PCM/context, no callbacks or retained state. Encode
 * 48 kHz stereo AAC-LC at 192 kbit/s after explicit downmix. Input is an owned
 * decoder window, 1..8 native channels, <=10 s, first_sample on the AAC grid.
 * Caller supplies bounded preroll/lookahead and retains only canonical packets.
 * Missing EOF tail within sample_count is deliberate movie-timeline silence;
 * no loudness normalization or hidden gain/DRC is applied. Output <=1 MiB.
 * Errors discard all tentative packets. AAC priming/padding is not audible content.
 */
[[nodiscard]] EncodedAacEpoch encode_stereo_aac_epoch(const DecodedAudioWindow& input,
                                                      std::uint32_t sample_count,
                                                      const MediaReadContext& context);
} // namespace send_airplay2::detail
#endif
