// SPDX-License-Identifier: Apache-2.0
#pragma once
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
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sap2_test {
using namespace send_airplay2;
using namespace send_airplay2::detail;
inline void expect(bool value, const std::string& scenario) {
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
inline const MediaResource& resource(const RemuxedHls& hls, const std::string& name) {
    const auto& all = hls.resources;
    const auto found =
        std::find_if(all.begin(), all.end(), [&](const auto& value) { return value.name == name; });
    expect(found != all.end(), "missing resource " + name);
    return *found;
}
inline const MediaResource& resource(const StereoAacHls& hls, const std::string& name) {
    return resource(hls.presentation(), name);
}
inline Bytes read(const MediaResource& source, const MediaReadContext& context) {
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
inline std::string text(const MediaResource& source, const MediaReadContext& context) {
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
} // namespace sap2_test
