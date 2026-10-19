// SPDX-License-Identifier: Apache-2.0
#include "prepared_stereo_media.h"
#include "media_track_inventory.h"
#ifdef SAP2_HAS_STEREO_AAC
#include "audio_decoder_source.h"
#endif
#include <utility>
#include <new>
#include <chrono>

namespace send_airplay2::detail {
namespace {
class CacheCleanup {
public:
    explicit CacheCleanup(AacFragmentStore store) noexcept : store_(std::move(store)) {}
    ~CacheCleanup() {
        if (store_.clear) {
            try {
                store_.clear();
            } catch (...) {
            }
        }
    }
    CacheCleanup(const CacheCleanup&) = delete;
    CacheCleanup& operator=(const CacheCleanup&) = delete;
    CacheCleanup(CacheCleanup&&) = delete;
    CacheCleanup& operator=(CacheCleanup&&) = delete;
    AacFragmentStore release() noexcept {
        return std::exchange(store_, {});
    }

private:
    AacFragmentStore store_;
};
#ifdef SAP2_HAS_STEREO_AAC
class ConvertedMedia final : public PreparedStereoMedia {
public:
    ConvertedMedia(std::unique_ptr<StereoAacHls> media, std::uint64_t id) noexcept
        : media_(std::move(media)), id_(id) {}
    const RemuxedHls& presentation() const noexcept override {
        return media_->presentation();
    }
    StereoAacHlsFailure failure() const noexcept override {
        return media_->failure();
    }
    std::uint64_t selected_audio_id() const noexcept override {
        return id_;
    }
    std::uint32_t authoring_flags() const noexcept override {
        const auto& provenance = media_->authoring();
        return authoring_audio_estimated |
               (provenance.copied_sizes_estimated ? authoring_copied_sizes_estimated : 0U) |
               (provenance.subtitle_sizes_unmeasured ? authoring_subtitle_sizes_unmeasured : 0U);
    }
    void stop() noexcept override {
        media_->stop();
    }

private:
    std::unique_ptr<StereoAacHls> media_;
    std::uint64_t id_;
};
class VideoOnlyMedia final : public PreparedStereoMedia {
public:
    explicit VideoOnlyMedia(RemuxedHls media) noexcept : media_(std::move(media)) {}
    ~VideoOnlyMedia() override {
        stop();
    }
    void take_store(AacFragmentStore store) noexcept {
        store_ = std::move(store);
    }
    const RemuxedHls& presentation() const noexcept override {
        return media_;
    }
    StereoAacHlsFailure failure() const noexcept override {
        return StereoAacHlsFailure::none;
    }
    std::uint64_t selected_audio_id() const noexcept override {
        return 0;
    }
    std::uint32_t authoring_flags() const noexcept override {
        return media_.segment_sizes_estimated ? authoring_copied_sizes_estimated : 0U;
    }
    void stop() noexcept override {
        media_.resources.clear();
        if (store_.clear) {
            try {
                store_.clear();
            } catch (...) {
            }
        }
        store_ = {};
    }

private:
    RemuxedHls media_;
    AacFragmentStore store_;
};
MediaPreparationFailure preparation_failure(AudioDecodeFailure reason) noexcept {
    switch (reason) {
    case AudioDecodeFailure::invalid_selection:
        return MediaPreparationFailure::invalid_selection;
    case AudioDecodeFailure::selection_required:
        return MediaPreparationFailure::selection_required;
    case AudioDecodeFailure::unsupported:
        return MediaPreparationFailure::unsupported;
    case AudioDecodeFailure::malformed:
        return MediaPreparationFailure::malformed;
    case AudioDecodeFailure::too_large:
        return MediaPreparationFailure::resource_limit;
    case AudioDecodeFailure::stopped:
        return MediaPreparationFailure::cancelled;
    case AudioDecodeFailure::source_error:
        return MediaPreparationFailure::source_error;
    case AudioDecodeFailure::codec_error:
        return MediaPreparationFailure::codec_error;
    }
    return MediaPreparationFailure::codec_error;
}
#endif
} // namespace
bool stereo_aac_available() noexcept {
#ifdef SAP2_HAS_STEREO_AAC
    return true;
#else
    return false;
#endif
}
#ifdef SAP2_HAS_STEREO_AAC
namespace {
std::unique_ptr<PreparedStereoMedia>
prepare_stereo_media_impl(MediaSource source, AacFragmentStore store, std::uint64_t quota,
                          const MediaReadContext& context, std::optional<std::uint64_t> selected_id,
                          bool audio_only) {
    CacheCleanup cleanup(std::move(store));
    try {
        if (audio_only) {
            auto media = StereoAacHls::open_audio_only(std::move(source), cleanup.release(), quota,
                                                       context, selected_id);
            return std::make_unique<ConvertedMedia>(std::move(media), selected_id.value());
        }
        const auto inventory = probe_media_tracks(source, context);
        const auto selection = select_source_audio(inventory, selected_id);
        if (selection.status == AudioSelectionStatus::selection_required) {
            throw MediaPreparationException(MediaPreparationFailure::selection_required);
        }
        if (selection.status == AudioSelectionStatus::invalid_selection) {
            throw MediaPreparationException(MediaPreparationFailure::invalid_selection);
        }
        if (selection.status == AudioSelectionStatus::video_only) {
            auto presentation = remux_to_hls(source, context.request_cancelled, true, &context);
            if (!presentation.zero_origin_video) {
                throw MediaPreparationException(MediaPreparationFailure::unsupported);
            }
            validate_stereo_video_spans(presentation.durations_us);
            auto media = std::make_unique<VideoOnlyMedia>(std::move(presentation));
            media->take_store(cleanup.release());
            return media;
        }
        const auto id = inventory.tracks.at(selection.track_index).id;
        auto media = StereoAacHls::open(std::move(source), cleanup.release(), quota, context, id);
        return std::make_unique<ConvertedMedia>(std::move(media), id);
    } catch (const AudioDecodeException& error) {
        const auto reason = error.reason() == AudioDecodeFailure::stopped &&
                                    std::chrono::steady_clock::now() >= context.deadline
                                ? MediaPreparationFailure::timeout
                                : preparation_failure(error.reason());
        throw MediaPreparationException(reason);
    } catch (const MediaPreparationException&) {
        throw;
    } catch (const RemuxException& error) {
        const auto reason =
            error.reason() == RemuxFailure::too_large   ? MediaPreparationFailure::resource_limit
            : error.reason() == RemuxFailure::malformed ? MediaPreparationFailure::malformed
                                                        : MediaPreparationFailure::unsupported;
        throw MediaPreparationException(reason);
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        throw MediaPreparationException(context.should_stop()
                                            ? (std::chrono::steady_clock::now() >= context.deadline
                                                   ? MediaPreparationFailure::timeout
                                                   : MediaPreparationFailure::cancelled)
                                            : MediaPreparationFailure::source_error);
    }
}
} // namespace
#endif
std::unique_ptr<PreparedStereoMedia>
prepare_stereo_media(MediaSource source, AacFragmentStore store, std::uint64_t quota,
                     const MediaReadContext& context, std::optional<std::uint64_t> selected_id) {
#ifdef SAP2_HAS_STEREO_AAC
    return prepare_stereo_media_impl(std::move(source), std::move(store), quota, context,
                                     selected_id, false);
#else
    CacheCleanup cleanup(std::move(store));
    (void)source;
    (void)quota;
    (void)context;
    (void)selected_id;
    throw MediaPreparationException(MediaPreparationFailure::backend_unavailable);
#endif
}
std::unique_ptr<PreparedStereoMedia>
prepare_audio_only_media(MediaSource source, AacFragmentStore store, std::uint64_t quota,
                         const MediaReadContext& context, std::uint64_t selected_id) {
#ifdef SAP2_HAS_STEREO_AAC
    return prepare_stereo_media_impl(std::move(source), std::move(store), quota, context,
                                     selected_id, true);
#else
    CacheCleanup cleanup(std::move(store));
    (void)source;
    (void)quota;
    (void)context;
    (void)selected_id;
    throw MediaPreparationException(MediaPreparationFailure::backend_unavailable);
#endif
}
} // namespace send_airplay2::detail
