// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_PREPARED_STEREO_MEDIA_H
#define SEND_AIRPLAY2_PREPARED_STEREO_MEDIA_H
#include "stereo_aac_hls.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

namespace send_airplay2::detail {
inline constexpr std::uint32_t authoring_audio_estimated = 1U;
inline constexpr std::uint32_t authoring_copied_sizes_estimated = 2U;
inline constexpr std::uint32_t authoring_subtitle_sizes_unmeasured = 4U;
enum class MediaPreparationFailure {
    backend_unavailable,
    unsupported,
    malformed,
    selection_required,
    invalid_selection,
    source_error,
    codec_error,
    resource_limit,
    cache_error,
    timeout,
    cancelled
};
class MediaPreparationException : public std::runtime_error {
public:
    explicit MediaPreparationException(MediaPreparationFailure reason)
        : std::runtime_error("media preparation failed"), reason_(reason) {}
    [[nodiscard]] MediaPreparationFailure reason() const noexcept {
        return reason_;
    }

private:
    MediaPreparationFailure reason_;
};
/** Controller-owned prepared resources and conversion worker. Methods perform
 * no I/O except stop; failure/metadata are thread-safe while this owner lives.
 * Stop follows media-server drain and is idempotent. Retained resource copies
 * are owned by the server and must finish before host source/cache release.
 */
class PreparedStereoMedia {
public:
    virtual ~PreparedStereoMedia() = default;
    PreparedStereoMedia() = default;
    PreparedStereoMedia(const PreparedStereoMedia&) = delete;
    PreparedStereoMedia& operator=(const PreparedStereoMedia&) = delete;
    PreparedStereoMedia(PreparedStereoMedia&&) = delete;
    PreparedStereoMedia& operator=(PreparedStereoMedia&&) = delete;
    [[nodiscard]] virtual const RemuxedHls& presentation() const noexcept = 0;
    [[nodiscard]] virtual StereoAacHlsFailure failure() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t selected_audio_id() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t authoring_flags() const noexcept = 0;
    virtual void stop() noexcept = 0;
};
[[nodiscard]] bool stereo_aac_available() noexcept;
/// Takes per-cast store ownership on entry, including startup refusal/cleanup.
/// Auto selection without enabled audio produces copied video only, no silent track.
[[nodiscard]] std::unique_ptr<PreparedStereoMedia>
prepare_stereo_media(MediaSource source, AacFragmentStore store, std::uint64_t quota,
                     const MediaReadContext& context, std::optional<std::uint64_t> selected_id);
} // namespace send_airplay2::detail
#endif
