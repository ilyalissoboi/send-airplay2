// SPDX-License-Identifier: Apache-2.0
#include "media_track_inventory.h"
#include "mkv_demux.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000;
constexpr std::uint64_t max_probe_read_bytes = 128ULL << 20;
constexpr std::size_t max_probe_read_calls = 1'048'576;
constexpr std::size_t max_callback_capacity = 64 * 1024;

[[noreturn]] void unsupported_time() {
    throw RemuxException(RemuxFailure::unsupported, "source audio timeline is not representable");
}
std::uint64_t checked_add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        unsupported_time();
    }
    return left + right;
}
/// Floor conversion without multiplying the possibly large whole timestamp.
std::uint64_t scale_floor(std::uint64_t ticks, std::uint32_t from, std::uint32_t to) {
    if (!from || !to) {
        unsupported_time();
    }
    const auto whole = ticks / from;
    if (whole > std::numeric_limits<std::uint64_t>::max() / to) {
        unsupported_time();
    }
    return checked_add(whole * to, (ticks % from) * to / from);
}

/// Floor of elapsed audio ticks after an empty edit, without rounding that edit
/// to nanoseconds first. Fraction cross-products fit uint64_t (uint32 clocks).
std::uint64_t elapsed_audio_ticks(std::uint64_t presentation_ns, std::uint64_t empty_movie_ticks,
                                  std::uint32_t movie_scale, std::uint32_t audio_scale) {
    const auto presentation_ticks =
        scale_floor(presentation_ns, nanoseconds_per_second, audio_scale);
    if (!empty_movie_ticks) {
        return presentation_ticks;
    }
    const auto empty_ticks = scale_floor(empty_movie_ticks, movie_scale, audio_scale);
    if (presentation_ticks < empty_ticks) {
        unsupported_time();
    }
    auto elapsed = presentation_ticks - empty_ticks;
    const auto presentation_fraction =
        (presentation_ns % nanoseconds_per_second) * audio_scale % nanoseconds_per_second;
    const auto empty_fraction = (empty_movie_ticks % movie_scale) * audio_scale % movie_scale;
    if (presentation_fraction * movie_scale < empty_fraction * nanoseconds_per_second) {
        if (!elapsed) {
            unsupported_time();
        }
        --elapsed;
    }
    return elapsed;
}
} // namespace

void validate_source_tracks(const MediaTrackInventory& inventory) {
    if (inventory.tracks.size() > max_source_tracks) {
        throw RemuxException(RemuxFailure::too_large, "source has more than 128 tracks");
    }
    for (std::size_t index = 0; index < inventory.tracks.size(); ++index) {
        if (!inventory.tracks[index].id) {
            throw RemuxException(RemuxFailure::malformed, "source track identity is zero");
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (inventory.tracks[earlier].id == inventory.tracks[index].id) {
                throw RemuxException(RemuxFailure::malformed, "duplicate source track identity");
            }
        }
    }
}

AudioSelection select_source_audio(const MediaTrackInventory& inventory,
                                   std::optional<std::uint64_t> explicit_id) {
    validate_source_tracks(inventory);
    std::size_t enabled_count = 0;
    std::size_t default_count = 0;
    std::size_t sole = 0;
    std::size_t default_index = 0;
    for (std::size_t index = 0; index < inventory.tracks.size(); ++index) {
        const auto& track = inventory.tracks[index];
        if (explicit_id && track.id == *explicit_id) {
            return {track.enabled && track.kind == SourceTrackKind::audio
                        ? AudioSelectionStatus::selected
                        : AudioSelectionStatus::invalid_selection,
                    index};
        }
        if (track.enabled && track.kind == SourceTrackKind::audio) {
            ++enabled_count;
            sole = index;
            if (track.default_track) {
                ++default_count;
                default_index = index;
            }
        }
    }
    if (explicit_id) {
        return {AudioSelectionStatus::invalid_selection, 0};
    }
    if (!enabled_count) {
        return {AudioSelectionStatus::video_only, 0};
    }
    if (enabled_count == 1) {
        return {AudioSelectionStatus::selected, sole};
    }
    if (inventory.container == SourceContainer::matroska && default_count == 1) {
        return {AudioSelectionStatus::selected, default_index};
    }
    return {AudioSelectionStatus::selection_required, 0};
}

