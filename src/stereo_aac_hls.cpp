// SPDX-License-Identifier: Apache-2.0
#include "stereo_aac_hls.h"
#include "audio_decoder_source.h"
#include "media_track_inventory.h"
#include "sample_entries.h"
#include "stereo_aac_encoder.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t sample_rate = 48000;
constexpr std::uint64_t microseconds_per_second = 1'000'000;
constexpr std::uint64_t preroll_samples = 48 * stereo_aac_frame_samples;
constexpr std::uint64_t maximum_segment_samples = 7 * sample_rate;
constexpr std::uint64_t maximum_movie_samples = 7 * 24 * 3600 * sample_rate;
constexpr std::uint64_t target_segment_samples = default_segment_target_seconds * sample_rate;
// One week of nominal six-second segments, plus the zero/final boundaries.
constexpr std::size_t maximum_video_boundaries = maximum_movie_samples / target_segment_samples + 2;
constexpr std::size_t maximum_pending_jobs = 4;
constexpr auto generation_deadline = std::chrono::seconds(30);
constexpr auto waiter_poll = std::chrono::milliseconds(10);
// AAC ABR plus MP4 overhead: conservative admission estimate, not measured HLS bandwidth.
constexpr std::uint64_t estimated_bytes_per_second = stereo_aac_bit_rate / 8 * 2;

