// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_SAMPLE_ENTRIES_H
#define SEND_AIRPLAY2_SAMPLE_ENTRIES_H
#include "mp4_demux.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace send_airplay2::detail {
/** ISO BMFF sample entries and track headers for tracks that come from a
 * container without them (MKV, D60). Each function returns a complete box.
 * Written from ISO/IEC 14496-12/-14/-15 and ETSI TS 102 366 Annex F. */

/** 'avc1' or 'hvc1' VisualSampleEntry with the decoder configuration record
 * as its avcC/hvcC child, copied byte for byte. `codec` is "avc1" or "hvc1".
 * When the display size differs from the coded size, a 'pasp' box records
 * the pixel aspect ratio (display_width x height : width x display_height,
 * reduced). */
[[nodiscard]] Bytes visual_sample_entry(const std::string& codec, std::uint16_t width,
                                        std::uint16_t height, std::uint32_t display_width,
                                        std::uint32_t display_height,
                                        const Bytes& decoder_configuration);

/// AAC settings read from an AudioSpecificConfig (ISO/IEC 14496-3 1.6.2.1).
struct AacConfig {
    std::uint8_t object_type = 0;
    std::uint32_t sample_rate = 0; ///< Core sampling frequency.
    std::uint8_t channels = 0;     ///< channelConfiguration; 0 means a PCE.
};

/** Reads the leading fields of an AudioSpecificConfig.
 * @throws RemuxException (malformed) when it is shorter than those fields or
 *         names a reserved sampling frequency index. */
[[nodiscard]] AacConfig read_aac_config(const Bytes& audio_specific_config);

/** A two-byte AudioSpecificConfig for an MKV A_AAC/... codec ID without
 * CodecPrivate: object type, sampling frequency index and channels.
 * @throws RemuxException (unsupported) for a rate outside the index table. */
[[nodiscard]] Bytes aac_config(std::uint8_t object_type, std::uint32_t sample_rate,
                               std::uint8_t channels);

/** 'mp4a' AudioSampleEntry with an esds (ISO/IEC 14496-1 ES_Descriptor,
 * object type 0x40) whose DecoderSpecificInfo is `audio_specific_config`. */
[[nodiscard]] Bytes aac_sample_entry(std::uint8_t channels, std::uint32_t sample_rate,
                                     const Bytes& audio_specific_config);

/// One AC-3 or E-AC-3 sync frame header, as needed for dac3/dec3.
struct DolbyFrame {
    bool enhanced = false;         ///< E-AC-3 (bsid 16) rather than AC-3.
    std::uint32_t sample_rate = 0; ///< 48000, 44100 or 32000 (or halved rates for E-AC-3).
    std::uint32_t frame_bytes = 0; ///< Size of this sync frame.
    std::uint32_t samples = 0;     ///< PCM samples per sync frame (1536 for AC-3).
    std::uint8_t fscod = 0;
    std::uint8_t bsid = 0;
    std::uint8_t bsmod = 0;
    std::uint8_t acmod = 0;
    std::uint8_t lfeon = 0;
    std::uint8_t channels = 0;      ///< Full-bandwidth channels plus LFE.
    std::uint8_t bit_rate_code = 0; ///< AC-3 only: frmsizecod >> 1.
    std::uint8_t stream_type = 0;   ///< E-AC-3 only: strmtyp.
};

/** Parses the sync frame header at the start of `data` (ETSI TS 102 366
 * 4.4 for AC-3, E.1.2 for E-AC-3).
 * @throws RemuxException (malformed) without a sync word or valid header;
 *         (unsupported) for a bsid outside 0..8 and 11..16. */
[[nodiscard]] DolbyFrame read_dolby_frame(const std::uint8_t* data, std::size_t size);

/** 'ac-3' AudioSampleEntry with dac3 (TS 102 366 F.4) from `frame`. */
[[nodiscard]] Bytes ac3_sample_entry(const DolbyFrame& frame);

/** 'ec-3' AudioSampleEntry with dec3 (TS 102 366 F.6) for one independent
 * substream without dependent substreams; data_rate in kbit/s.
 * @throws RemuxException (unsupported) for a dependent-substream frame. */
[[nodiscard]] Bytes eac3_sample_entry(const DolbyFrame& frame);

/** tkhd (version 0, enabled and in movie) with `track_id`, zero duration,
 * full volume for audio and the display size (16.16) for video. */
[[nodiscard]] Bytes track_header_box(TrackKind kind, std::uint32_t track_id,
                                     std::uint32_t display_width, std::uint32_t display_height);

/// mdhd packed ISO-639-2/T language for a three-letter code; "und" otherwise.
[[nodiscard]] std::uint16_t packed_language(const std::string& code);
} // namespace send_airplay2::detail
#endif
