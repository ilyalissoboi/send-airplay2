// SPDX-License-Identifier: Apache-2.0
#include "probe.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr std::uint64_t maximum_source_bytes = 8 * 1024 * 1024;
constexpr int io_buffer_bytes = 32768;
constexpr int output_rate = 48000;
constexpr int maximum_frame_samples = 65536;
constexpr int maximum_channels = 8;
constexpr std::uint64_t maximum_stereo_samples = 60 * output_rate;
constexpr double center_surround_gain = 0.7071067811865475244;
constexpr double lfe_gain = 0.5;

struct ProbeError {
    std::int32_t result;
};
void require(bool condition, std::int32_t result = SAP2_PROBE_CODEC_ERROR) {
    if (!condition) {
        throw ProbeError{result};
    }
}
void checked(int result) {
    require(result >= 0);
}
bool stopped(const sap2_probe_source& source) {
    return source.should_stop && source.should_stop(source.context);
}
void check_cancel(const sap2_probe_source& source) {
    require(!stopped(source), SAP2_PROBE_CANCELLED);
}

template <typename T, void (*Free)(T**)> struct AvDeleter {
    void operator()(T* value) const noexcept {
        Free(&value);
    }
};
using Codec = std::unique_ptr<AVCodecContext, AvDeleter<AVCodecContext, avcodec_free_context>>;
using Frame = std::unique_ptr<AVFrame, AvDeleter<AVFrame, av_frame_free>>;
using Packet = std::unique_ptr<AVPacket, AvDeleter<AVPacket, av_packet_free>>;
using Resampler = std::unique_ptr<SwrContext, AvDeleter<SwrContext, swr_free>>;
using Format = std::unique_ptr<AVFormatContext, AvDeleter<AVFormatContext, avformat_close_input>>;
struct IoDeleter {
    void operator()(AVIOContext* value) const noexcept {
        if (value) {
            // libavformat may replace the original buffer; free its current owner.
            av_freep(&value->buffer);
            avio_context_free(&value);
        }
    }
};
struct FifoDeleter {
    void operator()(AVAudioFifo* value) const noexcept {
        av_audio_fifo_free(value);
    }
};
using Io = std::unique_ptr<AVIOContext, IoDeleter>;
using Fifo = std::unique_ptr<AVAudioFifo, FifoDeleter>;

struct Reader {
    const sap2_probe_source& source;
    std::uint64_t position = 0;
    bool failed = false;
    static int read(void* context, std::uint8_t* buffer, int capacity) noexcept {
        auto& reader = *static_cast<Reader*>(context);
        try {
            if (stopped(reader.source)) {
                return AVERROR_EXIT;
            }
            if (reader.position == reader.source.size) {
                return AVERROR_EOF;
            }
            const auto available = reader.source.size - reader.position;
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                available,
                std::min<std::uint64_t>(static_cast<std::uint64_t>(capacity), io_buffer_bytes)));
            const auto actual =
                reader.source.read_at(reader.source.context, reader.position, buffer, count);
            if (actual == 0 || actual > count) {
                reader.failed = true;
                return AVERROR(EIO);
            }
            reader.position += actual;
            return static_cast<int>(actual);
        } catch (...) {
            reader.failed = true;
            return AVERROR(EIO);
        }
    }
    static std::int64_t seek(void* context, std::int64_t offset, int whence) noexcept {
        auto& reader = *static_cast<Reader*>(context);
        if (whence == AVSEEK_SIZE) {
            return static_cast<std::int64_t>(reader.source.size);
        }
        const auto origin = whence & ~AVSEEK_FORCE;
        std::int64_t base = 0;
        if (origin == SEEK_CUR) {
            base = static_cast<std::int64_t>(reader.position);
        } else if (origin == SEEK_END) {
            base = static_cast<std::int64_t>(reader.source.size);
        } else if (origin != SEEK_SET) {
            return AVERROR(EINVAL);
        }
        // The source is small, but offset is arbitrary signed input from AVIO.
        if (offset < -base || offset > static_cast<std::int64_t>(reader.source.size) - base) {
            return AVERROR(EINVAL);
        }
        reader.position = static_cast<std::uint64_t>(base + offset);
        return static_cast<std::int64_t>(reader.position);
    }
    static int interrupt(void* context) noexcept {
        try {
            return stopped(static_cast<Reader*>(context)->source) ? 1 : 0;
        } catch (...) {
            return 1;
        }
    }
};