void require(bool condition, AudioDecodeFailure reason, const char* message) {
    if (!condition) {
        throw AudioDecodeException(reason, message);
    }
}
void check_request(const MediaReadContext& context) {
    require(context.server_stopped && context.request_cancelled, AudioDecodeFailure::source_error,
            "invalid AAC HLS request context");
    require(!context.should_stop(), AudioDecodeFailure::stopped, "AAC HLS request stopped");
}
StereoAacHlsFailure decode_failure(AudioDecodeFailure reason) noexcept {
    switch (reason) {
    case AudioDecodeFailure::unsupported:
        return StereoAacHlsFailure::unsupported;
    case AudioDecodeFailure::malformed:
        return StereoAacHlsFailure::malformed;
    case AudioDecodeFailure::too_large:
        return StereoAacHlsFailure::resource_limit;
    case AudioDecodeFailure::codec_error:
        return StereoAacHlsFailure::codec_error;
    default:
        return StereoAacHlsFailure::source_error;
    }
}
/// Fixed descriptions prevent callback paths or other host details escaping.
void throw_failure(StereoAacHlsFailure failure) {
    switch (failure) {
    case StereoAacHlsFailure::none:
        return;
    case StereoAacHlsFailure::unsupported:
        throw AudioDecodeException(AudioDecodeFailure::unsupported, "AAC conversion unsupported");
    case StereoAacHlsFailure::malformed:
        throw AudioDecodeException(AudioDecodeFailure::malformed, "AAC conversion malformed input");
    case StereoAacHlsFailure::codec_error:
        throw AudioDecodeException(AudioDecodeFailure::codec_error, "AAC conversion codec failure");
    case StereoAacHlsFailure::resource_limit:
        throw AudioDecodeException(AudioDecodeFailure::too_large, "AAC conversion resource limit");
    case StereoAacHlsFailure::generation_timeout:
        throw AudioDecodeException(AudioDecodeFailure::stopped, "AAC generation deadline expired");
    case StereoAacHlsFailure::cache_error:
        throw AudioDecodeException(AudioDecodeFailure::source_error, "AAC cache failure");
    case StereoAacHlsFailure::source_error:
        throw AudioDecodeException(AudioDecodeFailure::source_error,
                                   "AAC conversion source failure");
    }
}
/// Arm host cleanup before context validation, allocations or source callbacks.
/// Transfer is noexcept; exactly one of this guard or Impl owns clear afterward.
class StoreCleanup {
public:
    explicit StoreCleanup(AacFragmentStore store) noexcept : store_(std::move(store)) {}
    ~StoreCleanup() {
        if (store_.clear) {
            try {
                store_.clear();
            } catch (...) { /* Nonthrowing host cleanup contract. */
            }
        }
    }
    StoreCleanup(const StoreCleanup&) = delete;
    StoreCleanup& operator=(const StoreCleanup&) = delete;
    StoreCleanup(StoreCleanup&&) = delete;
    StoreCleanup& operator=(StoreCleanup&&) = delete;
    AacFragmentStore release() noexcept {
        return std::exchange(store_, {});
    }

private:
    AacFragmentStore store_;
};
std::string seconds_text(std::uint64_t samples) {
    const auto us = (samples * microseconds_per_second + sample_rate / 2) / sample_rate;
    auto fraction = std::to_string(us % microseconds_per_second);
    fraction.insert(0, 6 - fraction.size(), '0');
    return std::to_string(us / microseconds_per_second) + '.' + fraction;
}
MediaResource memory_resource(const std::string& name, const char* type, Bytes bytes) {
    const auto owned = std::make_shared<const Bytes>(std::move(bytes));
    return {name,
            type,
            {[owned] { return static_cast<std::uint64_t>(owned->size()); },
             [owned](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                     const MediaReadContext& context) {
                 if (context.should_stop() || offset >= owned->size()) {
                     return std::size_t{0};
                 }
                 const auto count = static_cast<std::size_t>(
                     std::min<std::uint64_t>(capacity, owned->size() - offset));
                 std::memcpy(output, owned->data() + offset, count);
                 return count;
             }},
            {}};
}
MediaResource text_resource(const std::string& name, const std::string& text) {
    return memory_resource(name, "application/vnd.apple.mpegurl", Bytes(text.begin(), text.end()));
}
Mp4Movie audio_movie(const Bytes& config, const std::string& language, std::uint64_t duration) {
    Mp4Movie movie;
    movie.timescale = static_cast<std::uint32_t>(sample_rate);
    Mp4Track track;
    track.kind = TrackKind::audio;
    track.timescale = movie.timescale;
    track.language = packed_language(language);
    track.codec = "mp4a";
    track.sample_entry = aac_sample_entry(2, movie.timescale, config);
    track.track_header = track_header_box(TrackKind::audio, 1, 0, 0);
    // Stored DTS is global audible PTS + 1024. The edit removes encoder priming
    // once and constrains the VOD end, including a partial final AAC frame.
    track.edits.push_back({duration, stereo_aac_frame_samples, 1, 0});
    movie.tracks.push_back(std::move(track));
    return movie;
}
Bytes fragment(Mp4Movie movie, const EncodedAacEpoch& encoded, std::size_t index,
               std::uint64_t begin, std::uint64_t end) {
    auto& track = movie.tracks.front();
    Bytes payload;
    const auto first_pts = index == 0 ? -static_cast<std::int64_t>(stereo_aac_frame_samples)
                                      : static_cast<std::int64_t>(begin);
    auto next_pts = first_pts;
    for (const auto& packet : encoded.packets) {
        if (packet.presentation_sample < first_pts ||
            packet.presentation_sample >= static_cast<std::int64_t>(end)) {
            continue;
        }
        require(packet.presentation_sample == next_pts, AudioDecodeFailure::codec_error,
                "AAC epoch has a packet gap");
        const auto stored_time =
            static_cast<std::uint64_t>(packet.presentation_sample + stereo_aac_frame_samples);
        const auto duration = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            stereo_aac_frame_samples, static_cast<std::uint64_t>(static_cast<std::int64_t>(end) -
                                                                 packet.presentation_sample)));
        track.samples.push_back(
            {0, stored_time, static_cast<std::uint32_t>(packet.bytes.size()), duration, 0, true});
        payload.insert(payload.end(), packet.bytes.begin(), packet.bytes.end());
        next_pts += duration;
    }
    require(next_pts == static_cast<std::int64_t>(end), AudioDecodeFailure::codec_error,
            "AAC epoch did not cover the requested segment");
    SegmentLayout layout{static_cast<std::uint32_t>(index + 1), {{0, track.samples.size()}}};
    auto bytes = write_segment_header(movie, layout);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}
} // namespace

