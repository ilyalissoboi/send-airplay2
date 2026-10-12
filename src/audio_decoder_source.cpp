// SPDX-License-Identifier: Apache-2.0
#include "audio_decoder_source.h"
#include "media_track_inventory.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t maximum_source_bytes = 1ULL << 40;
constexpr std::uint64_t maximum_read_bytes = 256ULL << 20;
constexpr std::size_t maximum_read_calls = 1'048'576;
constexpr int io_buffer_bytes = 32768;
constexpr int maximum_frame_samples = 65536;
constexpr int output_rate = AudioDecoderSource::output_rate;
constexpr std::uint32_t maximum_window_samples = 10 * AudioDecoderSource::output_rate;
constexpr std::uint64_t maximum_input_samples = 20 * AudioDecoderSource::output_rate;
constexpr std::uint64_t maximum_presentation_samples = 7ULL * 24 * 60 * 60 * output_rate;
constexpr AVRational output_clock{1, AudioDecoderSource::output_rate};
constexpr AVRational nanosecond_clock{1, 1'000'000'000};

[[noreturn]] void fail(AudioDecodeFailure reason, const char* message) {
    throw AudioDecodeException(reason, message);
}
void require(bool condition, AudioDecodeFailure reason, const char* message) {
    if (!condition) {
        fail(reason, message);
    }
}
void checked(int result) {
    require(result >= 0, AudioDecodeFailure::codec_error, "audio codec operation failed");
}
void check_stop(const MediaReadContext& context) {
    require(context.server_stopped && context.request_cancelled, AudioDecodeFailure::source_error,
            "audio source cancellation context is invalid");
    require(!context.should_stop(), AudioDecodeFailure::stopped,
            "audio source cancelled or expired");
}
template <typename T, void (*Free)(T**)> struct AvDeleter {
    void operator()(T* value) const noexcept {
        Free(&value);
    }
};
using Format = std::unique_ptr<AVFormatContext, AvDeleter<AVFormatContext, avformat_close_input>>;
using Codec = std::unique_ptr<AVCodecContext, AvDeleter<AVCodecContext, avcodec_free_context>>;
using Frame = std::unique_ptr<AVFrame, AvDeleter<AVFrame, av_frame_free>>;
using Packet = std::unique_ptr<AVPacket, AvDeleter<AVPacket, av_packet_free>>;
using Resampler = std::unique_ptr<SwrContext, AvDeleter<SwrContext, swr_free>>;
struct IoDeleter {
    void operator()(AVIOContext* value) const noexcept {
        if (value) {
            av_freep(&value->buffer);
            avio_context_free(&value);
        }
    }
};
using Io = std::unique_ptr<AVIOContext, IoDeleter>;

void validate_backend() {
    const std::string_view configuration(avcodec_configuration());
    const std::string_view license(avcodec_license());
    require(license == "LGPL version 2.1 or later" && license == avformat_license() &&
                license == avutil_license() && license == swresample_license() &&
                configuration == avformat_configuration() &&
                configuration == avutil_configuration() &&
                configuration == swresample_configuration() &&
                configuration.find("--disable-gpl") != std::string_view::npos &&
                configuration.find("--disable-nonfree") != std::string_view::npos &&
                configuration.find("--disable-network") != std::string_view::npos,
            AudioDecodeFailure::unsupported, "audio decoder backend profile is unsupported");
}

/// Contains exceptions inside FFmpeg callbacks; checked again on the C++ side.
struct Reader {
    MediaSource source;
    std::uint64_t size = 0;
    std::uint64_t position = 0;
    std::uint64_t requested_bytes = 0;
    std::size_t calls = 0;
    const MediaReadContext* context = nullptr;
    std::exception_ptr error;

    void check() const {
        if (error) {
            std::rethrow_exception(error);
        }
        require(context != nullptr, AudioDecodeFailure::source_error,
                "audio source operation has no context");
        check_stop(*context);
    }
    static int read(void* opaque, std::uint8_t* output, int capacity) noexcept {
        auto& reader = *static_cast<Reader*>(opaque);
        try {
            reader.check();
            require(capacity > 0, AudioDecodeFailure::source_error, "invalid decoder read");
            if (reader.position == reader.size) {
                return AVERROR_EOF;
            }
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                reader.size - reader.position, std::min(capacity, io_buffer_bytes)));
            require(++reader.calls <= maximum_read_calls &&
                        count <= maximum_read_bytes - reader.requested_bytes,
                    AudioDecodeFailure::too_large, "audio source read budget exceeded");
            reader.requested_bytes += count;
            const auto actual =
                reader.source.read_at(reader.position, output, count, *reader.context);
            reader.check();
            require(actual > 0 && actual <= count, AudioDecodeFailure::source_error,
                    "audio source returned an invalid read length");
            reader.position += actual;
            return static_cast<int>(actual);
        } catch (...) {
            reader.error = std::current_exception();
            return AVERROR(EIO);
        }
    }
    static std::int64_t seek(void* opaque, std::int64_t offset, int whence) noexcept {
        auto& reader = *static_cast<Reader*>(opaque);
        try {
            reader.check();
            if ((whence & ~AVSEEK_FORCE) == AVSEEK_SIZE) {
                return static_cast<std::int64_t>(reader.size);
            }
            const auto origin = whence & ~AVSEEK_FORCE;
            const auto base = origin == SEEK_CUR   ? static_cast<std::int64_t>(reader.position)
                              : origin == SEEK_END ? static_cast<std::int64_t>(reader.size)
                                                   : 0;
            require(origin == SEEK_SET || origin == SEEK_CUR || origin == SEEK_END,
                    AudioDecodeFailure::source_error, "invalid decoder seek origin");
            require(offset >= -base && offset <= static_cast<std::int64_t>(reader.size) - base,
                    AudioDecodeFailure::source_error, "decoder seek outside source");
            reader.position = static_cast<std::uint64_t>(base + offset);
            return static_cast<std::int64_t>(reader.position);
        } catch (...) {
            reader.error = std::current_exception();
            return AVERROR(EIO);
        }
    }
    static int interrupt(void* opaque) noexcept {
        try {
            static_cast<Reader*>(opaque)->check();
            return 0;
        } catch (...) {
            return 1;
        }
    }
};
/// Lifetime is shorter than Reader; all owners clean up with a live Reader.
struct Operation {
    Reader& reader;
    Operation(Reader& input, const MediaReadContext& context) : reader(input) {
        check_stop(context);
        reader.context = &context;
        reader.error = {};
        reader.calls = 0;
        reader.requested_bytes = 0;
    }
    ~Operation() {
        reader.context = nullptr;
    }
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;
    Operation(Operation&&) = delete;
    Operation& operator=(Operation&&) = delete;
};