/// Row-major [left/right][source channel], with conservative correlated-peak
/// headroom. Layout identities, never channel-count guesses, choose the weights.
std::array<double, 2 * maximum_channels> stereo_matrix(const AVChannelLayout& layout) {
    require(layout.order == AV_CHANNEL_ORDER_NATIVE && av_channel_layout_check(&layout) &&
                layout.nb_channels >= 1 && layout.nb_channels <= maximum_channels,
            SAP2_PROBE_UNSUPPORTED);
    std::array<double, 2 * maximum_channels> matrix{};
    double left_sum = 0;
    double right_sum = 0;
    for (int index = 0; index < layout.nb_channels; ++index) {
        double left = 0;
        double right = 0;
        switch (av_channel_layout_channel_from_index(&layout, static_cast<unsigned>(index))) {
        case AV_CHAN_FRONT_LEFT:
            left = 1;
            break;
        case AV_CHAN_FRONT_RIGHT:
            right = 1;
            break;
        case AV_CHAN_FRONT_CENTER:
            left = right = layout.nb_channels == 1 ? 1 : center_surround_gain;
            break;
        case AV_CHAN_LOW_FREQUENCY:
            left = right = lfe_gain;
            break;
        case AV_CHAN_SIDE_LEFT:
        case AV_CHAN_BACK_LEFT:
            left = center_surround_gain;
            break;
        case AV_CHAN_SIDE_RIGHT:
        case AV_CHAN_BACK_RIGHT:
            right = center_surround_gain;
            break;
        default:
            throw ProbeError{SAP2_PROBE_UNSUPPORTED};
        }
        matrix[static_cast<std::size_t>(index)] = left;
        matrix[maximum_channels + static_cast<std::size_t>(index)] = right;
        left_sum += left;
        right_sum += right;
    }
    const auto headroom = std::max({1.0, left_sum, right_sum});
    for (auto& value : matrix) {
        value /= headroom;
    }
    return matrix;
}