std::vector<std::uint64_t> plan_stereo_aac_boundaries(const std::vector<std::uint64_t>& video) {
    require(video.size() >= 2 && video.size() <= maximum_video_boundaries && video.front() == 0 &&
                video.back() > 0 && video.back() <= maximum_movie_samples,
            AudioDecodeFailure::unsupported, "invalid AAC video boundary bounds");
    // Validate the entire input before splitting. A huge intermediate boundary
    // followed by a small end must not drive allocation before being refused.
    for (std::size_t index = 1; index < video.size(); ++index) {
        require(video[index] > video[index - 1], AudioDecodeFailure::malformed,
                "AAC video boundaries are not increasing");
    }
    std::vector<std::uint64_t> audio{0};
    for (std::size_t index = 1; index < video.size(); ++index) {
        const auto end = index + 1 == video.size()
                             ? video[index]
                             : video[index] / stereo_aac_frame_samples * stereo_aac_frame_samples;
        if (end <= audio.back()) {
            continue;
        } // Sub-frame intermediate video span.
        while (end - audio.back() > maximum_segment_samples) {
            const auto grid = audio.back() / target_segment_samples + 1;
            auto next =
                grid * target_segment_samples / stereo_aac_frame_samples * stereo_aac_frame_samples;
            if (next <= audio.back()) {
                next = (grid + 1) * target_segment_samples / stereo_aac_frame_samples *
                       stereo_aac_frame_samples;
            }
            require(next > audio.back() && next - audio.back() <= maximum_segment_samples,
                    AudioDecodeFailure::unsupported, "AAC split exceeds epoch bound");
            audio.push_back(next);
        }
        audio.push_back(end);
    }
    return audio;
}

struct StereoAacHls::Impl {
    enum class Status { empty, queued, generating, sealed, failed };
    struct Entry {
        Status status = Status::empty;
        std::uint64_t size = 0;
    };
    std::unique_ptr<AudioDecoderSource> decoder;
    AacFragmentStore store;
    Mp4Movie movie;
    Bytes config;
    StereoAacHlsAuthoring authoring;
    std::vector<std::uint64_t> boundaries;
    std::uint32_t channels = 0;
    std::uint64_t channel_mask = 0;
    std::uint64_t quota = 0;
    std::uint64_t used = 0;
    std::atomic_bool stopped{false};
    std::atomic<StereoAacHlsFailure> failure{StereoAacHlsFailure::none};
    std::atomic_bool not_cancelled{false};
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::size_t> pending;
    std::vector<Entry> entries;
    std::size_t active_calls = 0;
    std::thread worker;
    bool cleared = false;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() {
        stop();
    }

