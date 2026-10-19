// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_PREPARED_AUTOMATIC_MEDIA_H
#define SEND_AIRPLAY2_PREPARED_AUTOMATIC_MEDIA_H
#include "audio_delivery_policy.h"
#include "prepared_stereo_media.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace send_airplay2::detail {
enum class AudioConversionPermission { allow_stereo, preserve_original };
/** Private owner underlying public automatic audio delivery and offline checks.
 * decision/failure/authoring_flags are thread-safe and perform no source/cache I/O.
 * Presentation resource access is serialized with Stop and media-server drain.
 * Stop follows media-server drain and is idempotent. Retained resource copies
 * must finish before borrowed host source/cache storage is released.
 */
class PreparedAutomaticMedia {
public:
    ~PreparedAutomaticMedia();
    PreparedAutomaticMedia(const PreparedAutomaticMedia&) = delete;
    PreparedAutomaticMedia& operator=(const PreparedAutomaticMedia&) = delete;
    PreparedAutomaticMedia(PreparedAutomaticMedia&&) = delete;
    PreparedAutomaticMedia& operator=(PreparedAutomaticMedia&&) = delete;
    [[nodiscard]] const AudioDeliveryPlan& decision() const noexcept;
    [[nodiscard]] const RemuxedHls& presentation() const noexcept;
    [[nodiscard]] StereoAacHlsFailure failure() const noexcept;
    [[nodiscard]] std::uint32_t authoring_flags() const noexcept;
    void stop() noexcept;
    /** Select/resolve before resource registration. Copy/video-only/progressive never call
     * or take ownership of store, and work in codec-free builds. Conversion
     * requires an enabled backend and complete per-cast store/quota; ownership
     * transfers only when entering the existing stereo preparation function.
     * No retry after source/configuration/cache/codec/timeout/cancellation failure.
     * apple_audio_v1 admits no video and only the finite audio-only subset.
     * Its progressive presentation is one original MIME-typed resource; the
     * presentation's playlist_name is the entry resource, not an HLS assertion.
     * Source remains immutable; operations borrow context until return only.
     */
    [[nodiscard]] static std::unique_ptr<PreparedAutomaticMedia>
    open(MediaSource source, AacFragmentStore store, std::uint64_t quota,
         const MediaReadContext& context, AudioDeliveryProfile profile,
         AudioConversionPermission permission, std::optional<std::uint64_t> selected_id = {});

private:
    PreparedAutomaticMedia() = default;
    AudioDeliveryPlan decision_;
    RemuxedHls copied_;
    std::unique_ptr<PreparedStereoMedia> converted_;
};
} // namespace send_airplay2::detail
#endif
