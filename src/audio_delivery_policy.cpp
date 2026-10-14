// SPDX-License-Identifier: Apache-2.0
#include "audio_delivery_policy.h"
#include "sample_entries.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace send_airplay2::detail {
namespace {
constexpr std::uint8_t mpeg4_audio_object_type = 0x40;
constexpr std::uint8_t dts_core_object_type = 0xa9;
constexpr std::size_t dolby_copy_header_bytes = 16;
constexpr std::size_t ac3_configuration_bytes = 3;
constexpr std::size_t eac3_configuration_bytes = 5;
constexpr unsigned eac3_dependent_count_mask = 0x1e;
[[noreturn]] void unsupported(const char* message) {
    throw RemuxException(RemuxFailure::unsupported, message);
}
[[noreturn]] void malformed(const char* message) {
    throw RemuxException(RemuxFailure::malformed, message);
}
AacConfig copy_aac_config(const Bytes& config) {
    const auto parsed = read_aac_config(config);
    // Exact two-byte LC GASpecificConfig: no SBR/PS, PCE, frameLengthFlag,
    // core-coder dependency or unvalidated trailing bytes. Also admit the exact
    // sync-extension form with extension type 0x2b7, object type 5 and SBR absent.
    const auto plain = aac_config(2, parsed.sample_rate, parsed.channels);
    auto no_sbr_extension = plain;
    no_sbr_extension.insert(no_sbr_extension.end(), {0x56, 0xe5, 0x00});
    if (parsed.object_type != 2 || (parsed.sample_rate != 44100 && parsed.sample_rate != 48000) ||
        (parsed.channels != 1 && parsed.channels != 2 && parsed.channels != 6) ||
        (config != plain && config != no_sbr_extension)) {
        unsupported("AAC configuration is outside the Apple HLS v1 copy table");
    }
    return parsed;
}
void validate_copy_packet_durations(const Mp4Track& audio, std::uint32_t coded_frame_samples) {
    for (std::size_t index = 0; index < audio.samples.size(); ++index) {
        const auto duration = audio.samples[index].duration;
        const bool final_packet = index + 1 == audio.samples.size();
        if (duration > coded_frame_samples || (!final_packet && duration != coded_frame_samples)) {
            unsupported("copy packet duration is outside the admitted coded frame clock");
        }
    }
}
SourceAudioFamily source_family(const SourceTrack& track, SourceContainer container) {
    const auto& codec = track.codec;
    if (container == SourceContainer::mp4) {
        if (codec == "mp4a") {
            const auto type = mp4_audio_object_type(track.configuration);
            if (type == mpeg4_audio_object_type) {
                return SourceAudioFamily::aac;
            }
            if (type == dts_core_object_type) {
                return SourceAudioFamily::dts;
            }
        }
        if (codec == "ac-3") {
            return SourceAudioFamily::ac3;
        }
        if (codec == "ec-3") {
            return SourceAudioFamily::eac3;
        }
        if (codec == "dtsc") {
            return SourceAudioFamily::dts;
        }
        if (codec == "fLaC") {
            return SourceAudioFamily::flac;
        }
        if (codec == "Opus") {
            return SourceAudioFamily::opus;
        }
        if (codec == "sowt" || codec == "twos" || codec == "in24" || codec == "in32" ||
            codec == "fl32" || codec == "fl64") {
            return SourceAudioFamily::pcm;
        }
    } else {
        if (codec == "A_AAC") {
            return SourceAudioFamily::aac;
        }
        if (codec == "A_AC3") {
            return SourceAudioFamily::ac3;
        }
        if (codec == "A_EAC3") {
            return SourceAudioFamily::eac3;
        }
        if (codec == "A_DTS") {
            return SourceAudioFamily::dts;
        }
        if (codec == "A_FLAC") {
            return SourceAudioFamily::flac;
        }
        if (codec == "A_OPUS") {
            return SourceAudioFamily::opus;
        }
        if (codec == "A_VORBIS") {
            return SourceAudioFamily::vorbis;
        }
        if (codec == "A_TRUEHD") {
            return SourceAudioFamily::truehd;
        }
        if (codec == "A_PCM/INT/LIT" || codec == "A_PCM/INT/BIG" || codec == "A_PCM/FLOAT/IEEE") {
            return SourceAudioFamily::pcm;
        }
    }
    unsupported("selected audio has no validated delivery family");
}
/// Restricted TS 102 366 E.1.2 independent, six-block BSI shape. Additional
/// BSI (including JOC), mixing/informational metadata and compression are outside
/// this first copy table. Refusal is not a claim that a receiver rejects them.
bool plain_eac3_bsi(const std::array<std::uint8_t, dolby_copy_header_bytes>& header) {
    const auto bit = [&header](std::size_t index) {
        return (header[index / 8] >> (7 - index % 8)) & 1U;
    };
    constexpr std::size_t substream_id_first_bit = 18;
    constexpr std::size_t compression_exists_bit = 50;
    constexpr std::size_t mixing_metadata_bit = 51;
    constexpr std::size_t informational_metadata_bit = 52;
    constexpr std::size_t additional_bsi_bit = 53;
    return !bit(substream_id_first_bit) && !bit(substream_id_first_bit + 1) &&
           !bit(substream_id_first_bit + 2) && !bit(compression_exists_bit) &&
           !bit(mixing_metadata_bit) && !bit(informational_metadata_bit) &&
           !bit(additional_bsi_bit);
}
} // namespace
AudioDeliveryPlan plan_audio_delivery(const MediaTrackInventory& inventory,
                                      AudioDeliveryProfile profile,
                                      std::optional<std::uint64_t> selected_id) {
    if (profile != AudioDeliveryProfile::apple_hls_v1) {
        unsupported("unknown audio delivery profile");
    }
    const auto selected = select_source_audio(inventory, selected_id);
    AudioDeliveryPlan plan;
    plan.selection = selected.status;
    if (selected.status != AudioSelectionStatus::selected) {
        return plan;
    }
    const auto& track = inventory.tracks.at(selected.track_index);
    if (track.encoded || !track.standard_timeline ||
        (inventory.container == SourceContainer::mp4 && track.sample_description_count != 1)) {
        unsupported("selected audio protection, descriptions or timeline is unsupported");
    }
    plan.source_id = track.id;
    plan.source_family = source_family(track, inventory.container);
    if (plan.source_family == SourceAudioFamily::aac) {
        Bytes config = track.configuration;
        if (inventory.container == SourceContainer::mp4) {
            const auto specific = mp4_audio_specific_config(track.configuration);
            if (!specific) {
                malformed("selected AAC has no DecoderSpecificInfo");
            }
            config = *specific;
        }
        const auto aac = copy_aac_config(config);
        const auto effective_rate = track.audio_output_sample_rate != 0
                                        ? track.audio_output_sample_rate
                                        : track.audio_sample_rate;
        if (inventory.container == SourceContainer::matroska &&
            (effective_rate != aac.sample_rate || track.audio_channels != aac.channels)) {
            malformed("AAC source metadata disagrees with its copy configuration");
        }
        plan.output_channels = aac.channels;
    }
    if (plan.source_family == SourceAudioFamily::aac ||
        plan.source_family == SourceAudioFamily::ac3 ||
        plan.source_family == SourceAudioFamily::eac3) {
        plan.action = AudioDeliveryAction::copy;
        plan.reason = AudioDeliveryReason::profile_copy;
        plan.output_family = plan.source_family;
    } else {
        plan.action = AudioDeliveryAction::stereo_aac;
        plan.reason = AudioDeliveryReason::profile_requires_stereo;
        plan.output_family = SourceAudioFamily::aac;
        plan.output_channels = 2;
    }
    return plan;
}
std::uint32_t validate_profile_copy_audio(const Mp4Track& audio, const RandomReader& read) {
    validate_mp4_audio_index(audio);
    if (mp4_audio_entry_version(audio.sample_entry) != 0) {
        unsupported("copy table requires an ISO version-zero audio sample entry");
    }
    if (audio.codec == "mp4a") {
        const auto config = mp4_audio_specific_config(audio.sample_entry);
        if (!config) {
            unsupported("selected mp4a is not MPEG-4 AAC");
        }
        const auto aac = copy_aac_config(*config);
        if (audio.timescale != aac.sample_rate) {
            malformed("AAC sample clock disagrees with configuration");
        }
        validate_copy_packet_durations(audio, aac.frame_samples);
        return aac.channels;
    }
    if (audio.codec != "ac-3" && audio.codec != "ec-3") {
        unsupported("selected audio is not copy eligible");
    }
    const bool enhanced = audio.codec == "ec-3";
    const auto configuration =
        mp4_audio_private_box(audio.sample_entry, enhanced ? "dec3" : "dac3");
    if ((enhanced &&
         (configuration.size() != eac3_configuration_bytes || (configuration[1] & 7) != 0 ||
          (configuration[4] & eac3_dependent_count_mask) != 0)) ||
        (!enhanced && configuration.size() != ac3_configuration_bytes)) {
        unsupported("Dolby configuration is outside the single-independent-stream copy table");
    }
    constexpr std::uint32_t dolby_coded_frame_samples = 1536;
    validate_copy_packet_durations(audio, dolby_coded_frame_samples);
    std::uint32_t channels = 0;
    for (const auto& packet : audio.samples) {
        std::array<std::uint8_t, dolby_copy_header_bytes> header{};
        if (packet.size < header.size()) {
            malformed("copied Dolby unit is shorter than its header");
        }
        read(packet.offset, header.data(), header.size());
        const auto frame = read_dolby_frame(header.data(), header.size());
        if (frame.enhanced != enhanced || frame.frame_bytes > packet.size) {
            malformed("copied Dolby packet/configuration association is invalid");
        }
        if (frame.frame_bytes < packet.size) {
            unsupported("copied Dolby block contains multiple or dependent units");
        }
        if ((frame.sample_rate != 44100 && frame.sample_rate != 48000) || frame.samples != 1536 ||
            !(((frame.acmod == 1 || frame.acmod == 2) && frame.lfeon == 0) ||
              (frame.acmod == 7 && frame.lfeon == 1)) ||
            frame.bsid != (enhanced ? 16 : 8) || frame.bsmod != 0 ||
            (enhanced && (frame.stream_type != 0 || !plain_eac3_bsi(header)))) {
            unsupported("Dolby unit is outside the Apple HLS v1 copy table");
        }
        if (audio.timescale != frame.sample_rate || (channels && channels != frame.channels)) {
            malformed("copied Dolby format changes or disagrees with its clock");
        }
        const auto entry = enhanced ? eac3_sample_entry(frame) : ac3_sample_entry(frame);
        const auto expected = mp4_audio_private_box(entry, enhanced ? "dec3" : "dac3");
        bool configuration_matches = configuration == expected;
        if (enhanced && configuration.size() == eac3_configuration_bytes) {
            // dec3 data_rate is in integer kbit/s; the native Matroska writer
            // truncates, while MOV may round up. 44.1-kHz frame packing also
            // alternates sizes. Allow one kbit/s of rounding, not layout drift.
            const auto declared_kbps =
                (static_cast<unsigned>(configuration[0]) << 5) | (configuration[1] >> 3);
            const auto frame_kbps =
                static_cast<unsigned>(static_cast<std::uint64_t>(frame.frame_bytes) * 8 *
                                      frame.sample_rate / frame.samples / 1000);
            const auto difference = declared_kbps > frame_kbps ? declared_kbps - frame_kbps
                                                               : frame_kbps - declared_kbps;
            configuration_matches =
                declared_kbps != 0 && difference <= 1 && (configuration[1] & 7) == 0 &&
                std::equal(configuration.begin() + 2, configuration.end(), expected.begin() + 2);
        }
        if (!configuration_matches) {
            malformed("copied Dolby private configuration disagrees with its bitstream");
        }
        channels = frame.channels;
    }
    return channels;
}
} // namespace send_airplay2::detail
