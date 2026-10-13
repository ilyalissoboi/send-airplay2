// SPDX-License-Identifier: Apache-2.0
#include "stereo_aac_hls.h"
#include "audio_decoder_source.h"
#include "mkv_demux.h"
#include "sample_entries.h"
#include "stereo_aac_encoder.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
void expect(bool value, const std::string& scenario) {
    if (!value) {
        throw std::runtime_error(scenario);
    }
}
struct Context {
    std::atomic_bool stopped{false};
    std::atomic_bool cancelled{false};
    MediaReadContext value{&stopped, &cancelled,
                           std::chrono::steady_clock::now() + std::chrono::seconds(40)};
};
struct Source {
    std::shared_ptr<Bytes> bytes;
    std::atomic<std::size_t> calls{0};
    std::atomic_bool block{false};
    std::atomic_bool entered{false};
    explicit Source(const std::filesystem::path& path) : bytes(std::make_shared<Bytes>()) {
        std::ifstream file(path, std::ios::binary);
        expect(file.good(), "missing HLS fixture " + path.filename().string());
        bytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    MediaSource callbacks() {
        return {[this] { return static_cast<std::uint64_t>(bytes->size()); },
                [this](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                       const MediaReadContext& context) {
                    ++calls;
                    if (block) {
                        entered = true;
                        while (block && !context.should_stop()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                    if (context.should_stop() || offset >= bytes->size()) {
                        return std::size_t{0};
                    }
                    const auto count = static_cast<std::size_t>(
                        std::min<std::uint64_t>({capacity, 997, bytes->size() - offset}));
                    std::memcpy(output, bytes->data() + offset, count);
                    return count;
                }};
    }
};
struct Store {
    std::mutex mutex;
    std::map<std::size_t, Bytes> sealed;
    std::size_t seals = 0;
    std::size_t clears = 0;
    bool fail_seal = false;
    AacFragmentStore callbacks() {
        return {[this](std::size_t index, const Bytes& bytes, const AacCacheContext& context) {
                    expect(!context.should_stop(), "seal after stop");
                    std::lock_guard<std::mutex> lock(mutex);
                    if (fail_seal) {
                        throw std::runtime_error("injected atomic seal failure");
                    }
                    expect(sealed.count(index) == 0, "duplicate sealed generation");
                    sealed.emplace(index, bytes);
                    ++seals;
                },
                [this](std::size_t index, std::uint64_t offset, std::uint8_t* output,
                       std::size_t capacity, const AacCacheContext& context) {
                    if (context.should_stop()) {
                        return std::size_t{0};
                    }
                    std::lock_guard<std::mutex> lock(mutex);
                    const auto& bytes = sealed.at(index);
                    const auto count = static_cast<std::size_t>(
                        std::min<std::uint64_t>({capacity, 509, bytes.size() - offset}));
                    std::memcpy(output, bytes.data() + offset, count);
                    return count;
                },
                [this] {
                    std::lock_guard<std::mutex> lock(mutex);
                    sealed.clear();
                    ++clears;
                }};
    }
};
const MediaResource& resource(const StereoAacHls& hls, const std::string& name) {
    const auto& all = hls.presentation().resources;
    const auto found =
        std::find_if(all.begin(), all.end(), [&](const auto& value) { return value.name == name; });
    expect(found != all.end(), "missing resource " + name);
    return *found;
}
Bytes read(const MediaResource& source, const MediaReadContext& context) {
    const auto size = media_resource_size(source, context);
    Bytes bytes(static_cast<std::size_t>(size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count =
            source.source.read_at(offset, bytes.data() + offset,
                                  std::min<std::size_t>(65536, bytes.size() - offset), context);
        expect(count > 0 && count <= bytes.size() - offset, "resource short-read progress");
        offset += count;
    }
    return bytes;
}
std::string text(const MediaResource& source, const MediaReadContext& context) {
    const auto bytes = read(source, context);
    return std::string(bytes.begin(), bytes.end());
}

/// Independent FFmpeg MOV reader/decoder checks the generated fMP4 representation.
class MovieDecoder {
public:
    explicit MovieDecoder(const Bytes& bytes, std::int64_t origin = 0)
        : bytes_(bytes), origin_(origin) {
        try {
            format_ = avformat_alloc_context();
            auto* buffer = static_cast<std::uint8_t*>(av_malloc(32768));
            expect(buffer != nullptr, "oracle I/O buffer allocation");
            io_ = avio_alloc_context(buffer, 32768, 0, this, receive, nullptr, seek);
            if (!io_) {
                av_free(buffer);
            }
            expect(format_ && io_, "oracle allocation");
            format_->pb = io_;
            format_->flags |= AVFMT_FLAG_CUSTOM_IO;
            expect(avformat_open_input(&format_, nullptr, av_find_input_format("mov"), nullptr) >=
                       0,
                   "oracle MOV open");
            expect(avformat_find_stream_info(format_, nullptr) >= 0 && format_->nb_streams == 1,
                   "audio-only MOV oracle");
            codec_ = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_AAC));
            expect(codec_ &&
                       avcodec_parameters_to_context(codec_, format_->streams[0]->codecpar) >= 0,
                   "oracle AAC open");
            // Use global demuxed PTS/duration, rather than reapplying stream
            // padding at the first packet of a separately fetched late fragment.
            codec_->initial_padding = 0;
            codec_->flags2 |= AV_CODEC_FLAG2_SKIP_MANUAL;
            expect(avcodec_open2(codec_, codec_->codec, nullptr) >= 0, "oracle AAC decoder open");
            packet_ = av_packet_alloc();
            frame_ = av_frame_alloc();
            expect(packet_ && frame_, "oracle packet/frame allocation");
        } catch (...) {
            release();
            throw;
        }
    }
    ~MovieDecoder() {
        release();
    }
    void release() noexcept {
        av_frame_free(&frame_);
        av_packet_free(&packet_);
        avcodec_free_context(&codec_);
        avformat_close_input(&format_);
        if (io_) {
            av_freep(&io_->buffer);
            avio_context_free(&io_);
        }
    }
    MovieDecoder(const MovieDecoder&) = delete;
    MovieDecoder& operator=(const MovieDecoder&) = delete;
    MovieDecoder(MovieDecoder&&) = delete;
    MovieDecoder& operator=(MovieDecoder&&) = delete;
    std::vector<float> decode() {
        std::vector<float> output;
        while (av_read_frame(format_, packet_) >= 0) {
            expect(avcodec_send_packet(codec_, packet_) >= 0, "oracle send AAC");
            drain(output);
            av_packet_unref(packet_);
        }
        expect(avcodec_send_packet(codec_, nullptr) >= 0, "oracle drain AAC");
        drain(output);
        return output;
    }

private:
    const Bytes& bytes_;
    std::int64_t origin_;
    std::size_t position_ = 0;
    AVFormatContext* format_ = nullptr;
    AVIOContext* io_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    static int receive(void* opaque, std::uint8_t* output, int capacity) {
        auto& self = *static_cast<MovieDecoder*>(opaque);
        if (self.position_ == self.bytes_.size()) {
            return AVERROR_EOF;
        }
        const auto count =
            std::min(static_cast<std::size_t>(capacity), self.bytes_.size() - self.position_);
        std::memcpy(output, self.bytes_.data() + self.position_, count);
        self.position_ += count;
        return static_cast<int>(count);
    }
    static std::int64_t seek(void* opaque, std::int64_t offset, int whence) {
        auto& self = *static_cast<MovieDecoder*>(opaque);
        if (whence == AVSEEK_SIZE) {
            return self.bytes_.size();
        }
        const auto origin = whence == SEEK_SET   ? 0
                            : whence == SEEK_CUR ? self.position_
                                                 : self.bytes_.size();
        const auto next = static_cast<std::int64_t>(origin) + offset;
        if (next < 0 || static_cast<std::uint64_t>(next) > self.bytes_.size()) {
            return AVERROR(EINVAL);
        }
        self.position_ = static_cast<std::size_t>(next);
        return next;
    }
    void drain(std::vector<float>& output) {
        for (;;) {
            const auto result = avcodec_receive_frame(codec_, frame_);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            expect(result >= 0 && frame_->format == AV_SAMPLE_FMT_FLTP &&
                       frame_->ch_layout.nb_channels == 2 && frame_->sample_rate == 48000,
                   "oracle stereo LC format");
            const auto* left = reinterpret_cast<float*>(frame_->extended_data[0]);
            const auto* right = reinterpret_cast<float*>(frame_->extended_data[1]);
            if (frame_->pts < origin_) {
                expect(frame_->pts + frame_->duration == origin_,
                       "only initial priming precedes the presentation");
                av_frame_unref(frame_);
                continue;
            }
            // MOV removes initial priming. AAC still decodes its complete final
            // frame; the demuxed duration, not nb_samples, defines the VOD end.
            expect(frame_->pts == origin_ + static_cast<std::int64_t>(output.size() / 2) &&
                       frame_->duration > 0 && frame_->duration <= frame_->nb_samples,
                   "oracle presentation timestamp: got " + std::to_string(frame_->pts) +
                       " expected " + std::to_string(origin_ + output.size() / 2) + " duration " +
                       std::to_string(frame_->duration));
            for (int sample = 0; sample < frame_->duration; ++sample) {
                output.push_back(left[sample]);
                output.push_back(right[sample]);
            }
            av_frame_unref(frame_);
        }
    }
};
void downmix_contract() {
    const auto matrix = stereo_downmix_matrix(0x3f, 6);
    const auto denominator = 1.0 + 0.7071067811865475244 * 2 + 0.5;
    expect(std::abs(matrix[0] - 1 / denominator) < 1e-12 && matrix[1] == 0 &&
               std::abs(matrix[2] - 0.7071067811865475244 / denominator) < 1e-12 &&
               std::abs(matrix[3] - 0.5 / denominator) < 1e-12 && matrix[5] == 0 &&
               matrix[8] == 0 && std::abs(matrix[9] - 1 / denominator) < 1e-12,
           "literal 5.1 weights and speaker routing");
    const auto surround = stereo_downmix_matrix(0x63f, 8);
    double left = 0, right = 0;
    for (std::size_t channel = 0; channel < 8; ++channel) {
        left += surround[channel];
        right += surround[8 + channel];
    }
    expect(std::abs(left - 1) < 1e-12 && std::abs(right - 1) < 1e-12 && surround[6] > 0 &&
               surround[15] > 0,
           "7.1 correlated headroom and side speakers");
    try {
        (void)stereo_downmix_matrix(0x43, 3);
        throw std::logic_error("unknown speaker accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported, "unknown speaker category");
    }
}
Bytes encoded_audio_movie(const DecodedAudioWindow& input, const MediaReadContext& context) {
    constexpr std::uint32_t input_samples = 8192;
    const auto encoded = encode_stereo_aac_epoch(input, input_samples, context);
    expect(encoded.priming_samples == 1024 && encoded.packets.size() == 9,
           "eight AAC frames plus literal priming packet");
    Mp4Movie movie;
    movie.timescale = 48000;
    Mp4Track audio;
    audio.kind = TrackKind::audio;
    audio.timescale = 48000;
    audio.codec = "mp4a";
    audio.sample_entry = aac_sample_entry(2, 48000, encoded.audio_specific_config);
    audio.track_header = track_header_box(TrackKind::audio, 1, 0, 0);
    audio.edits.push_back({input_samples, 1024, 1, 0});
    Bytes payload;
    for (std::size_t index = 0; index < encoded.packets.size(); ++index) {
        const auto& packet = encoded.packets[index];
        expect(packet.presentation_sample == static_cast<std::int64_t>(index * 1024) - 1024,
               "literal AAC packet PTS grid");
        audio.samples.push_back(
            {0, index * 1024, static_cast<std::uint32_t>(packet.bytes.size()), 1024, 0, true});
        payload.insert(payload.end(), packet.bytes.begin(), packet.bytes.end());
    }
    movie.tracks.push_back(std::move(audio));
    auto bytes = write_init_segment(movie, true);
    const auto header = write_segment_header(movie, SegmentLayout{1, {{0, 9}}});
    bytes.insert(bytes.end(), header.begin(), header.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}
void encoded_speaker_routing() {
    Context context;
    constexpr double surround_gain = 0.7071067811865475244;
    constexpr double input_peak = 0.25;
    constexpr double pi = 3.14159265358979323846;
    for (const auto channels : {6U, 8U}) {
        const auto denominator = 1 + 0.5 + surround_gain * (channels == 6 ? 2 : 3);
        for (std::uint32_t speaker = 0; speaker < channels; ++speaker) {
            DecodedAudioWindow input;
            input.channels = channels;
            input.channel_mask = channels == 6 ? 0x3f : 0x63f;
            input.samples.resize(8192 * channels);
            for (std::size_t sample = 0; sample < 8192; ++sample) {
                input.samples[sample * channels + speaker] =
                    static_cast<float>(input_peak * std::sin(2 * pi * 1000 * sample / 48000));
            }
            const auto movie = encoded_audio_movie(input, context.value);
            const auto decoded = MovieDecoder(movie).decode();
            expect(decoded.size() == 8192 * 2, "isolated speaker presentation length");
            double energy[2]{};
            for (std::size_t sample = 4096; sample < 8192; ++sample) {
                for (std::size_t channel = 0; channel < 2; ++channel) {
                    const auto value = decoded[sample * 2 + channel];
                    energy[channel] += value * value;
                }
            }
            const bool both = speaker == 2 || speaker == 3;
            const bool left = speaker == 0 || speaker == 4 || speaker == 6;
            const auto weight = speaker < 2 ? 1.0 : speaker == 3 ? 0.5 : surround_gain;
            const auto expected_rms = input_peak * weight / denominator / std::sqrt(2.0);
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto actual = std::sqrt(energy[channel] / 4096);
                const bool audible = both || (left ? channel == 0 : channel == 1);
                expect(audible ? std::abs(actual - expected_rms) < expected_rms * 0.1
                               : actual < 0.0001,
                       "encoded " + std::to_string(channels) + " channel speaker " +
                           std::to_string(speaker) + " output " + std::to_string(channel) +
                           " RMS " + std::to_string(actual));
            }
        }
    }
}
void copied_video_and_subtitles(Source& source, const StereoAacHls& hls,
                                const std::filesystem::path& path, const std::string& master,
                                const MediaReadContext& context) {
    const RandomReader source_reader = [&](std::uint64_t offset, std::uint8_t* output,
                                           std::size_t count) {
        expect(offset <= source.bytes->size() && count <= source.bytes->size() - offset,
               "video source range");
        std::memcpy(output, source.bytes->data() + offset, count);
    };
    const auto video = path.extension() == ".mkv"
                           ? read_mkv_video_source(source_reader, source.bytes->size())
                           : read_mp4_video_source(source_reader, source.bytes->size());
    expect(video.tracks.size() == 1 && video.tracks.front().kind == TrackKind::video,
           "copied video has no muxed audio");
    const auto video_segments = plan_segments(video);
    for (std::size_t index = 0; index < video_segments.size(); ++index) {
        const auto copied = read(resource(hls, "s" + std::to_string(index) + ".m4s"), context);
        const auto header = write_segment_header(video, video_segments[index]);
        std::size_t payload = header.size();
        const auto& range = video_segments[index].ranges.front();
        for (auto sample = range.first; sample < range.end; ++sample) {
            const auto& packet = video.tracks.front().samples[sample];
            expect(
                payload + packet.size <= copied.size() &&
                    std::equal(copied.begin() + static_cast<std::ptrdiff_t>(payload),
                               copied.begin() + static_cast<std::ptrdiff_t>(payload + packet.size),
                               source.bytes->begin() + static_cast<std::ptrdiff_t>(packet.offset)),
                "video packet changed in segment " + std::to_string(index));
            payload += packet.size;
        }
        expect(payload == copied.size(), "video payload has no added audio bytes");
    }
    if (path.filename() == "dts-subtitles.mp4") {
        expect(master.find("SUBTITLES=\"subs\"") != std::string::npos &&
                   text(resource(hls, "t0s0.vtt"), context).find("Original caption") !=
                       std::string::npos,
               "original subtitle association and cues preserved");
    }
}

void cold_audio_seek(const StereoAacHls& hls, const MediaResource& late,
                     const std::vector<float>& decoded, const MediaReadContext& context) {
    Bytes cold = read(resource(hls, "audio-init.mp4"), context);
    const auto late_bytes = read(late, context);
    cold.insert(cold.end(), late_bytes.begin(), late_bytes.end());
    const auto cold_decoded = MovieDecoder(cold, 575488).decode();
    expect(cold_decoded.size() == (792000 - 575488) * 2,
           "cold seek retains absolute timestamps and VOD end");
    double cold_error = 0;
    // AAC synthesis overlap needs one packet of history after a cold seek.
    // Compare every later sample within a strict decoded-error bound. AAC decoder
    // state (including synthesis/noise history) need not produce bit-identical PCM.
    for (std::size_t sample = 2048; sample < cold_decoded.size(); ++sample) {
        const auto difference = cold_decoded[sample] - decoded[575488 * 2 + sample];
        cold_error += difference * difference;
    }
    const auto cold_rms = std::sqrt(cold_error / (cold_decoded.size() - 2048));
    expect(cold_rms < 0.001,
           "cold seek AAC after one overlap frame RMS " + std::to_string(cold_rms));
}

void audible_joins(Source& source, const std::filesystem::path& path,
                   const std::vector<float>& decoded, const MediaReadContext& context) {
    auto reference = AudioDecoderSource::open(source.callbacks(), context, 42, true);
    (void)reference->read_window(0, 1024, context);
    // Join reference windows begin before the boundary and contain continuous
    // source decoder/resampler history across it. No correlation offset allowed.
    if (path.filename() == "aac-short.mp4" || path.filename() == "aac-edits.mp4") {
        expect(std::all_of(decoded.begin() + 8 * 48000, decoded.end(),
                           [](float value) { return std::abs(value) < 1e-6; }),
               "selected audio EOF becomes deliberate movie-timeline silence");
    } else {
        for (const auto boundary : {287744ULL, 575488ULL}) {
            const auto window = reference->read_window(boundary - 24000, 48000, context);
            const auto matrix = stereo_downmix_matrix(window.channel_mask, window.channels);
            double squared = 0;
            for (std::size_t sample = 0; sample < 48000; ++sample) {
                for (std::size_t channel = 0; channel < 2; ++channel) {
                    double expected = 0;
                    for (std::size_t input = 0; input < window.channels; ++input) {
                        expected += window.samples[sample * window.channels + input] *
                                    matrix[channel * 8 + input];
                    }
                    const auto difference =
                        decoded[(boundary - 24000 + sample) * 2 + channel] - expected;
                    squared += difference * difference;
                }
            }
            const auto rms = std::sqrt(squared / 96000);
            expect(rms < 0.004, path.filename().string() + " audible join " +
                                    std::to_string(boundary) + " RMS " + std::to_string(rms));
        }
    }
}
void movie_join(const std::filesystem::path& path) {
    Source source(path);
    Store store;
    Context context;
    auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                  context.value, 42);
    expect(store.seals == 0 && hls->presentation().segment_count == 3,
           "startup must not encode the full movie");
    const auto master = text(resource(*hls, "main.m3u8"), context.value);
    expect(master.find("TYPE=AUDIO") != std::string::npos &&
               master.find("CHANNELS=\"2\"") != std::string::npos &&
               master.find("mp4a.40.2") != std::string::npos &&
               master.find("ac-3") == std::string::npos,
           "separate AAC master metadata");
    copied_video_and_subtitles(source, *hls, path, master, context.value);
    // Cold late seek, then backward: byte identity after sealing and coalesced reads.
    auto late = resource(*hls, "a2.m4s");
    auto first = std::async(std::launch::async, [&] { return read(late, context.value); });
    auto second = std::async(std::launch::async, [&] { return read(late, context.value); });
    expect(first.get() == second.get() && store.seals == 1, "coalesced late seek sealed once");
    Bytes combined = read(resource(*hls, "audio-init.mp4"), context.value);
    for (int index = 0; index < 3; ++index) {
        const auto fragment =
            read(resource(*hls, "a" + std::to_string(index) + ".m4s"), context.value);
        combined.insert(combined.end(), fragment.begin(), fragment.end());
    }
    expect(store.seals == 3, "one immutable generation per segment");
    const auto decoded = MovieDecoder(combined).decode();
    expect(decoded.size() == 792000 * 2,
           "audible VOD sample count: got " + std::to_string(decoded.size() / 2));
    cold_audio_seek(*hls, late, decoded, context.value);
    audible_joins(source, path, decoded, context.value);
    const auto before = source.calls.load();
    hls->stop();
    hls->stop();
    expect(store.clears == 1 && store.sealed.empty() && before == source.calls,
           "stop clears once and releases decoder");
    try {
        (void)media_resource_size(late, context.value);
        throw std::logic_error("retained audio resource active after stop");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::stopped, "retained resource stop category");
    }
}
void quota_admission(const std::filesystem::path& path) {
    Source source(path);
    Store store;
    Context context;
    try {
        (void)StereoAacHls::open(source.callbacks(), store.callbacks(), 1, context.value, 42);
        throw std::logic_error("tiny quota accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::too_large, "quota admission category");
    }
}
void waiter_cancellation(const std::filesystem::path& path) {
    Source source(path);
    Store store;
    Context context;
    auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                  context.value, 42);
    auto late = resource(*hls, "a2.m4s");
    Context cancelled;
    source.block = true;
    auto waiter = std::async(std::launch::async, [&] {
        try {
            (void)media_resource_size(late, cancelled.value);
            return false;
        } catch (const AudioDecodeException& error) {
            return error.reason() == AudioDecodeFailure::stopped;
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!source.entered && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    expect(source.entered, "worker blocked in cooperative source");
    cancelled.cancelled = true;
    expect(waiter.get() && store.seals == 0,
           "one waiter cancelled while shared work remains pending");
    Context expired;
    expired.value.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    try {
        (void)media_resource_size(late, expired.value);
        throw std::logic_error("waiter deadline extended");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::stopped, "waiter absolute deadline");
    }
    source.block = false;
    expect(!read(late, context.value).empty() && store.seals == 1,
           "independent waiter succeeds after first cancels");
    hls->stop();
}
void cast_stop(const std::filesystem::path& path) {
    Source source(path);
    Store store;
    Context context;
    source.entered = false;
    auto stopping = StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                       context.value, 42);
    auto pending = resource(*stopping, "a1.m4s");
    source.block = true;
    auto active = std::async(std::launch::async, [&] {
        try {
            (void)media_resource_size(pending, context.value);
            return false;
        } catch (const AudioDecodeException&) {
            return true;
        }
    });
    const auto stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!source.entered && std::chrono::steady_clock::now() < stop_deadline) {
        std::this_thread::yield();
    }
    expect(source.entered, "stop test entered decoder");
    stopping->stop();
    expect(active.get() && store.sealed.empty(), "stop joined worker and waiters before clearing");
}
void atomic_seal_failure(const std::filesystem::path& path) {
    Source source(path);
    Store store;
    Context context;
    source.block = false;
    store.fail_seal = true;
    auto failing = StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                      context.value, 42);
    try {
        (void)read(resource(*failing, "a0.m4s"), context.value);
        throw std::logic_error("seal failure not propagated");
    } catch (const std::runtime_error& error) {
        expect(std::string(error.what()) == "injected atomic seal failure",
               "seal failure preserved");
    }
    expect(store.sealed.empty(), "failed seal has no published partial bytes");
    try {
        (void)read(resource(*failing, "a1.m4s"), context.value);
        throw std::logic_error("poisoned cast continued");
    } catch (const std::runtime_error& error) {
        expect(std::string(error.what()) == "injected atomic seal failure",
               "failed cast refuses later jobs");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "fixture directory argument");
        const std::filesystem::path fixtures(argv[1]);
        downmix_contract();
        encoded_speaker_routing();
        for (const auto* name :
             {"aac.mp4", "ac3.mp4", "eac3.mp4", "dts.mp4", "dts.mkv", "aac-short.mp4",
              "aac-edits.mp4", "dts-subtitles.mp4", "dts-bframes.mp4"}) {
            movie_join(fixtures / name);
        }
        quota_admission(fixtures / "dts.mp4");
        waiter_cancellation(fixtures / "dts.mp4");
        cast_stop(fixtures / "dts.mp4");
        atomic_seal_failure(fixtures / "dts.mp4");
        std::cout << "stereo AAC HLS joins, timing, downmix and bounded lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