AVCodecID source_codec(const SourceTrack& track, SourceContainer container) {
    const auto& name = track.codec;
    if (container == SourceContainer::mp4) {
        if (name == "mp4a") {
            // MPEG-4 systems objectTypeIndication, never a fourcc-only guess.
            constexpr std::uint8_t mpeg4_audio = 0x40;
            constexpr std::uint8_t ac3_audio = 0xA5;
            constexpr std::uint8_t eac3_audio = 0xA6;
            constexpr std::uint8_t dts_core_audio = 0xA9;
            const auto type = mp4_audio_object_type(track.configuration);
            if (type == mpeg4_audio) {
                return AV_CODEC_ID_AAC;
            }
            if (type == ac3_audio) {
                return AV_CODEC_ID_AC3;
            }
            if (type == eac3_audio) {
                return AV_CODEC_ID_EAC3;
            }
            if (type == dts_core_audio) {
                return AV_CODEC_ID_DTS;
            }
            fail(AudioDecodeFailure::unsupported, "selected mp4a object type is unsupported");
        }
        if (name == "ac-3") {
            return AV_CODEC_ID_AC3;
        }
        if (name == "ec-3") {
            return AV_CODEC_ID_EAC3;
        }
        if (name == "dtsc") {
            return AV_CODEC_ID_DTS;
        }
    } else {
        if (name == "A_AAC") {
            return AV_CODEC_ID_AAC;
        }
        if (name == "A_AC3") {
            return AV_CODEC_ID_AC3;
        }
        if (name == "A_EAC3") {
            return AV_CODEC_ID_EAC3;
        }
        if (name == "A_DTS") {
            return AV_CODEC_ID_DTS;
        }
    }
    fail(AudioDecodeFailure::unsupported, "selected audio codec is unsupported");
}
void validate_timeline(const SourceTrack& track, SourceContainer container) {
    require(!track.encoded && track.standard_timeline, AudioDecodeFailure::unsupported,
            "selected audio protection or timeline is unsupported");
    if (container == SourceContainer::mp4) {
        require(track.sample_description_count == 1 && track.timescale > 0,
                AudioDecodeFailure::unsupported, "selected audio description is unsupported");
        require(track.edits.size() <= 2, AudioDecodeFailure::unsupported,
                "selected audio edit sequence is unsupported");
        for (std::size_t index = 0; index < track.edits.size(); ++index) {
            const auto& edit = track.edits[index];
            const bool empty = track.edits.size() == 2 && index == 0;
            require(edit.rate_integer == 1 && edit.rate_fraction == 0 &&
                        (empty ? edit.media_time == -1 : edit.media_time >= 0),
                    AudioDecodeFailure::unsupported, "selected audio edit is unsupported");
        }
    }
}
std::uint64_t decoder_track_id(const AVStream& stream, SourceContainer container) {
    if (container == SourceContainer::mp4) {
        // MOV reads the unsigned tkhd ID into its signed int field.
        return static_cast<std::uint32_t>(stream.id);
    }
    const auto* value = av_dict_get(stream.metadata, "sap2_matroska_track_number", nullptr, 0);
    require(value && value->value, AudioDecodeFailure::unsupported,
            "Matroska decoder lacks original track identity metadata");
    const std::string_view text(value->value);
    std::uint64_t id = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id, 16);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && id != 0,
            AudioDecodeFailure::malformed, "invalid Matroska decoder track identity");
    return id;
}
int match_stream(const AVFormatContext& format, const SourceTrack& selected,
                 SourceContainer container) {
    require(format.nb_streams <= max_source_tracks, AudioDecodeFailure::too_large,
            "decoder has too many streams");
    int matched = -1;
    for (unsigned index = 0; index < format.nb_streams; ++index) {
        const auto& stream = *format.streams[index];
        // Attached pictures/data are not audio identities; no ordinal fallback.
        if (stream.codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
            continue;
        }
        if (decoder_track_id(stream, container) == selected.id) {
            require(matched == -1, AudioDecodeFailure::malformed,
                    "duplicate selected decoder identity");
            matched = static_cast<int>(index);
        }
    }
    require(matched >= 0, AudioDecodeFailure::invalid_selection,
            "selected audio has no decoder stream");
    require(format.streams[matched]->codecpar->codec_id == source_codec(selected, container),
            AudioDecodeFailure::unsupported, "selected source and decoder codecs disagree");
    return matched;
}
struct InputAudioFormat {
    int rate = 0;
    int sample_format = -1;
    int channels = 0;
    std::uint64_t channel_mask = 0;
};
} // namespace

