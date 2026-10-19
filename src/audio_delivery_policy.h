// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_AUDIO_DELIVERY_POLICY_H
#define SEND_AIRPLAY2_AUDIO_DELIVERY_POLICY_H
#include "media_track_inventory.h"
#include <cstdint>
#include <optional>

namespace send_airplay2::detail {
enum class AudioDeliveryProfile { apple_hls_v1, apple_audio_v1 };
enum class AudioDeliveryAction { video_only, copy, stereo_aac, progressive };
enum class AudioDeliveryReason {
    no_enabled_audio,
    profile_copy,
    profile_requires_stereo,
    profile_progressive
};
enum class SourceAudioFamily { none, aac, ac3, eac3, dts, flac, opus, vorbis, pcm, truehd, mp3 };
/** Metadata plan, not decoder or receiver proof. action/id are meaningful only
 * for selected/video_only selection status. Copy candidates must also pass the
 * selected native index and executable payload/configuration table below.
 */
struct AudioDeliveryPlan {
    AudioDeliveryProfile profile = AudioDeliveryProfile::apple_hls_v1;
    AudioSelectionStatus selection = AudioSelectionStatus::video_only;
    AudioDeliveryAction action = AudioDeliveryAction::video_only;
    AudioDeliveryReason reason = AudioDeliveryReason::no_enabled_audio;
    SourceAudioFamily source_family = SourceAudioFamily::none;
    SourceAudioFamily output_family = SourceAudioFamily::none;
    std::uint64_t source_id = 0;
    std::uint32_t output_channels = 0;
};
/// Select before codec eligibility; never replace the selected original identity.
/// RemuxException reports malformed/unsupported metadata or unknown profile.
[[nodiscard]] AudioDeliveryPlan plan_audio_delivery(const MediaTrackInventory& inventory,
                                                    AudioDeliveryProfile profile,
                                                    std::optional<std::uint64_t> selected_id = {});
/** Validate selected copied audio before resources are prepared. Reads only
 * bounded Dolby headers (all units), not AAC payloads or decoded PCM. The caller
 * owns immutable source, byte/callback/deadline/cancellation limits. No fallback
 * follows any failure. Returns the authoritative output channel count.
 * AAC-LC metadata validation cannot prove arbitrary compressed payload integrity.
 */
[[nodiscard]] std::uint32_t validate_profile_copy_audio(const Mp4Track& audio,
                                                        const RandomReader& read);
} // namespace send_airplay2::detail
#endif