class Encoder {
public:
    Encoder(const sap2_probe_source& source, sap2_probe_report& report)
        : source_(source), report_(report),
          codec_(avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_AAC))),
          frame_(av_frame_alloc()), packet_(av_packet_alloc()),
          verification_(avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_AAC))),
          verification_frame_(av_frame_alloc()),
          fifo_(av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, maximum_frame_samples)) {
        require(codec_ && frame_ && packet_ && verification_ && verification_frame_ && fifo_);
        codec_->sample_rate = output_rate;
        codec_->sample_fmt = AV_SAMPLE_FMT_FLTP;
        codec_->bit_rate = 192000;
        codec_->profile = AV_PROFILE_AAC_LOW;
        codec_->time_base = AVRational{1, output_rate};
        codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        av_channel_layout_default(&codec_->ch_layout, 2);
        checked(avcodec_open2(codec_.get(), codec_->codec, nullptr));
        require(codec_->extradata_size > 0);
        verification_->extradata = static_cast<std::uint8_t*>(av_mallocz(
            static_cast<std::size_t>(codec_->extradata_size) + AV_INPUT_BUFFER_PADDING_SIZE));
        require(verification_->extradata != nullptr);
        std::memcpy(verification_->extradata, codec_->extradata,
                    static_cast<std::size_t>(codec_->extradata_size));
        verification_->extradata_size = codec_->extradata_size;
        checked(avcodec_open2(verification_.get(), verification_->codec, nullptr));
        require(codec_->frame_size > 0 && codec_->frame_size <= maximum_frame_samples);
        report_.initial_padding = static_cast<std::uint32_t>(codec_->initial_padding);
        frame_->format = codec_->sample_fmt;
        frame_->sample_rate = output_rate;
        frame_->nb_samples = codec_->frame_size;
        checked(av_channel_layout_copy(&frame_->ch_layout, &codec_->ch_layout));
        checked(av_frame_get_buffer(frame_.get(), 0));
    }
    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    Encoder(Encoder&&) = delete;
    Encoder& operator=(Encoder&&) = delete;

    void append(std::uint8_t* const* data, int count) {
        check_cancel(source_);
        require(count >= 0 && count <= maximum_frame_samples);
        require(report_.stereo_samples + static_cast<std::uint64_t>(count) <=
                    maximum_stereo_samples,
                SAP2_PROBE_UNSUPPORTED);
        for (int channel = 0; channel < 2; ++channel) {
            const auto* samples = reinterpret_cast<const float*>(data[channel]);
            auto& peak = channel == 0 ? report_.left_peak : report_.right_peak;
            auto& sum = channel == 0 ? report_.left_mean : report_.right_mean;
            for (int sample = 0; sample < count; ++sample) {
                require(std::isfinite(samples[sample]));
                peak = std::max(peak, std::abs(static_cast<double>(samples[sample])));
                sum += samples[sample];
            }
        }
        report_.stereo_samples += static_cast<std::uint64_t>(count);
        void* planes[]{data[0], data[1]};
        require(av_audio_fifo_write(fifo_.get(), planes, count) == count);
        while (av_audio_fifo_size(fifo_.get()) >= codec_->frame_size) {
            send_frame(codec_->frame_size);
        }
    }
    void finish() {
        const auto remainder = av_audio_fifo_size(fifo_.get());
        if (remainder) {
            send_frame(remainder);
        }
        check_cancel(source_);
        checked(avcodec_send_frame(codec_.get(), nullptr));
        drain();
        checked(avcodec_send_packet(verification_.get(), nullptr));
        verify_packets();
        require(report_.verified_aac_samples > 0);
        require(report_.stereo_samples != 0, SAP2_PROBE_MALFORMED);
        report_.left_mean /= static_cast<double>(report_.stereo_samples);
        report_.right_mean /= static_cast<double>(report_.stereo_samples);
    }