struct AudioDecoderSource::Impl {
    Reader reader; // Outlives every FFmpeg owner which refers to it.
    Io io;
    Format format;
    Codec codec;
    SourceTrack selected;
    SourceContainer container = SourceContainer::mp4;
    int stream_index = -1;
    bool usable = true;
    InputAudioFormat input_format;

    Impl(const MediaSource& source, std::uint64_t size) : reader{source, size} {}
    void open_format() {
        auto* buffer = static_cast<std::uint8_t*>(av_malloc(io_buffer_bytes));
        require(buffer != nullptr, AudioDecodeFailure::codec_error, "audio I/O allocation failed");
        io.reset(avio_alloc_context(buffer, io_buffer_bytes, 0, &reader, Reader::read, nullptr,
                                    Reader::seek));
        if (!io) {
            av_free(buffer);
            fail(AudioDecodeFailure::codec_error, "audio I/O context allocation failed");
        }
        format.reset(avformat_alloc_context());
        require(format != nullptr, AudioDecodeFailure::codec_error,
                "audio format allocation failed");
        format->pb = io.get();
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        format->interrupt_callback = AVIOInterruptCB{Reader::interrupt, &reader};
        format->probesize = 1 << 20;
        format->max_streams = static_cast<int>(max_source_tracks);
        format->max_analyze_duration = AV_TIME_BASE;
        const auto* demuxer =
            av_find_input_format(container == SourceContainer::mp4 ? "mov" : "matroska");
        require(demuxer != nullptr, AudioDecodeFailure::unsupported, "audio demuxer unavailable");
        auto* raw_format = format.release();
        const auto opened = avformat_open_input(&raw_format, nullptr, demuxer, nullptr);
        format.reset(raw_format);
        reader.check();
        require(opened >= 0 && format, AudioDecodeFailure::malformed,
                "audio container open failed");
        stream_index = match_stream(*format, selected, container);
        discard_other_streams();
        const auto found = avformat_find_stream_info(format.get(), nullptr);
        reader.check();
        require(found >= 0, AudioDecodeFailure::malformed, "audio stream probe failed");
    }
    void open_codec() {
        stream_index = match_stream(*format, selected, container);
        const auto* parameters = format->streams[stream_index]->codecpar;
        const auto* decoder = avcodec_find_decoder(parameters->codec_id);
        require(decoder != nullptr, AudioDecodeFailure::unsupported, "audio decoder unavailable");
        codec.reset(avcodec_alloc_context3(decoder));
        require(codec != nullptr, AudioDecodeFailure::codec_error,
                "audio decoder allocation failed");
        checked(avcodec_parameters_to_context(codec.get(), parameters));
        // MOV/MKV already publish edited/delay-adjusted PTS and packet skip data.
        // Decoder preinit must not independently skip codecpar padding again.
        codec->initial_padding = 0;
        codec->err_recognition = AV_EF_EXPLODE;
        codec->pkt_timebase = format->streams[stream_index]->time_base;
        if (parameters->codec_id == AV_CODEC_ID_AC3 || parameters->codec_id == AV_CODEC_ID_EAC3) {
            checked(av_opt_set_double(codec->priv_data, "drc_scale", 0.0, 0));
        }
        checked(avcodec_open2(codec.get(), decoder, nullptr));
        input_format.rate = parameters->sample_rate;
        input_format.sample_format = parameters->format;
        input_format.channels = parameters->ch_layout.nb_channels;
        if (parameters->ch_layout.order == AV_CHANNEL_ORDER_NATIVE) {
            input_format.channel_mask = parameters->ch_layout.u.mask;
        }
    }
    void discard_other_streams() {
        for (unsigned index = 0; index < format->nb_streams; ++index) {
            format->streams[index]->discard =
                static_cast<int>(index) == stream_index ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
        }
    }
    void seek(std::uint64_t begin) {
        const auto* stream = format->streams[stream_index];
        const auto metadata_preroll = av_rescale_q(
            static_cast<std::int64_t>(selected.seek_preroll_ns), nanosecond_clock, output_clock);
        const auto preroll = std::max<std::int64_t>(output_rate, metadata_preroll);
        const auto raw_target = static_cast<std::int64_t>(begin) - preroll;
        // MOV's negative AAC packet carries the overlap/priming history at start.
        const auto target =
            container == SourceContainer::mp4 ? raw_target : std::max<std::int64_t>(0, raw_target);
        const auto timestamp = av_rescale_q(target, output_clock, stream->time_base);
        const auto result =
            avformat_seek_file(format.get(), stream_index, std::numeric_limits<std::int64_t>::min(),
                               timestamp, timestamp, AVSEEK_FLAG_BACKWARD);
        reader.check();
        require(result >= 0, AudioDecodeFailure::unsupported, "selected audio seek failed");
        avcodec_flush_buffers(codec.get());
    }
};

