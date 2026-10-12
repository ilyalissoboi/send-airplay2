// SPDX-License-Identifier: Apache-2.0
#include "probe.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
}
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
void expect(bool condition, const std::string& scenario) {
    if (!condition) {
        throw std::runtime_error(scenario);
    }
}
void little(Bytes& bytes, std::uint32_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
    }
}
void tag(Bytes& bytes, const char* name) {
    bytes.insert(bytes.end(), name, name + 4);
}
/// Original WAVEFORMATEXTENSIBLE PCM fixture. Channel-mask bit order defines
/// interleaving; there is no dependency on the probe's matrix implementation.
Bytes wave(unsigned channels, std::uint32_t mask, int active_channel, unsigned samples = 4800,
           unsigned rate = 48000, std::uint16_t amplitude = 8192) {
    constexpr unsigned riff_header_bytes = 12;
    constexpr unsigned format_chunk_bytes = 8 + 40;
    constexpr unsigned data_chunk_header_bytes = 8;
    const auto payload_bytes = samples * channels * 2;
    const auto file_bytes =
        riff_header_bytes + format_chunk_bytes + data_chunk_header_bytes + payload_bytes;
    Bytes bytes;
    tag(bytes, "RIFF");
    little(bytes, file_bytes - 8, 4);
    tag(bytes, "WAVE");
    tag(bytes, "fmt ");
    little(bytes, 40, 4);
    little(bytes, 0xfffe, 2); // WAVE_FORMAT_EXTENSIBLE.
    little(bytes, channels, 2);
    little(bytes, rate, 4);
    little(bytes, rate * channels * 2, 4);
    little(bytes, channels * 2, 2);
    little(bytes, 16, 2);
    little(bytes, 22, 2); // cbSize.
    little(bytes, 16, 2); // Valid bits per sample.
    little(bytes, mask, 4);
    // KSDATAFORMAT_SUBTYPE_PCM, little-endian GUID fields.
    const std::uint8_t pcm_guid[]{1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
    bytes.insert(bytes.end(), std::begin(pcm_guid), std::end(pcm_guid));
    tag(bytes, "data");
    little(bytes, payload_bytes, 4);
    for (unsigned sample = 0; sample < samples; ++sample) {
        for (unsigned channel = 0; channel < channels; ++channel) {
            little(bytes,
                   active_channel < 0 || static_cast<int>(channel) == active_channel ? amplitude
                                                                                     : 0,
                   2);
        }
    }
    return bytes;
}

struct Source {
    explicit Source(Bytes data) : bytes(std::move(data)) {}
    Bytes bytes;
    Bytes output;
    std::vector<Bytes> packet_data;
    std::size_t read_limit = 97; // Deliberate short reads across every header and sample.
    bool cancelled = false;
    bool output_failure = false;
    bool read_failure = false;
    unsigned cancel_after_packets = 0;
    unsigned packets = 0;
    unsigned reads = 0;
    static std::size_t read(void* context, std::uint64_t offset, std::uint8_t* buffer,
                            std::size_t capacity) {
        auto& source = *static_cast<Source*>(context);
        ++source.reads;
        if (source.read_failure || offset >= source.bytes.size()) {
            return 0;
        }
        const auto count = std::min(
            {capacity, source.read_limit, source.bytes.size() - static_cast<std::size_t>(offset)});
        std::memcpy(buffer, source.bytes.data() + offset, count);
        return count;
    }
    static int stop(void* context) {
        return static_cast<Source*>(context)->cancelled ? 1 : 0;
    }
    static int write(void* context, const std::uint8_t* bytes, std::size_t count) {
        auto& source = *static_cast<Source*>(context);
        if (source.output_failure) {
            return 0;
        }
        source.output.insert(source.output.end(), bytes, bytes + count);
        source.packet_data.emplace_back(bytes, bytes + count);
        ++source.packets;
        if (source.cancel_after_packets && source.packets == source.cancel_after_packets) {
            source.cancelled = true;
        }
        return 1;
    }
    sap2_probe_source callbacks() {
        return sap2_probe_source{sizeof(sap2_probe_source), this, bytes.size(), read, stop, write};
    }
};
struct Result {
    std::int32_t code;
    sap2_probe_report report;
};
Result probe(Source& source) {
    auto callbacks = source.callbacks();
    sap2_probe_report report{};
    report.struct_size = sizeof report;
    const auto code = sap2_codec_probe(&callbacks, &report);
    return Result{code, report};
}
void successful(const Result& result, const Source& source, const std::string& scenario) {
    expect(result.code == SAP2_PROBE_OK, scenario + ": result=" + std::to_string(result.code));
    expect(result.report.output_channels == 2 && result.report.output_sample_rate == 48000,
           scenario + ": stereo output format");
    expect(result.report.packet_bytes == source.output.size() && result.report.packet_count > 0,
           scenario + ": packet cache accounting");
    expect(result.report.initial_padding == 1024, scenario + ": AAC initial padding changed");
    expect(result.report.verified_aac_samples > 0, scenario + ": encoded AAC decode verification");
}
void channel_mapping() {
    // Literal independently calculated normalized rows for FL/FR/FC/LFE/SL/SR.
    const double left_51[]{0.343145750508, 0, 0.242640687119, 0.171572875254, 0.242640687119, 0};
    const double right_51[]{0, 0.343145750508, 0.242640687119, 0.171572875254, 0, 0.242640687119};
    // FL/FR/FC/LFE/BL/BR/SL/SR, preserving the side/back distinction.
    const double left_71[]{0.276142374915, 0, 0.195262145876, 0.138071187458,
                           0.195262145876, 0, 0.195262145876, 0};
    const double right_71[]{0, 0.276142374915, 0.195262145876, 0.138071187458, 0, 0.195262145876,
                            0, 0.195262145876};
    for (const unsigned channels : {6u, 8u}) {
        for (unsigned channel = 0; channel < channels; ++channel) {
            Source source{wave(channels, channels == 6 ? 0x60f : 0x63f, static_cast<int>(channel))};
            const auto result = probe(source);
            const auto scenario =
                std::to_string(channels) + " channels, isolated index=" + std::to_string(channel);
            successful(result, source, scenario);
            const auto* left = channels == 6 ? left_51 : left_71;
            const auto* right = channels == 6 ? right_51 : right_71;
            expect(std::abs(result.report.left_mean - 0.25 * left[channel]) < 0.000001,
                   scenario + ": left contribution");
            expect(std::abs(result.report.right_mean - 0.25 * right[channel]) < 0.000001,
                   scenario + ": right contribution");
            expect(result.report.decoded_samples == 4800 && result.report.stereo_samples == 4800,
                   scenario + ": source duration");
        }
        Source correlated{wave(channels, channels == 6 ? 0x60f : 0x63f, -1, 4800, 48000, 31129)};
        const auto result = probe(correlated);
        successful(result, correlated, "correlated peak, channels=" + std::to_string(channels));
        expect(result.report.left_peak < 0.951 && result.report.right_peak < 0.951,
               "correlated full-scale inputs must retain headroom");
    }
}
void lifecycle_failures() {
    Source cancelled{wave(6, 0x60f, -1)};
    cancelled.cancelled = true;
    expect(probe(cancelled).code == SAP2_PROBE_CANCELLED && cancelled.reads == 0,
           "pre-cancel: no source read");
    Source mid_encode{wave(6, 0x60f, -1)};
    mid_encode.cancel_after_packets = 1;
    const auto cancelled_result = probe(mid_encode);
    expect(cancelled_result.code == SAP2_PROBE_CANCELLED && mid_encode.packets == 1 &&
               cancelled_result.report.packet_count == 0,
           "cancel during encoding: partial output is not successful report");
    Source output_error{wave(6, 0x60f, -1)};
    output_error.output_failure = true;
    expect(probe(output_error).code == SAP2_PROBE_OUTPUT_ERROR, "host output/cache failure");
    Source read_error{wave(6, 0x60f, -1)};
    read_error.read_failure = true;
    expect(probe(read_error).code == SAP2_PROBE_MALFORMED, "premature zero source read");
    Source malformed{Bytes(100, 0)};
    expect(probe(malformed).code == SAP2_PROBE_MALFORMED, "malformed media");
    Source unknown_layout{
        wave(3, 0x103, -1)}; // Front L/R and back center, deliberately unsupported.
    expect(probe(unknown_layout).code == SAP2_PROBE_UNSUPPORTED, "unimplemented layout identity");
}
void resampling_and_encoder_epochs() {
    Source source{wave(6, 0x60f, -1, 4410, 44100)};
    const auto result = probe(source);
    successful(result, source, "44.1 kHz resampling");
    expect(result.report.stereo_samples == 4800, "resampler drain preserves 100 ms");
    Source first{wave(6, 0x60f, -1, 2400)};
    Source second{wave(6, 0x60f, -1, 2400)};
    const auto first_result = probe(first);
    const auto second_result = probe(second);
    successful(first_result, first, "encoder epoch one");
    successful(second_result, second, "encoder epoch two");
    expect(first.output == second.output, "identical PCM epochs produce stable AAC packet bytes");
    expect(first_result.report.initial_padding + second_result.report.initial_padding == 2048,
           "two independent epochs each introduce AAC priming; cannot concatenate blindly");
}
void compressed_fixtures(const std::filesystem::path& directory) {
    for (const auto* name : {"aac-51.m4a", "ac3-51.mka", "eac3-51.mka", "dts-core-51.mka"}) {
        std::ifstream stream(directory / name, std::ios::binary);
        expect(stream.good(), std::string("fixture missing: ") + name);
        Source source{
            Bytes(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>())};
        const auto result = probe(source);
        successful(result, source, name);
        expect(result.report.input_channels == 6 && result.report.stereo_samples > 0,
               std::string(name) + ": six-channel decoder output");
    }
}

/// Independent decoder configured by literal AAC-LC/48 kHz/stereo ASC bytes,
/// rather than by the encoder's own configuration. Raw packets have no skip
/// metadata, so returned samples include the first encoder's 1024-sample priming.
std::vector<float> decode_packets(const std::vector<Bytes>& packets) {
    struct CodecFree {
        void operator()(AVCodecContext* value) const {
            avcodec_free_context(&value);
        }
    };
    struct FrameFree {
        void operator()(AVFrame* value) const {
            av_frame_free(&value);
        }
    };
    struct PacketFree {
        void operator()(AVPacket* value) const {
            av_packet_free(&value);
        }
    };
    std::unique_ptr<AVCodecContext, CodecFree> codec(
        avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_AAC)));
    std::unique_ptr<AVFrame, FrameFree> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketFree> packet(av_packet_alloc());
    expect(codec && frame && packet, "epoch decoder allocation");
    codec->extradata = static_cast<std::uint8_t*>(av_mallocz(2 + AV_INPUT_BUFFER_PADDING_SIZE));
    expect(codec->extradata != nullptr, "epoch ASC allocation");
    codec->extradata_size = 2;
    codec->extradata[0] = 0x11;
    codec->extradata[1] = 0x90;
    expect(avcodec_open2(codec.get(), codec->codec, nullptr) >= 0, "epoch decoder open");
    std::vector<float> samples;
    auto drain = [&] {
        for (;;) {
            const auto status = avcodec_receive_frame(codec.get(), frame.get());
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
                return;
            }
            expect(status >= 0 && frame->format == AV_SAMPLE_FMT_FLTP &&
                       frame->ch_layout.nb_channels == 2,
                   "epoch decoder frame");
            const auto* data = reinterpret_cast<const float*>(frame->extended_data[0]);
            samples.insert(samples.end(), data, data + frame->nb_samples);
            av_frame_unref(frame.get());
        }
    };
    for (const auto& bytes : packets) {
        expect(av_new_packet(packet.get(), static_cast<int>(bytes.size())) >= 0,
               "epoch packet allocation");
        std::memcpy(packet->data, bytes.data(), bytes.size());
        expect(avcodec_send_packet(codec.get(), packet.get()) >= 0, "epoch packet decode");
        drain();
        av_packet_unref(packet.get());
    }
    expect(avcodec_send_packet(codec.get(), nullptr) >= 0, "epoch decoder flush");
    drain();
    return samples;
}

