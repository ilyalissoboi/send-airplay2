// SPDX-License-Identifier: Apache-2.0
#include "stereo_aac_encoder.h"
#include "sample_entries.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
}
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t front_left = 1ULL << 0;
constexpr std::uint64_t front_right = 1ULL << 1;
constexpr std::uint64_t front_center = 1ULL << 2;
constexpr std::uint64_t low_frequency = 1ULL << 3;
constexpr std::uint64_t back_left = 1ULL << 4;
constexpr std::uint64_t back_right = 1ULL << 5;
constexpr std::uint64_t side_left = 1ULL << 9;
constexpr std::uint64_t side_right = 1ULL << 10;
constexpr std::uint64_t supported_speakers = front_left | front_right | front_center |
                                             low_frequency | back_left | back_right | side_left |
                                             side_right;
constexpr double surround_gain = 0.7071067811865475244;
constexpr double lfe_gain = 0.5;
constexpr std::size_t maximum_epoch_bytes = 1 << 20;
constexpr std::size_t maximum_packet_bytes = 8192;
constexpr std::uint32_t maximum_epoch_samples = 10 * AudioDecoderSource::output_rate;
void require(bool condition, AudioDecodeFailure reason, const char* message) {
    if (!condition) {
        throw AudioDecodeException(reason, message);
    }
}
void check_context(const MediaReadContext& context) {
    require(context.server_stopped && context.request_cancelled, AudioDecodeFailure::source_error,
            "AAC encoder context is invalid");
    require(!context.should_stop(), AudioDecodeFailure::stopped,
            "AAC encoding cancelled or expired");
}
void checked(int result) {
    require(result >= 0, AudioDecodeFailure::codec_error, "AAC encoder operation failed");
}
template <typename T, void (*Free)(T**)> struct AvDeleter {
    void operator()(T* value) const noexcept {
        Free(&value);
    }
};
using Codec = std::unique_ptr<AVCodecContext, AvDeleter<AVCodecContext, avcodec_free_context>>;
using Frame = std::unique_ptr<AVFrame, AvDeleter<AVFrame, av_frame_free>>;
using Packet = std::unique_ptr<AVPacket, AvDeleter<AVPacket, av_packet_free>>;

class EpochEncoder {
public:
    EpochEncoder(const DecodedAudioWindow& input, const MediaReadContext& context)
        : input_(input), context_(context),
          matrix_(stereo_downmix_matrix(input.channel_mask, input.channels)),
          codec_(avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_AAC))),
          frame_(av_frame_alloc()), packet_(av_packet_alloc()) {
        validate_audio_codec_backend();
        require(codec_ && frame_ && packet_, AudioDecodeFailure::codec_error,
                "AAC encoder allocation failed");
        codec_->sample_rate = AudioDecoderSource::output_rate;
        codec_->sample_fmt = AV_SAMPLE_FMT_FLTP;
        codec_->bit_rate = stereo_aac_bit_rate;
        codec_->profile = AV_PROFILE_AAC_LOW;
        codec_->time_base = AVRational{1, AudioDecoderSource::output_rate};
        codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        av_channel_layout_default(&codec_->ch_layout, 2);
        checked(avcodec_open2(codec_.get(), codec_->codec, nullptr));
        require(codec_->frame_size == static_cast<int>(stereo_aac_frame_samples) &&
                    codec_->initial_padding == static_cast<int>(stereo_aac_frame_samples) &&
                    codec_->extradata && codec_->extradata_size > 0 && codec_->extradata_size <= 64,
                AudioDecodeFailure::unsupported,
                "AAC encoder configuration is outside the tested profile");
        result_.audio_specific_config.assign(codec_->extradata,
                                             codec_->extradata + codec_->extradata_size);
        result_.priming_samples = static_cast<std::uint32_t>(codec_->initial_padding);
        const auto config = read_aac_config(result_.audio_specific_config);
        require(config.object_type == 2 && config.channels == 2 && config.sample_rate == 48000 &&
                    config.frame_samples == stereo_aac_frame_samples,
                AudioDecodeFailure::unsupported,
                "AAC encoder did not produce stereo LC configuration");
        frame_->format = codec_->sample_fmt;
        frame_->sample_rate = codec_->sample_rate;
        frame_->nb_samples = codec_->frame_size;
        checked(av_channel_layout_copy(&frame_->ch_layout, &codec_->ch_layout));
        checked(av_frame_get_buffer(frame_.get(), 0));
    }
    EpochEncoder(const EpochEncoder&) = delete;
    EpochEncoder& operator=(const EpochEncoder&) = delete;
    EpochEncoder(EpochEncoder&&) = delete;
    EpochEncoder& operator=(EpochEncoder&&) = delete;
    EncodedAacEpoch run(std::uint32_t sample_count) {
        for (std::uint32_t first = 0; first < sample_count; first += stereo_aac_frame_samples) {
            check_context(context_);
            checked(av_frame_make_writable(frame_.get()));
            mix_frame(first, std::min(stereo_aac_frame_samples, sample_count - first));
            frame_->pts = static_cast<std::int64_t>(input_.first_sample + first);
            checked(avcodec_send_frame(codec_.get(), frame_.get()));
            drain();
        }
        check_context(context_);
        checked(avcodec_send_frame(codec_.get(), nullptr));
        drain();
        return std::move(result_);
    }