namespace {
/// One window has fresh resampler history, rebuilt by decoding preroll.
class WindowDecoder {
public:
    WindowDecoder(AVCodecContext& codec, AVStream& stream, Reader& reader,
                  InputAudioFormat& input_format, DecodedAudioWindow& output, std::uint32_t count)
        : codec_(codec), stream_(stream), reader_(reader), expected_format_(input_format),
          output_(output), count_(count), frame_(av_frame_alloc()), packet_(av_packet_alloc()) {
        require(frame_ && packet_, AudioDecodeFailure::codec_error,
                "audio window allocation failed");
        const auto quantum = av_rescale_q_rnd(1, stream.time_base, output_clock, AV_ROUND_UP);
        require(quantum > 0 && quantum <= output_rate, AudioDecodeFailure::unsupported,
                "audio timestamp clock is unsupported");
        output_.timestamp_precision_samples = static_cast<std::uint32_t>(quantum);
    }
    void run(AVFormatContext& format) {
        std::size_t packets = 0;
        while (!complete_) {
            reader_.check();
            const auto read = av_read_frame(&format, packet_.get());
            reader_.check();
            if (read == AVERROR_EOF) {
                checked(avcodec_send_packet(&codec_, nullptr));
                drain();
                flush_resampler();
                output_.eof = true;
                break;
            }
            require(read >= 0, AudioDecodeFailure::malformed, "audio packet read failed");
            require(++packets <= maximum_read_calls, AudioDecodeFailure::too_large,
                    "audio packet budget exceeded");
            if (packet_->stream_index == stream_.index) {
                checked(avcodec_send_packet(&codec_, packet_.get()));
                drain();
            }
            av_packet_unref(packet_.get());
        }
        require(output_.channels != 0, AudioDecodeFailure::malformed,
                "audio source has no decoded frames");
        const auto frames = covered_ <= static_cast<std::int64_t>(output_.first_sample)
                                ? 0
                                : std::min<std::uint64_t>(count_, covered_ - output_.first_sample);
        output_.samples.resize(static_cast<std::size_t>(frames) * output_.channels);
    }
    WindowDecoder(const WindowDecoder&) = delete;
    WindowDecoder& operator=(const WindowDecoder&) = delete;
    WindowDecoder(WindowDecoder&&) = delete;
    WindowDecoder& operator=(WindowDecoder&&) = delete;

private:
    AVCodecContext& codec_;
    AVStream& stream_;
    Reader& reader_;
    InputAudioFormat& expected_format_;
    DecodedAudioWindow& output_;
    std::uint32_t count_;
    Frame frame_;
    Packet packet_;
    Resampler resampler_;
    int input_rate_ = 0;
    int input_format_ = -1;
    std::uint64_t input_mask_ = 0;
    std::uint64_t input_samples_ = 0;
    std::int64_t origin_ = 0;
    std::int64_t next_output_ = 0;
    std::int64_t covered_ = 0;
    bool complete_ = false;

