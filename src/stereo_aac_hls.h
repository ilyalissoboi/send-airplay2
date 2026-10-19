// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_STEREO_AAC_HLS_H
#define SEND_AIRPLAY2_STEREO_AAC_HLS_H
#include "hls_remux.h"
#include "hls_authoring.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace send_airplay2::detail {
/// First terminal failure, independent of request cancellation or explicit Stop.
/// Fixed categories only; no host exception text, paths or receiver details.
enum class StereoAacHlsFailure {
    none,
    unsupported,
    malformed,
    source_error,
    codec_error,
    resource_limit,
    cache_error,
    generation_timeout
};
/// Immutable startup provenance; measured bytes do not imply whole-VOD accuracy.
struct StereoAacHlsAuthoring {
    HlsBitRates copied_rates;
    HlsBitRates representative_audio_rates;
    std::uint64_t representative_audio_bytes = 0;
    std::uint64_t representative_audio_duration_us = 0;
    bool copied_sizes_estimated = false;
    bool subtitle_sizes_unmeasured = false;
};
/** Plan sample-clock audio cuts from absolute video boundaries at 48 kHz.
 * Preserve usable snapped video cuts; split long gaps on the absolute six-second
 * grid. All nonfinal cuts lie on AAC frames, final end is exact, each span <=7 s.
 * Requires increasing boundaries from zero, <=7 days and <=100802 boundaries.
 * Throws AudioDecodeException for invalid bounds; no PCM/source I/O is performed.
 */
[[nodiscard]] std::vector<std::uint64_t>
plan_stereo_aac_boundaries(const std::vector<std::uint64_t>& video_boundaries);
/// Borrowed during a cache callback; combines cast stop with request cancellation.
struct AacCacheContext {
    const MediaReadContext& request;
    const std::atomic_bool& cast_stopped;
    [[nodiscard]] bool should_stop() const noexcept {
        return cast_stopped.load(std::memory_order_relaxed) || request.should_stop();
    }
};
/** Per-cast host-owned storage. seal atomically consumes the complete fragment,
 * never retaining its bytes/context; failure must not publish partial bytes.
 * read_at is thread-safe, polls should_stop, and returns short reads like MediaSource.
 * clear is nonthrowing, deletes only this cast's objects and runs once after all
 * callbacks finish. Storage stays immutable until clear; no eviction. No paths,
 * filesystem access or full-movie PCM cache are chosen by the native builder.
 */
struct AacFragmentStore {
    std::function<void(std::size_t, const Bytes&, const AacCacheContext&)> seal;
    std::function<std::size_t(std::size_t, std::uint64_t, std::uint8_t*, std::size_t,
                              const AacCacheContext&)>
        read_at;
    std::function<void()> clear;
};
/** Optional private HLS composition, not a public ABI/delivery mode. Owns resources,
 * decoder and one worker. Startup borrows one absolute context; all later reads
 * borrow their request contexts. Same-segment requests coalesce; cancelling one
 * waiter does not cancel generation. Four pending jobs, <=10 s PCM per epoch,
 * <=1 MiB encoded epoch and a mandatory per-cast sealed-byte quota.
 * Conversion/seal/read failures are terminal: all generated audio fragment requests fail,
 * generation cancels, and failure() retains the first fixed category. The host must stop its media
 * server/playback on that signal; video/playlist callbacks keep their remux lifetime contract.
 * Request cancellation, admission refusal and explicit Stop do not report terminal failure. Failed
 * storage is released by Stop, not callbacks. Call stop after stopping the MediaServer. stop is
 * idempotent, cancels/joins work, waits for cache readers, clears storage and releases
 * source/resource callbacks. Retained audio-resource copies become inert. Retained video copies
 * retain their source as in remux_to_hls, so the host must release those before releasing storage.
 * Callbacks must cooperate and must not reenter stop; C++ cannot interrupt them.
 * No caller may concurrently mutate presentation() or stop/destroy this object.
 * Matroska composition uses the first video timestamp as a shared origin:
 * video/text are rebased, source audio windows are offset then relabeled on
 * that same presentation clock. Earlier audio is clipped; later audio keeps
 * initial silence. Source origin plus movie span must fit the seven-day bound.
 */
class StereoAacHls {
public:
    [[nodiscard]] static std::unique_ptr<StereoAacHls>
    open(MediaSource source, AacFragmentStore store, std::uint64_t quota_bytes,
         const MediaReadContext& context, std::optional<std::uint64_t> selected_id = {});
    /** Private audio-only foundation; does not change public automatic routing.
     * Admits unedited progressive MP4/MOV or the bounded standalone FLAC/WAVE
     * subset, without video and with a selected supported FLAC/PCM stream. Native
     * packet index determines duration, not a host-supplied or FFmpeg estimate.
     * Uses the existing stereo encoder, lazy cache, deadlines and Stop contract.
     * Returns one direct audio variant, no video resources or rendition group.
     * Standalone metadata is validated against a bounded decoded end/EOF window;
     * shorter windows fail rather than padding a truncated declared presentation.
     * Ogg/MP3, edited MP4 and Matroska audio-only timelines remain separate.
     * Store cleanup is consumed on every call, including startup failure.
     */
    [[nodiscard]] static std::unique_ptr<StereoAacHls>
    open_audio_only(MediaSource source, AacFragmentStore store, std::uint64_t quota_bytes,
                    const MediaReadContext& context, std::optional<std::uint64_t> selected_id = {});
    ~StereoAacHls();
    StereoAacHls(const StereoAacHls&) = delete;
    StereoAacHls& operator=(const StereoAacHls&) = delete;
    StereoAacHls(StereoAacHls&&) = delete;
    StereoAacHls& operator=(StereoAacHls&&) = delete;
    [[nodiscard]] const RemuxedHls& presentation() const noexcept;
    /// Audio count may exceed the video's segment_count for long GOPs.
    [[nodiscard]] std::size_t audio_segment_count() const noexcept;
    /// Thread-safe while this owner is alive; first failure persists after Stop.
    [[nodiscard]] StereoAacHlsFailure failure() const noexcept;
    /// No I/O or generation; startup estimates/provenance persist after Stop.
    [[nodiscard]] const StereoAacHlsAuthoring& authoring() const noexcept;
    void stop() noexcept;

private:
    [[nodiscard]] static std::unique_ptr<StereoAacHls>
    open_impl(MediaSource source, AacFragmentStore store, std::uint64_t quota_bytes,
              const MediaReadContext& context, std::optional<std::uint64_t> selected_id,
              bool audio_only);
    struct Impl;
    explicit StereoAacHls(std::shared_ptr<Impl> impl, RemuxedHls presentation);
    std::shared_ptr<Impl> impl_;
    RemuxedHls presentation_;
};
} // namespace send_airplay2::detail
#endif