private:
    const DecodedAudioWindow& input_;
    const MediaReadContext& context_;
    std::array<double, 16> matrix_;
    Codec codec_;
    Frame frame_;
    Packet packet_;
    EncodedAacEpoch result_;
    std::size_t bytes_ = 0;
    void mix_frame(std::uint32_t first, std::uint32_t valid) {
        auto* left = reinterpret_cast<float*>(frame_->data[0]);
        auto* right = reinterpret_cast<float*>(frame_->data[1]);
        const auto available = input_.samples.size() / input_.channels;
        for (std::uint32_t frame = 0; frame < stereo_aac_frame_samples; ++frame) {
            double mixed_left = 0;
            double mixed_right = 0;
            if (frame < valid && first + frame < available) {
                for (std::uint32_t channel = 0; channel < input_.channels; ++channel) {
                    const auto sample =
                        input_.samples[(static_cast<std::size_t>(first) + frame) * input_.channels +
                                       channel];
                    require(std::isfinite(sample), AudioDecodeFailure::malformed,
                            "AAC input PCM is nonfinite");
                    mixed_left += sample * matrix_[channel];
                    mixed_right += sample * matrix_[stereo_downmix_max_channels + channel];
                }
            }
            require(std::abs(mixed_left) <= 1.000001 && std::abs(mixed_right) <= 1.000001,
                    AudioDecodeFailure::unsupported, "AAC input exceeds normalized PCM headroom");
            left[frame] = static_cast<float>(mixed_left);
            right[frame] = static_cast<float>(mixed_right);
        }
    }
    void drain() {
        for (;;) {
            check_context(context_);
            const auto result = avcodec_receive_packet(codec_.get(), packet_.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            checked(result);
            require(packet_->pts != AV_NOPTS_VALUE &&
                        packet_->duration == stereo_aac_frame_samples && packet_->size > 0 &&
                        static_cast<std::size_t>(packet_->size) <= maximum_packet_bytes,
                    AudioDecodeFailure::unsupported, "AAC packet timing or size is unsupported");
            const auto size = static_cast<std::size_t>(packet_->size);
            require(size <= maximum_epoch_bytes - bytes_, AudioDecodeFailure::too_large,
                    "AAC epoch exceeds its encoded working budget");
            bytes_ += size;
            result_.packets.push_back({packet_->pts, Bytes(packet_->data, packet_->data + size)});
            av_packet_unref(packet_.get());
        }
    }
};
} // namespace

std::array<double, 16> stereo_downmix_matrix(std::uint64_t mask, std::uint32_t channels) {
    require(channels > 0 && channels <= stereo_downmix_max_channels && mask != 0 &&
                (mask & ~supported_speakers) == 0,
            AudioDecodeFailure::unsupported, "unsupported stereo downmix speaker layout");
    std::array<double, 16> result{};
    std::uint32_t index = 0;
    double left_sum = 0;
    double right_sum = 0;
    for (std::uint64_t speaker = 1; speaker <= side_right; speaker <<= 1) {
        if (!(mask & speaker)) {
            continue;
        }
        require(index < channels, AudioDecodeFailure::unsupported,
                "downmix mask/count disagreement");
        const double left = speaker == front_left      ? 1.0
                            : speaker == front_center  ? (channels == 1 ? 1.0 : surround_gain)
                            : speaker == low_frequency ? lfe_gain
                            : speaker == back_left || speaker == side_left ? surround_gain
                                                                           : 0.0;
        const double right = speaker == front_right     ? 1.0
                             : speaker == front_center  ? (channels == 1 ? 1.0 : surround_gain)
                             : speaker == low_frequency ? lfe_gain
                             : speaker == back_right || speaker == side_right ? surround_gain
                                                                              : 0.0;
        result[index] = left;
        result[stereo_downmix_max_channels + index] = right;
        left_sum += left;
        right_sum += right;
        ++index;
    }
    require(index == channels, AudioDecodeFailure::unsupported, "downmix mask/count disagreement");
    const auto headroom = std::max({1.0, left_sum, right_sum});
    for (auto& weight : result) {
        weight /= headroom;
    }
    return result;
}
EncodedAacEpoch encode_stereo_aac_epoch(const DecodedAudioWindow& input, std::uint32_t sample_count,
                                        const MediaReadContext& context) {
    check_context(context);
    require(sample_count > 0 && sample_count <= maximum_epoch_samples && input.channels > 0 &&
                input.samples.size() % input.channels == 0 &&
                input.samples.size() / input.channels <= sample_count &&
                (input.samples.size() / input.channels == sample_count || input.eof) &&
                input.first_sample % stereo_aac_frame_samples == 0 &&
                input.first_sample <=
                    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) -
                        sample_count,
            AudioDecodeFailure::unsupported, "invalid AAC epoch input bounds");
    return EpochEncoder(input, context).run(sample_count);
}
} // namespace send_airplay2::detail
