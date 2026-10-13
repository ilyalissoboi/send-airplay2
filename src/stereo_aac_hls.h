// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_STEREO_AAC_HLS_H
#define SEND_AIRPLAY2_STEREO_AAC_HLS_H
#include "hls_remux.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace send_airplay2::detail {
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
 * Call stop after stopping the MediaServer. stop is idempotent, cancels/joins work,
 * waits for cache readers, clears storage and releases source/resource callbacks.
 * Retained audio-resource copies become inert. Retained video copies retain their
 * source as in remux_to_hls, so the host must release those before releasing storage.
 * Callbacks must cooperate and must not reenter stop; C++ cannot interrupt them.
 * No caller may concurrently mutate presentation() or stop/destroy this object.
 */
class StereoAacHls {
public:
    [[nodiscard]] static std::unique_ptr<StereoAacHls>
    open(MediaSource source, AacFragmentStore store, std::uint64_t quota_bytes,
         const MediaReadContext& context, std::optional<std::uint64_t> selected_id = {});
    ~StereoAacHls();
    StereoAacHls(const StereoAacHls&) = delete;
    StereoAacHls& operator=(const StereoAacHls&) = delete;
    StereoAacHls(StereoAacHls&&) = delete;
    StereoAacHls& operator=(StereoAacHls&&) = delete;
    [[nodiscard]] const RemuxedHls& presentation() const noexcept;
    void stop() noexcept;

private:
    struct Impl;
    explicit StereoAacHls(std::shared_ptr<Impl> impl, RemuxedHls presentation);
    std::shared_ptr<Impl> impl_;
    RemuxedHls presentation_;
};
} // namespace send_airplay2::detail
#endif