    void check_failure() const {
        throw_failure(failure.load(std::memory_order_acquire));
    }
    /// First fatal failure cancels all work, including sealed-resource reads.
    /// Cleanup/join stays with Stop; callbacks must never join their own worker.
    void fail(StereoAacHlsFailure reason) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopped || failure.load(std::memory_order_relaxed) != StereoAacHlsFailure::none) {
            return;
        }
        failure.store(reason, std::memory_order_release);
        stopped.store(true, std::memory_order_release);
        for (auto& entry : entries) {
            entry.status = Status::failed;
        }
        pending.clear();
        changed.notify_all();
    }

    /// Keeps storage/source alive until every waiter/read callback has returned.
    class Call {
    public:
        explicit Call(Impl& owner) : owner_(owner) {
            std::lock_guard<std::mutex> lock(owner_.mutex);
            owner_.check_failure();
            require(!owner_.stopped, AudioDecodeFailure::stopped, "AAC cast stopped");
            ++owner_.active_calls;
        }
        ~Call() {
            std::lock_guard<std::mutex> lock(owner_.mutex);
            --owner_.active_calls;
            owner_.changed.notify_all();
        }
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;
        Call(Call&&) = delete;
        Call& operator=(Call&&) = delete;

    private:
        Impl& owner_;
    };
    std::uint64_t await(std::size_t index, const MediaReadContext& context) {
        check_request(context);
        std::unique_lock<std::mutex> lock(mutex);
        check_failure();
        auto& entry = entries.at(index);
        if (entry.status == Status::empty) {
            require(pending.size() < maximum_pending_jobs, AudioDecodeFailure::too_large,
                    "AAC generation queue is full");
            pending.push_back(index);
            entry.status = Status::queued;
            changed.notify_all();
        }
        while (entry.status != Status::sealed && entry.status != Status::failed) {
            check_failure();
            require(!stopped && !context.should_stop(), AudioDecodeFailure::stopped,
                    "AAC cache wait stopped");
            changed.wait_for(lock, waiter_poll);
        }
        check_failure();
        require(!stopped && !context.should_stop(), AudioDecodeFailure::stopped,
                "AAC cache request stopped");
        return entry.size;
    }
    std::size_t read_cached(std::size_t index, std::uint64_t offset, std::uint8_t* output,
                            std::size_t capacity, const MediaReadContext& request) {
        Call call(*this);
        const auto size = await(index, request);
        if (offset >= size || capacity == 0) {
            return 0;
        }
        const auto wanted =
            static_cast<std::size_t>(std::min<std::uint64_t>({capacity, 65536, size - offset}));
        std::size_t count = 0;
        try {
            count = store.read_at(index, offset, output, wanted, AacCacheContext{request, stopped});
            if (!request.should_stop() && !stopped && (count == 0 || count > wanted)) {
                fail(StereoAacHlsFailure::cache_error);
            }
        } catch (...) {
            if (!request.should_stop()) {
                fail(StereoAacHlsFailure::cache_error);
            }
            check_failure();
            throw AudioDecodeException(AudioDecodeFailure::stopped, "AAC cache read stopped");
        }
        check_failure();
        require(!stopped && !request.should_stop(), AudioDecodeFailure::stopped,
                "AAC cache read stopped");
        return count;
    }
    Bytes generate(std::size_t index, const MediaReadContext& context) {
        const auto begin = boundaries.at(index);
        const auto end = boundaries.at(index + 1);
        const auto epoch_begin = begin > preroll_samples ? begin - preroll_samples : 0;
        const auto rounded_end = (end + stereo_aac_frame_samples - 1) / stereo_aac_frame_samples *
                                 stereo_aac_frame_samples;
        const auto count = static_cast<std::uint32_t>(rounded_end + preroll_samples - epoch_begin);
        const auto source_count = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(count, boundaries.back() - epoch_begin));
        auto input = decoder->read_window(epoch_begin, source_count, context);
        if (!input.channels && input.eof) {
            input.channels = channels;
            input.channel_mask = channel_mask;
        }
        // Final lookahead is movie-external silence, not additional source audio.
        // This also keeps an exact seven-day movie inside D70's seek bound.
        if (source_count < count && input.samples.size() / input.channels == source_count) {
            input.samples.resize(static_cast<std::size_t>(count) * input.channels, 0.0F);
        }
        const auto encoded = encode_stereo_aac_epoch(input, count, context);
        require(encoded.audio_specific_config == config, AudioDecodeFailure::codec_error,
                "AAC encoder configuration changed");
        return fragment(movie, encoded, index, begin, end);
    }
    void run() noexcept {
        for (;;) {
            std::size_t index;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stopped || !pending.empty(); });
                if (stopped) {
                    return;
                }
                index = pending.front();
                pending.pop_front();
                entries[index].status = Status::generating;
            }
            const MediaReadContext context{&stopped, &not_cancelled,
                                           std::chrono::steady_clock::now() + generation_deadline};
            bool sealing = false;
            try {
                auto bytes = generate(index, context);
                require(bytes.size() <= quota - used, AudioDecodeFailure::too_large,
                        "AAC cache quota exhausted");
                require(!context.should_stop(), AudioDecodeFailure::stopped,
                        "AAC generation stopped");
                sealing = true;
                store.seal(index, bytes, AacCacheContext{context, stopped});
                require(!context.should_stop(), AudioDecodeFailure::stopped, "AAC sealing stopped");
                std::lock_guard<std::mutex> lock(mutex);
                used += bytes.size();
                entries[index].size = bytes.size();
                entries[index].status = Status::sealed;
            } catch (...) {
                auto reason =
                    sealing ? StereoAacHlsFailure::cache_error : StereoAacHlsFailure::source_error;
                try {
                    throw;
                } catch (const AudioDecodeException& error) {
                    if (!sealing) {
                        reason = decode_failure(error.reason());
                    }
                } catch (const std::bad_alloc&) {
                    reason = StereoAacHlsFailure::resource_limit;
                } catch (...) {
                    // Host exception text is intentionally discarded.
                }
                if (std::chrono::steady_clock::now() >= context.deadline) {
                    reason = StereoAacHlsFailure::generation_timeout;
                }
                fail(reason);
                return;
            }
            changed.notify_all();
        }
    }
    void stop() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopped.store(true, std::memory_order_release);
        }
        changed.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return active_calls == 0; });
        decoder.reset();
        if (!cleared && store.clear) {
            cleared = true;
            try {
                store.clear();
            } catch (...) { /* Host contract violation; destructor remains safe. */
            }
        }
        store = {};
    }
};

