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
#include <exception>
#include <limits>
#include <mutex>
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

struct StereoAacHls::Impl {
    enum class Status { empty, queued, generating, sealed, failed };
    struct Entry {
        Status status = Status::empty;
        std::uint64_t size = 0;
        std::exception_ptr error;
    };
    std::unique_ptr<AudioDecoderSource> decoder;
    AacFragmentStore store;
    Mp4Movie movie;
    Bytes config;
    std::vector<std::uint64_t> boundaries;
    std::uint32_t channels = 0;
    std::uint64_t channel_mask = 0;
    std::uint64_t quota = 0;
    std::uint64_t used = 0;
    std::atomic_bool stopped{false};
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

    /// Keeps storage/source alive until every waiter/read callback has returned.
    class Call {
    public:
        explicit Call(Impl& owner) : owner_(owner) {
            std::lock_guard<std::mutex> lock(owner_.mutex);
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
        auto& entry = entries.at(index);
        if (entry.status == Status::empty) {
            require(pending.size() < maximum_pending_jobs, AudioDecodeFailure::too_large,
                    "AAC generation queue is full");
            pending.push_back(index);
            entry.status = Status::queued;
            changed.notify_all();
        }
        while (entry.status != Status::sealed && entry.status != Status::failed) {
            require(!stopped && !context.should_stop(), AudioDecodeFailure::stopped,
                    "AAC cache wait stopped");
            changed.wait_for(lock, waiter_poll);
        }
        require(!stopped && !context.should_stop(), AudioDecodeFailure::stopped,
                "AAC cache request stopped");
        if (entry.error) {
            std::rethrow_exception(entry.error);
        }
        return entry.size;
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
            try {
                const MediaReadContext context{&stopped, &not_cancelled,
                                               std::chrono::steady_clock::now() +
                                                   generation_deadline};
                auto bytes = generate(index, context);
                require(bytes.size() <= quota - used, AudioDecodeFailure::too_large,
                        "AAC cache quota exhausted");
                require(!context.should_stop(), AudioDecodeFailure::stopped,
                        "AAC generation stopped");
                store.seal(index, bytes, AacCacheContext{context, stopped});
                require(!context.should_stop(), AudioDecodeFailure::stopped, "AAC sealing stopped");
                std::lock_guard<std::mutex> lock(mutex);
                used += bytes.size();
                entries[index].size = bytes.size();
                entries[index].status = Status::sealed;
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex);
                // Decoder failure invalidates it: poison future jobs instead of
                // retrying partial state or publishing a different fragment.
                const auto error = std::current_exception();
                for (auto& entry : entries) {
                    if (entry.status != Status::sealed) {
                        entry.status = Status::failed;
                        entry.error = error;
                    }
                }
                pending.clear();
                changed.notify_all();
                return;
            }
            changed.notify_all();
        }
    }
    void stop() noexcept {
        stopped.store(true, std::memory_order_relaxed);
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
void StereoAacHls::stop() noexcept {
    impl_->stop();
    presentation_.resources.clear();
}
std::unique_ptr<StereoAacHls> StereoAacHls::open(MediaSource source, AacFragmentStore store,
                                                 std::uint64_t quota,
                                                 const MediaReadContext& context,
                                                 std::optional<std::uint64_t> selected_id) {
    check_request(context);
    require(source.size && source.read_at && store.seal && store.read_at && store.clear &&
                quota > 0,
            AudioDecodeFailure::source_error, "AAC HLS requires source, storage and quota");
    const auto size = source.size();
    source.size = [size] { return size; };
    const auto inventory = probe_media_tracks(source, context);
    const auto selection = select_source_audio(inventory, selected_id);
    require(selection.status != AudioSelectionStatus::selection_required,
            AudioDecodeFailure::selection_required, "AAC HLS source selection required");
    require(selection.status == AudioSelectionStatus::selected,
            AudioDecodeFailure::invalid_selection, "AAC HLS has no selected audio");
    const auto& selected = inventory.tracks.at(selection.track_index);
    auto impl = std::make_shared<Impl>();
    impl->store = std::move(store);
    impl->quota = quota;
    impl->decoder = AudioDecoderSource::open(source, context, selected.id, true);
    auto presentation = remux_to_hls(source, context.request_cancelled, true, &context);
    require(presentation.zero_origin_video, AudioDecodeFailure::unsupported,
            "AAC HLS video timeline requires a zero origin and no trim");
    impl->boundaries.push_back(0);
    for (std::size_t index = 0; index < presentation.segment_count; ++index) {
        auto end = presentation.video_boundaries_48khz.at(index + 1);
        require(end <= maximum_movie_samples, AudioDecodeFailure::too_large,
                "AAC HLS movie exceeds seven days");
        if (index + 1 != presentation.segment_count) {
            end = end / stereo_aac_frame_samples * stereo_aac_frame_samples;
        }
        require(end > impl->boundaries.back() &&
                    end - impl->boundaries.back() <= maximum_segment_samples,
                AudioDecodeFailure::unsupported,
                "AAC HLS segment exceeds seven seconds or is empty");
        impl->boundaries.push_back(end);
    }
    const auto duration = impl->boundaries.back();
    require(duration > 0 && duration <= maximum_movie_samples, AudioDecodeFailure::too_large,
            "AAC HLS duration bound");
    const auto estimate = (duration / sample_rate + 1) * estimated_bytes_per_second +
                          presentation.segment_count * 8192;
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
    impl->entries.resize(presentation.segment_count);
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
    for (std::size_t index = 0; index < presentation.segment_count; ++index) {
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
            Impl::Call call(*impl);
            const auto size = impl->await(index, request);
            if (offset >= size) {
                return std::size_t{0};
            }
            const auto wanted =
                static_cast<std::size_t>(std::min<std::uint64_t>({capacity, 65536, size - offset}));
            const auto count = impl->store.read_at(index, offset, output, wanted,
                                                   AacCacheContext{request, impl->stopped});
            require(!impl->stopped && !request.should_stop(), AudioDecodeFailure::stopped,
                    "AAC cache read stopped");
            require(count > 0 && count <= wanted, AudioDecodeFailure::source_error,
                    "AAC cache read failed");
            return count;
        };
        presentation.resources.push_back(std::move(resource));
    }
    playlist += "#EXT-X-ENDLIST\n";
    presentation.resources.push_back(text_resource("audio.m3u8", playlist));
    presentation.variant.codecs += ",mp4a.40.2";
    presentation.variant.peak_bits_per_second += estimated_bytes_per_second * 8;
    presentation.variant.average_bits_per_second += stereo_aac_bit_rate;
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