Bytes tone_window(unsigned start, unsigned samples) {
    constexpr unsigned channels = 6;
    auto bytes = wave(channels, 0x60f, 2, samples);
    const auto payload_start = bytes.size() - samples * channels * sizeof(std::uint16_t);
    for (unsigned index = 0; index < samples; ++index) {
        constexpr double pi = 3.14159265358979323846;
        const auto time = static_cast<double>(start + index) / 48000.0;
        const auto value = static_cast<std::int16_t>(
            4096 * (0.7 * std::sin(2 * pi * 447 * time) + 0.3 * std::sin(2 * pi * 1022 * time)));
        const auto center_offset = payload_start + (index * channels + 2) * sizeof(std::uint16_t);
        const auto encoded = static_cast<std::uint16_t>(value);
        bytes[center_offset] = static_cast<std::uint8_t>(encoded);
        bytes[center_offset + 1] = static_cast<std::uint8_t>(encoded >> 8);
    }
    return bytes;
}

void bounded_encoder_windows() {
    constexpr unsigned aac_frame_samples = 1024;
    constexpr unsigned window_samples = 4 * aac_frame_samples;
    constexpr unsigned window_count = 12;
    constexpr unsigned warmup_samples = 32 * aac_frame_samples;
    Source continuous{tone_window(0, window_count * window_samples)};
    successful(probe(continuous), continuous, "continuous encoder reference");
    std::vector<Bytes> window_packets;
    for (unsigned window = 0; window < window_count; ++window) {
        const auto start = window * window_samples;
        const auto preroll = std::min(start, warmup_samples);
        const auto postroll =
            std::min(window_count * window_samples - start - window_samples, warmup_samples);
        Source source{tone_window(start - preroll, preroll + window_samples + postroll)};
        successful(probe(source), source, "bounded encoder window=" + std::to_string(window));
        const auto discarded_packets = window == 0 ? 0u : 1u + preroll / aac_frame_samples;
        expect(source.packet_data.size() ==
                   (preroll + window_samples + postroll) / aac_frame_samples + 1,
               "bounded window packet count=" + std::to_string(window));
        const auto retained_packets = window_samples / aac_frame_samples + (window == 0 ? 1u : 0u);
        window_packets.insert(window_packets.end(), source.packet_data.begin() + discarded_packets,
                              source.packet_data.begin() + discarded_packets + retained_packets);
    }
    const auto reference = decode_packets(continuous.packet_data);
    const auto joined = decode_packets(window_packets);
    expect(reference.size() == joined.size() &&
               joined.size() == aac_frame_samples + window_count * window_samples,
           "bounded encoder windows preserve the global sample grid");
    double squared_error = 0;
    for (std::size_t sample = aac_frame_samples; sample < joined.size(); ++sample) {
        const auto difference = static_cast<double>(joined[sample] - reference[sample]);
        squared_error += difference * difference;
    }
    const auto rms =
        std::sqrt(squared_error / static_cast<double>(joined.size() - aac_frame_samples));
    expect(rms < 0.002, "bounded encoder window join RMS=" + std::to_string(rms));
    std::cout << "bounded encoder windows: PCM preroll <=32768 samples, join RMS=" << rms << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 2, "fixture directory argument");
        expect(sap2_codec_probe_build_ok() != 0, "restricted LGPL codec configuration");
        channel_mapping();
        lifecycle_failures();
        resampling_and_encoder_epochs();
        compressed_fixtures(argv[1]);
        bounded_encoder_windows();
        std::cout << "audio codec probe: channel mapping, headroom, resampling, lifecycle, encoder "
                     "epochs and compressed fixtures passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "audio codec probe FAIL: " << error.what() << '\n';
        return 1;
    }
}