MediaTrackInventory probe_media_tracks(const MediaSource& input, const MediaReadContext& context) {
    if (!input.size || !input.read_at) {
        throw std::invalid_argument("source probe requires size and read callbacks");
    }
    const auto check_stop = [&] {
        if (context.should_stop()) {
            throw std::runtime_error("source probe cancelled or expired");
        }
    };
    check_stop();
    const auto file_size = input.size();
    check_stop();
    std::uint64_t requested_bytes = 0;
    std::size_t read_calls = 0;
    const RandomReader read = [&](std::uint64_t offset, std::uint8_t* output, std::size_t length) {
        check_stop();
        if (offset > file_size || length > file_size - offset) {
            throw RemuxException(RemuxFailure::malformed, "source probe read outside file");
        }
        if (length > max_probe_read_bytes - requested_bytes) {
            throw RemuxException(RemuxFailure::too_large, "source probe read budget exceeded");
        }
        requested_bytes += length;
        while (length) {
            check_stop();
            if (++read_calls > max_probe_read_calls) {
                throw RemuxException(RemuxFailure::too_large,
                                     "source probe callback limit exceeded");
            }
            const auto wanted = std::min(length, max_callback_capacity);
            const auto count = input.read_at(offset, output, wanted, context);
            check_stop();
            if (!count || count > wanted) {
                throw std::runtime_error("source probe read failed");
            }
            offset += count;
            output += count;
            length -= count;
        }
    };
    std::array<std::uint8_t, 4> signature{};
    if (file_size < signature.size()) {
        throw RemuxException(RemuxFailure::malformed, "source probe input is truncated");
    }
    read(0, signature.data(), signature.size());
    auto inventory = starts_like_matroska(signature.data(), signature.size())
                         ? probe_mkv_tracks(read, file_size)
                         : probe_mp4_tracks(read, file_size);
    check_stop();
    validate_source_tracks(inventory);
    return inventory;
}

void validate_mp4_audio_index(const Mp4Track& audio) {
    if (audio.kind != TrackKind::audio || audio.samples.empty()) {
        throw std::invalid_argument("audio seek requires an audio sample index");
    }
    for (std::size_t index = 0; index < audio.samples.size(); ++index) {
        if (audio.samples[index].composition_offset) {
            unsupported_time();
        }
        if (!audio.samples[index].duration ||
            (index &&
             audio.samples[index].decode_time != checked_add(audio.samples[index - 1].decode_time,
                                                             audio.samples[index - 1].duration))) {
            throw std::invalid_argument("audio sample decode times are not contiguous");
        }
    }
    if (!audio.timescale) {
        unsupported_time();
    }
}

Mp4AudioSeek plan_mp4_audio_seek(const Mp4Track& audio, std::uint32_t movie_timescale,
                                 std::uint64_t presentation_ns,
                                 std::uint64_t decoder_preroll_ticks) {
    if (audio.kind != TrackKind::audio || audio.samples.empty()) {
        throw std::invalid_argument("audio seek requires an audio sample index");
    }
    std::uint64_t empty_movie_ticks = 0;
    std::uint64_t media_start = 0;
    std::optional<std::uint64_t> edit_duration_movie_ticks;
    if (!audio.edits.empty()) {
        const auto& media = audio.edits.back();
        if (audio.edits.size() > 2 || media.media_time < 0 || media.rate_integer != 1 ||
            media.rate_fraction != 0) {
            unsupported_time();
        }
        if (audio.edits.size() == 2) {
            const auto& empty = audio.edits.front();
            if (empty.media_time != -1 || empty.rate_integer != 1 || empty.rate_fraction != 0) {
                unsupported_time();
            }
            empty_movie_ticks = empty.segment_duration;
        }
        media_start = static_cast<std::uint64_t>(media.media_time);
        edit_duration_movie_ticks = media.segment_duration;
    }
    Mp4AudioSeek result;
    const auto presentation_movie_ticks =
        audio.edits.empty() ? 0
                            : scale_floor(presentation_ns, nanoseconds_per_second, movie_timescale);
    result.initial_silence = presentation_movie_ticks < empty_movie_ticks;
    const auto relative_ticks = result.initial_silence
                                    ? 0
                                    : elapsed_audio_ticks(presentation_ns, empty_movie_ticks,
                                                          movie_timescale, audio.timescale);
    result.discard_before_ticks = checked_add(media_start, relative_ticks);
    const auto end = checked_add(audio.samples.back().decode_time, audio.samples.back().duration);
    if (result.discard_before_ticks >= end ||
        (edit_duration_movie_ticks && !result.initial_silence &&
         presentation_movie_ticks - empty_movie_ticks >= *edit_duration_movie_ticks)) {
        result.sample_index = audio.samples.size();
        return result;
    }
    const auto target = result.discard_before_ticks > decoder_preroll_ticks
                            ? result.discard_before_ticks - decoder_preroll_ticks
                            : 0;
    const auto after = std::upper_bound(
        audio.samples.begin(), audio.samples.end(), target,
        [](std::uint64_t ticks, const Mp4Sample& sample) { return ticks < sample.decode_time; });
    result.sample_index = after == audio.samples.begin()
                              ? 0
                              : static_cast<std::size_t>(after - audio.samples.begin() - 1);
    return result;
}

MkvAudioSeekTime plan_mkv_audio_seek_time(const SourceTrack& audio, std::uint64_t presentation_ns,
                                          std::uint64_t decoder_preroll_ns) {
    if (audio.kind != SourceTrackKind::audio) {
        throw std::invalid_argument("Matroska audio seek requires an audio track");
    }
    if (!audio.standard_timeline) {
        unsupported_time();
    }
    MkvAudioSeekTime result;
    result.discard_before_timestamp_ns = checked_add(presentation_ns, audio.codec_delay_ns);
    const auto preroll = std::max(audio.seek_preroll_ns, decoder_preroll_ns);
    result.seek_timestamp_ns = result.discard_before_timestamp_ns > preroll
                                   ? result.discard_before_timestamp_ns - preroll
                                   : 0;
    return result;
}
} // namespace send_airplay2::detail