private:
    void verify_packets() {
        for (;;) {
            check_cancel(source_);
            const auto result =
                avcodec_receive_frame(verification_.get(), verification_frame_.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            checked(result);
            require(verification_frame_->ch_layout.nb_channels == 2 &&
                    verification_frame_->sample_rate == output_rate);
            report_.verified_aac_samples +=
                static_cast<std::uint64_t>(verification_frame_->nb_samples);
            av_frame_unref(verification_frame_.get());
        }
    }
    void send_frame(int samples) {
        check_cancel(source_);
        checked(av_frame_make_writable(frame_.get()));
        checked(av_samples_set_silence(frame_->data, 0, codec_->frame_size, 2, AV_SAMPLE_FMT_FLTP));
        void* planes[]{frame_->data[0], frame_->data[1]};
        require(av_audio_fifo_read(fifo_.get(), planes, samples) == samples);
        frame_->pts = static_cast<std::int64_t>(report_.encoded_samples);
        checked(avcodec_send_frame(codec_.get(), frame_.get()));
        report_.encoded_samples += static_cast<std::uint64_t>(codec_->frame_size);
        drain();
    }
    void drain() {
        for (;;) {
            check_cancel(source_);
            const auto result = avcodec_receive_packet(codec_.get(), packet_.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            checked(result);
            require(packet_->size > 0);
            checked(avcodec_send_packet(verification_.get(), packet_.get()));
            verify_packets();
            require(source_.write_packet(source_.context, packet_->data,
                                         static_cast<std::size_t>(packet_->size)) != 0,
                    SAP2_PROBE_OUTPUT_ERROR);
            ++report_.packet_count;
            report_.packet_bytes += static_cast<std::uint64_t>(packet_->size);
            av_packet_unref(packet_.get());
        }
    }
    const sap2_probe_source& source_;
    sap2_probe_report& report_;
    Codec codec_;
    Frame frame_;
    Packet packet_;
    Codec verification_;
    Frame verification_frame_;
    Fifo fifo_;
};

void convert(AVCodecContext& decoder, AVFormatContext& format, int stream_index,
             const sap2_probe_source& source, sap2_probe_report& report) {
    Frame frame(av_frame_alloc());
    Packet packet(av_packet_alloc());
    require(frame && packet);
    Encoder encoder(source, report);
    Resampler resampler;
    std::uint64_t input_channel_mask = 0;
    int input_sample_format = AV_SAMPLE_FMT_NONE;
    std::array<std::vector<float>, 2> stereo;
    std::array<std::uint8_t*, 2> output{};
    auto consume = [&](const AVFrame* input) {
        check_cancel(source);
        if (input && !resampler) {
            require(input->sample_rate >= 8000 && input->sample_rate <= 192000,
                    SAP2_PROBE_UNSUPPORTED);
            const auto matrix = stereo_matrix(input->ch_layout);
            SwrContext* raw = nullptr;
            const AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
            checked(swr_alloc_set_opts2(
                &raw, &layout, AV_SAMPLE_FMT_FLTP, output_rate, &input->ch_layout,
                static_cast<AVSampleFormat>(input->format), input->sample_rate, 0, nullptr));
            resampler.reset(raw);
            checked(swr_set_matrix(resampler.get(), matrix.data(), maximum_channels));
            checked(swr_init(resampler.get()));
            report.input_channels = static_cast<std::uint32_t>(input->ch_layout.nb_channels);
            report.input_sample_rate = static_cast<std::uint32_t>(input->sample_rate);
            input_channel_mask = input->ch_layout.u.mask;
            input_sample_format = input->format;
        }
        if (!resampler) {
            return 0;
        }
        if (input) {
            require(input->nb_samples >= 0 && input->nb_samples <= maximum_frame_samples);
            require(input->sample_rate == static_cast<int>(report.input_sample_rate) &&
                        input->ch_layout.order == AV_CHANNEL_ORDER_NATIVE &&
                        input->ch_layout.u.mask == input_channel_mask &&
                        input->format == input_sample_format,
                    SAP2_PROBE_UNSUPPORTED);
            report.decoded_samples += static_cast<std::uint64_t>(input->nb_samples);
        }
        const auto capacity = swr_get_out_samples(resampler.get(), input ? input->nb_samples : 0);
        require(capacity >= 0 && capacity <= maximum_frame_samples);
        for (int channel = 0; channel < 2; ++channel) {
            stereo[channel].resize(static_cast<std::size_t>(std::max(1, capacity)));
            output[channel] = reinterpret_cast<std::uint8_t*>(stereo[channel].data());
        }
        const auto count =
            swr_convert(resampler.get(), output.data(), capacity,
                        input ? const_cast<const std::uint8_t**>(input->extended_data) : nullptr,
                        input ? input->nb_samples : 0);
        checked(count);
        encoder.append(output.data(), count);
        return count;
    };
    auto drain = [&] {
        for (;;) {
            check_cancel(source);
            const auto result = avcodec_receive_frame(&decoder, frame.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            checked(result);
            consume(frame.get());
            av_frame_unref(frame.get());
        }
    };
    for (;;) {
        check_cancel(source);
        const auto result = av_read_frame(&format, packet.get());
        check_cancel(source);
        if (result == AVERROR_EOF) {
            break;
        }
        require(result >= 0, SAP2_PROBE_MALFORMED);
        if (packet->stream_index == stream_index) {
            checked(avcodec_send_packet(&decoder, packet.get()));
            drain();
        }
        av_packet_unref(packet.get());
    }
    checked(avcodec_send_packet(&decoder, nullptr));
    drain();
    while (consume(nullptr) != 0) {
        // Drain delayed resampler samples before padding the final encoder frame.
    }
    encoder.finish();
    report.output_channels = 2;
    report.output_sample_rate = output_rate;
}

sap2_probe_report run(const sap2_probe_source& source) {
    require(sap2_codec_probe_build_ok() != 0, SAP2_PROBE_BUILD_ERROR);
    check_cancel(source);
    Reader reader{source};
    auto* buffer = static_cast<std::uint8_t*>(av_malloc(io_buffer_bytes));
    require(buffer != nullptr);
    Io io(avio_alloc_context(buffer, io_buffer_bytes, 0, &reader, Reader::read, nullptr,
                             Reader::seek));
    if (!io) {
        av_free(buffer);
        throw ProbeError{SAP2_PROBE_CODEC_ERROR};
    }
    Format format(avformat_alloc_context());
    require(format != nullptr);
    format->pb = io.get();
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    format->interrupt_callback = AVIOInterruptCB{Reader::interrupt, &reader};
    format->probesize = 65536;
    format->max_analyze_duration = AV_TIME_BASE;
    auto* raw_format = format.release();
    const auto opened = avformat_open_input(&raw_format, nullptr, nullptr, nullptr);
    format.reset(raw_format);
    check_cancel(source);
    require(opened >= 0 && format, SAP2_PROBE_MALFORMED);
    const auto found = avformat_find_stream_info(format.get(), nullptr);
    check_cancel(source);
    require(found >= 0 && !reader.failed, SAP2_PROBE_MALFORMED);
    require(format->nb_streams <= 16, SAP2_PROBE_UNSUPPORTED);
    int stream_index = -1;
    for (unsigned index = 0; index < format->nb_streams; ++index) {
        if (format->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            require(stream_index == -1, SAP2_PROBE_UNSUPPORTED);
            stream_index = static_cast<int>(index);
        }
    }
    require(stream_index >= 0, SAP2_PROBE_UNSUPPORTED);
    const auto* parameters = format->streams[stream_index]->codecpar;
    const auto* implementation = avcodec_find_decoder(parameters->codec_id);
    require(implementation != nullptr, SAP2_PROBE_UNSUPPORTED);
    Codec decoder(avcodec_alloc_context3(implementation));
    require(decoder != nullptr);
    checked(avcodec_parameters_to_context(decoder.get(), parameters));
    if (parameters->codec_id == AV_CODEC_ID_AC3 || parameters->codec_id == AV_CODEC_ID_EAC3) {
        // Dolby decoders otherwise apply metadata-driven DRC by default. Keep
        // compression out of the spike's explicit downmix policy.
        checked(av_opt_set_double(decoder->priv_data, "drc_scale", 0.0, 0));
    }
    checked(avcodec_open2(decoder.get(), implementation, nullptr));
    sap2_probe_report report{};
    report.struct_size = sizeof report;
    convert(*decoder, *format, stream_index, source, report);
    require(!reader.failed, SAP2_PROBE_MALFORMED);
    return report;
}
} // namespace

int sap2_codec_probe_build_ok(void) {
    const std::string_view configuration(avcodec_configuration());
    const std::string_view license(avcodec_license());
    return license == "LGPL version 2.1 or later" && license == avformat_license() &&
           license == avutil_license() && license == swresample_license() &&
           configuration == avformat_configuration() && configuration == avutil_configuration() &&
           configuration == swresample_configuration() &&
           configuration.find("--disable-gpl") != std::string_view::npos &&
           configuration.find("--disable-nonfree") != std::string_view::npos &&
           configuration.find("--disable-network") != std::string_view::npos &&
           avcodec_find_encoder(AV_CODEC_ID_AAC) && avcodec_find_decoder(AV_CODEC_ID_AAC) &&
           avcodec_find_decoder(AV_CODEC_ID_AC3) && avcodec_find_decoder(AV_CODEC_ID_EAC3) &&
           avcodec_find_decoder(AV_CODEC_ID_DTS);
}

std::int32_t sap2_codec_probe(const sap2_probe_source* source, sap2_probe_report* report) {
    if (!source || !report || source->struct_size != sizeof *source ||
        report->struct_size != sizeof *report) {
        return SAP2_PROBE_INVALID;
    }
    *report = {};
    report->struct_size = sizeof *report;
    if (!source->read_at || !source->write_packet || source->size == 0 ||
        source->size > maximum_source_bytes) {
        return SAP2_PROBE_INVALID;
    }
    try {
        *report = run(*source);
        return SAP2_PROBE_OK;
    } catch (const ProbeError& error) {
        return error.result;
    } catch (...) {
        return SAP2_PROBE_CODEC_ERROR;
    }
}