StereoAacHls::StereoAacHls(std::shared_ptr<Impl> impl, RemuxedHls presentation)
    : impl_(std::move(impl)), presentation_(std::move(presentation)) {}
StereoAacHls::~StereoAacHls() {
    stop();
}
const RemuxedHls& StereoAacHls::presentation() const noexcept {
    return presentation_;
}
std::size_t StereoAacHls::audio_segment_count() const noexcept {
    return impl_->boundaries.size() - 1;
}
StereoAacHlsFailure StereoAacHls::failure() const noexcept {
    return impl_->failure.load(std::memory_order_acquire);
}
const StereoAacHlsAuthoring& StereoAacHls::authoring() const noexcept {
    return impl_->authoring;
}
void StereoAacHls::stop() noexcept {
    impl_->stop();
    presentation_.resources.clear();
}
std::unique_ptr<StereoAacHls> StereoAacHls::open(MediaSource source, AacFragmentStore store,
                                                 std::uint64_t quota,
                                                 const MediaReadContext& context,
                                                 std::optional<std::uint64_t> selected_id) {
    const bool valid_store = store.seal && store.read_at && store.clear;
    StoreCleanup cleanup(std::move(store));
    check_request(context);
    require(source.size && source.read_at && valid_store && quota > 0,
            AudioDecodeFailure::source_error, "AAC HLS requires source, storage and quota");
    auto impl = std::make_shared<Impl>();
    impl->store = cleanup.release();
    impl->quota = quota;
    const auto size = source.size();
    source.size = [size] { return size; };
    const auto inventory = probe_media_tracks(source, context);
    const auto selection = select_source_audio(inventory, selected_id);
    require(selection.status != AudioSelectionStatus::selection_required,
            AudioDecodeFailure::selection_required, "AAC HLS source selection required");
    require(selection.status == AudioSelectionStatus::selected,
            AudioDecodeFailure::invalid_selection, "AAC HLS has no selected audio");
    const auto& selected = inventory.tracks.at(selection.track_index);
    impl->decoder = AudioDecoderSource::open(source, context, selected.id, true);
    auto presentation = remux_to_hls(source, context.request_cancelled, true, &context);
    require(presentation.zero_origin_video, AudioDecodeFailure::unsupported,
            "AAC HLS video timeline requires a zero origin and no trim");
    validate_stereo_video_spans(presentation.durations_us);
    impl->boundaries = plan_stereo_aac_boundaries(presentation.video_boundaries_48khz);
    const auto audio_count = impl->boundaries.size() - 1;
    auto target = presentation.target_duration_seconds;
    for (std::size_t index = 0; index < audio_count; ++index) {
        const auto segment_samples = impl->boundaries[index + 1] - impl->boundaries[index];
        const auto rounded_seconds =
            static_cast<std::uint32_t>((segment_samples + sample_rate / 2) / sample_rate);
        target = std::max(target, rounded_seconds);
    }
    // Audio snapping can move a segment across an integer-rounding boundary.
    // All renditions share the larger target, including video and subtitles.
    raise_composition_target_duration(presentation, target);
    const auto duration = impl->boundaries.back();
    require(!impl->decoder->requires_start_rebuild() ||
                duration <= AudioDecoderSource::maximum_start_rebuild_samples,
            AudioDecodeFailure::unsupported,
            "coarse Matroska HLS exceeds bounded start reconstruction");
    require(duration > 0 && duration <= maximum_movie_samples, AudioDecodeFailure::too_large,
            "AAC HLS duration bound");
    const auto estimate =
        (duration / sample_rate + 1) * estimated_bytes_per_second + audio_count * 8192;
    require(estimate <= quota, AudioDecodeFailure::too_large,
            "AAC HLS cache quota below admission estimate");
    // One tiny startup decode establishes the source format; no full conversion.
    auto initial = impl->decoder->read_window(0, stereo_aac_frame_samples, context);
    impl->channels = initial.channels;
    impl->channel_mask = initial.channel_mask;
    const auto encoded = encode_stereo_aac_epoch(initial, stereo_aac_frame_samples, context);
    const auto language = selected.language.empty() ? "und" : selected.language;
    impl->movie = audio_movie(encoded.audio_specific_config, language, duration);
    impl->config = encoded.audio_specific_config;
    impl->entries.resize(audio_count);
    // A bounded period encoded with exactly the serving settings replaces the
    // arbitrary nominal-rate allowances. It is discarded, not sealed or kept
    // as a second in-memory fragment cache. The full VOD remains lazy.
    {
        // Calibration owns a separate decoder: demux/codec seek history must
        // not perturb the serving decoder's established startup state.
        auto serving_decoder = std::move(impl->decoder);
        impl->decoder = AudioDecoderSource::open(source, context, selected.id, true);
        const auto reference = impl->generate(0, context);
        impl->authoring.representative_audio_bytes = reference.size();
        impl->authoring.representative_audio_duration_us =
            (impl->boundaries[1] * microseconds_per_second + sample_rate / 2) / sample_rate;
        impl->authoring.representative_audio_rates = hls_bit_rates(
            {impl->authoring.representative_audio_duration_us}, {reference.size()}, target);
        impl->decoder = std::move(serving_decoder);
    }
    impl->authoring.copied_rates =
        hls_bit_rates(presentation.durations_us, presentation.video_segment_bytes, target);
    HlsBitRates subtitle_rates;
    for (const auto& sizes : presentation.subtitle_segment_bytes) {
        const auto rates = hls_bit_rates(presentation.durations_us, sizes, target);
        // At most one subtitle rendition can play at a time.
        subtitle_rates.peak = std::max(subtitle_rates.peak, rates.peak);
        subtitle_rates.average = std::max(subtitle_rates.average, rates.average);
    }
    impl->authoring.copied_rates = add_hls_bit_rates(impl->authoring.copied_rates, subtitle_rates);
    impl->authoring.copied_sizes_estimated = presentation.segment_sizes_estimated;
    impl->authoring.subtitle_sizes_unmeasured =
        presentation.segment_sizes_estimated && presentation.text_track_count != 0;
    presentation.resources.erase(
        std::remove_if(presentation.resources.begin(), presentation.resources.end(),
                       [](const auto& resource) { return resource.name == "main.m3u8"; }),
        presentation.resources.end());
    presentation.resources.push_back(
        memory_resource("audio-init.mp4", "audio/mp4", write_init_segment(impl->movie, true)));
    std::string playlist =
        "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:" +
        std::to_string(presentation.target_duration_seconds) +
        "\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-MAP:URI=\"audio-init.mp4\"\n";
    for (std::size_t index = 0; index < audio_count; ++index) {
        const auto name = "a" + std::to_string(index) + ".m4s";
        playlist +=
            "#EXTINF:" + seconds_text(impl->boundaries[index + 1] - impl->boundaries[index]) +
            ",\n" + name + '\n';
        MediaResource resource{name, "audio/mp4", {}, {}};
        resource.size_on_request = [impl, index](const MediaReadContext& request) {
            Impl::Call call(*impl);
            return impl->await(index, request);
        };
        resource.source.read_at = [impl, index](std::uint64_t offset, std::uint8_t* output,
                                                std::size_t capacity,
                                                const MediaReadContext& request) {
            return impl->read_cached(index, offset, output, capacity, request);
        };
        presentation.resources.push_back(std::move(resource));
    }
    playlist += "#EXT-X-ENDLIST\n";
    presentation.resources.push_back(text_resource("audio.m3u8", playlist));
    presentation.variant.codecs += ",mp4a.40.2";
    const auto declared_rates =
        add_hls_bit_rates(impl->authoring.copied_rates, impl->authoring.representative_audio_rates);
    presentation.variant.peak_bits_per_second = declared_rates.peak;
    presentation.variant.average_bits_per_second = declared_rates.average;
    const AudioRendition audio{"audio.m3u8", language};
    presentation.resources.push_back(text_resource(
        "main.m3u8", multivariant_playlist(presentation.variant, presentation.subtitles, &audio)));
    presentation.playlist_name = "main.m3u8";
    check_request(context);
    auto result = std::unique_ptr<StereoAacHls>(new StereoAacHls(impl, std::move(presentation)));
    impl->worker = std::thread([owner = impl.get()] { owner->run(); });
    return result;
}
} // namespace send_airplay2::detail