    void configure(const AVFrame& frame, std::int64_t timestamp) {
        require(frame.ch_layout.order == AV_CHANNEL_ORDER_NATIVE &&
                    av_channel_layout_check(&frame.ch_layout) && frame.ch_layout.nb_channels >= 1 &&
                    frame.ch_layout.nb_channels <= 8 && frame.sample_rate >= 8000 &&
                    frame.sample_rate <= 192000,
                AudioDecodeFailure::unsupported, "audio frame layout or rate is unsupported");
        input_rate_ = frame.sample_rate;
        input_format_ = frame.format;
        input_mask_ = frame.ch_layout.u.mask;
        require((expected_format_.rate == 0 || expected_format_.rate == input_rate_) &&
                    (expected_format_.sample_format < 0 ||
                     expected_format_.sample_format == input_format_) &&
                    (expected_format_.channels == 0 ||
                     expected_format_.channels == frame.ch_layout.nb_channels) &&
                    (expected_format_.channel_mask == 0 ||
                     expected_format_.channel_mask == input_mask_),
                AudioDecodeFailure::unsupported,
                "selected audio format changed across seek epochs");
        expected_format_ = {input_rate_, input_format_, frame.ch_layout.nb_channels, input_mask_};
        output_.channels = static_cast<std::uint32_t>(frame.ch_layout.nb_channels);
        output_.channel_mask = input_mask_;
        output_.samples.assign(static_cast<std::size_t>(count_) * output_.channels, 0.0F);
        origin_ = next_output_ = timestamp;
        SwrContext* raw = nullptr;
        auto layout = frame.ch_layout; // Native mask layout has no owned map.
        const auto allocated = swr_alloc_set_opts2(
            &raw, &layout, AV_SAMPLE_FMT_FLT, output_rate, &layout,
            static_cast<AVSampleFormat>(frame.format), frame.sample_rate, 0, nullptr);
        resampler_.reset(raw);
        checked(allocated);
        require(resampler_ != nullptr, AudioDecodeFailure::codec_error,
                "resampler allocation failed");
        checked(swr_init(resampler_.get()));
        align_resampler_phase(frame);
    }
    /// Start each epoch on the same rational input/output phase as presentation
    /// sample zero. Dummy history is discarded during preroll, never a seek offset.
    void align_resampler_phase(const AVFrame& frame) {
        const auto period = input_rate_ / std::gcd(input_rate_, output_rate);
        require(period <= 8192, AudioDecodeFailure::unsupported,
                "audio resampling phase period is unsupported");
        const auto input_position =
            av_rescale_q(frame.pts, stream_.time_base, AVRational{1, input_rate_});
        const auto padding = static_cast<int>((input_position % period + period) % period);
        next_output_ =
            av_rescale_q(input_position - padding, AVRational{1, input_rate_}, output_clock);
        if (!padding) {
            return;
        }
        Frame silence(av_frame_alloc());
        require(silence != nullptr, AudioDecodeFailure::codec_error,
                "audio phase allocation failed");
        silence->format = input_format_;
        silence->sample_rate = input_rate_;
        silence->nb_samples = padding;
        checked(av_channel_layout_copy(&silence->ch_layout, &frame.ch_layout));
        checked(av_frame_get_buffer(silence.get(), 0));
        checked(av_samples_set_silence(silence->extended_data, 0, padding,
                                       static_cast<int>(output_.channels),
                                       static_cast<AVSampleFormat>(input_format_)));
        convert(silence->extended_data, padding);
    }
    void consume(const AVFrame& frame) {
        reader_.check();
        require(frame.nb_samples > 0 && frame.nb_samples <= maximum_frame_samples &&
                    frame.pts != AV_NOPTS_VALUE,
                AudioDecodeFailure::unsupported, "audio frame timestamp or length is unsupported");
        if (codec_.codec_id == AV_CODEC_ID_DTS) {
            require(codec_.profile == AV_PROFILE_DTS, AudioDecodeFailure::unsupported,
                    "DTS input is outside the tested core profile");
        }
        const auto timestamp = av_rescale_q(frame.pts, stream_.time_base, output_clock);
        require(timestamp >= -static_cast<std::int64_t>(maximum_input_samples) &&
                    timestamp <= static_cast<std::int64_t>(maximum_presentation_samples),
                AudioDecodeFailure::unsupported,
                "audio frame timestamp exceeds presentation bounds");
        if (!resampler_) {
            configure(frame, timestamp);
        }
        require(frame.sample_rate == input_rate_ && frame.format == input_format_ &&
                    frame.ch_layout.order == AV_CHANNEL_ORDER_NATIVE &&
                    frame.ch_layout.u.mask == input_mask_ &&
                    frame.ch_layout.nb_channels == static_cast<int>(output_.channels),
                AudioDecodeFailure::unsupported, "audio format changed during decoding");
        const auto expected = origin_ + av_rescale_q(static_cast<std::int64_t>(input_samples_),
                                                     AVRational{1, input_rate_}, output_clock);
        const auto tolerance = static_cast<std::int64_t>(output_.timestamp_precision_samples) + 1;
        require(timestamp >= expected - tolerance && timestamp <= expected + tolerance,
                AudioDecodeFailure::unsupported, "audio presentation timeline is discontinuous");
        input_samples_ += static_cast<std::uint64_t>(frame.nb_samples);
        require(av_rescale_q(static_cast<std::int64_t>(input_samples_), AVRational{1, input_rate_},
                             output_clock) <= static_cast<std::int64_t>(maximum_input_samples),
                AudioDecodeFailure::too_large, "audio window preroll budget exceeded");
        convert(frame.extended_data, frame.nb_samples);
    }
    void convert(std::uint8_t* const* input, int samples) {
        const auto capacity = swr_get_out_samples(resampler_.get(), samples);
        require(capacity >= 0 && capacity <= 4 * maximum_frame_samples,
                AudioDecodeFailure::too_large, "audio resampler output is too large");
        std::vector<float> pcm(static_cast<std::size_t>(capacity) * output_.channels);
        auto* data = reinterpret_cast<std::uint8_t*>(pcm.data());
        const auto actual = swr_convert(resampler_.get(), &data, capacity,
                                        const_cast<const std::uint8_t**>(input), samples);
        checked(actual);
        copy_pcm(pcm, actual);
    }
    void copy_pcm(const std::vector<float>& pcm, int frames) {
        require(std::all_of(pcm.begin(),
                            pcm.begin() + static_cast<std::size_t>(frames) * output_.channels,
                            [](float sample) { return std::isfinite(sample); }),
                AudioDecodeFailure::malformed, "audio decoder returned nonfinite PCM");
        const auto begin = static_cast<std::int64_t>(output_.first_sample);
        const auto end = begin + count_;
        const auto frame_end = next_output_ + frames;
        const auto copy_begin = std::max(begin, next_output_);
        const auto copy_end = std::min(end, frame_end);
        if (copy_end > copy_begin) {
            const auto input_offset =
                static_cast<std::size_t>(copy_begin - next_output_) * output_.channels;
            const auto output_offset =
                static_cast<std::size_t>(copy_begin - begin) * output_.channels;
            const auto count = static_cast<std::size_t>(copy_end - copy_begin) * output_.channels;
            std::copy_n(pcm.begin() + input_offset, count, output_.samples.begin() + output_offset);
        }
        covered_ = std::max(covered_, frame_end);
        next_output_ = frame_end;
        complete_ = covered_ >= end;
    }
    void drain() {
        for (;;) {
            reader_.check();
            const auto result = avcodec_receive_frame(&codec_, frame_.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            checked(result);
            consume(*frame_);
            av_frame_unref(frame_.get());
        }
    }
    void flush_resampler() {
        if (resampler_ && !complete_) {
            convert(nullptr, 0);
        }
    }
};
} // namespace

AudioDecoderSource::AudioDecoderSource(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AudioDecoderSource::~AudioDecoderSource() = default;

std::unique_ptr<AudioDecoderSource>
AudioDecoderSource::open(const MediaSource& source, const MediaReadContext& context,
                         std::optional<std::uint64_t> explicit_track_id) {
    require(source.size && source.read_at, AudioDecodeFailure::source_error,
            "audio source callbacks missing");
    validate_backend();
    check_stop(context);
    const auto size = source.size();
    check_stop(context);
    require(size > 0 && size <= maximum_source_bytes, AudioDecodeFailure::too_large,
            "audio source size is unsupported");
    auto snapshot = source;
    snapshot.size = [size] { return size; };
    const auto inventory = probe_media_tracks(snapshot, context);
    const auto selection = select_source_audio(inventory, explicit_track_id);
    require(selection.status != AudioSelectionStatus::selection_required,
            AudioDecodeFailure::selection_required, "source audio selection is required");
    require(selection.status == AudioSelectionStatus::selected,
            AudioDecodeFailure::invalid_selection, "selected source audio is unavailable");
    auto impl = std::make_unique<Impl>(snapshot, size);
    impl->selected = inventory.tracks[selection.track_index];
    impl->container = inventory.container;
    validate_timeline(impl->selected, inventory.container);
    (void)source_codec(impl->selected, inventory.container);
    require(impl->selected.seek_preroll_ns <= 10ULL * 1'000'000'000,
            AudioDecodeFailure::unsupported, "audio preroll metadata exceeds window budget");
    Operation operation(impl->reader, context);
    impl->open_format();
    impl->open_codec();
    impl->reader.check();
    return std::unique_ptr<AudioDecoderSource>(new AudioDecoderSource(std::move(impl)));
}

DecodedAudioWindow AudioDecoderSource::read_window(std::uint64_t first_sample, std::uint32_t count,
                                                   const MediaReadContext& context) {
    require(impl_->usable && count > 0 && count <= maximum_window_samples &&
                first_sample <= maximum_presentation_samples - count,
            AudioDecodeFailure::unsupported, "invalid audio window request or failed decoder");
    try {
        Operation operation(impl_->reader, context);
        impl_->seek(first_sample);
        DecodedAudioWindow window;
        window.source_track_id = impl_->selected.id;
        window.first_sample = first_sample;
        WindowDecoder decoder(*impl_->codec, *impl_->format->streams[impl_->stream_index],
                              impl_->reader, impl_->input_format, window, count);
        decoder.run(*impl_->format);
        impl_->reader.check();
        return window;
    } catch (...) {
        impl_->usable = false;
        throw;
    }
}
} // namespace send_airplay2::detail
