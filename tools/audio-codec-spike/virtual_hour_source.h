// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "box_writer.h"
#include "hls_test_support.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

namespace sap2_test {
/** Original progressive MP4 tables with one hour of repeated owned packets.
 * co64 locates payload beyond a sparse free box, above 4 GiB. Only metadata and
 * one short packet cycle are allocated; callbacks synthesize the rest. This is
 * callback/index/seek scaling evidence, not a real long-film or receiver test.
 * Callback lifetime is borrowed from this owner; counters are thread-safe.
 */
class VirtualHourSource {
public:
    static constexpr std::uint64_t payload_offset = 0x1'0000'1000;
    static constexpr std::uint32_t duration_samples = 172800000;
    static constexpr std::uint32_t video_samples = 7200;
    static constexpr std::uint32_t audio_samples = 337500;
    std::atomic<std::uint64_t> metadata_bytes{0};
    std::atomic<std::uint64_t> payload_bytes{0};
    std::atomic<std::uint64_t> largest_offset{0};
    std::atomic<std::size_t> size_calls{0};
    std::atomic<std::size_t> read_calls{0};
    std::atomic_bool block{false};
    std::atomic_bool entered{false};

    explicit VirtualHourSource(const std::filesystem::path& path) {
        Source fixture(path);
        const RandomReader reader = [&](std::uint64_t offset, std::uint8_t* output,
                                        std::size_t count) {
            expect(offset <= fixture.bytes->size() && count <= fixture.bytes->size() - offset,
                   "virtual hour base packet range");
            std::memcpy(output, fixture.bytes->data() + offset, count);
        };
        const auto video = read_mp4_video_source(reader, fixture.bytes->size()).tracks.front();
        const auto audio = read_mp4_source_audio(reader, fixture.bytes->size(), 42);
        const auto video_sizes = packet_cycle(video, *fixture.bytes, video_cycle_);
        const auto audio_sizes = packet_cycle(audio, *fixture.bytes, audio_cycle_);
        expect(audio_sizes.size() > 100 && audio.samples.front().duration == 512 &&
                   video.samples.front().duration * 2 == video.timescale,
               "virtual hour base DTS 512-sample / 2fps video format");
        video_bytes_ = repeated_bytes(video_sizes, video_samples);
        audio_bytes_ = repeated_bytes(audio_sizes, audio_samples);
        BoxWriter out;
        const auto ftyp = out.open("ftyp");
        out.bytes(reinterpret_cast<const std::uint8_t*>("isom"), 4);
        out.u32(0);
        out.bytes(reinterpret_cast<const std::uint8_t*>("isommp41"), 8);
        out.close(ftyp);
        const auto moov = out.open("moov");
        movie_header(out);
        track(out, video, 11, video_samples, 24000, video_sizes, payload_offset);
        track(out, audio, 42, audio_samples, 512, audio_sizes, payload_offset + video_bytes_);
        out.close(moov);
        prefix_ = out.take();
        expect(prefix_.size() + 32 < payload_offset, "virtual hour sparse free-box bounds");
        // Large-size headers have size=1 and a big-endian 64-bit total size.
        BoxWriter free;
        free.u32(1);
        free.bytes(reinterpret_cast<const std::uint8_t*>("free"), 4);
        free.u64(payload_offset - 16 - prefix_.size());
        const auto free_header = free.take();
        prefix_.insert(prefix_.end(), free_header.begin(), free_header.end());
        BoxWriter mdat;
        mdat.u32(1);
        mdat.bytes(reinterpret_cast<const std::uint8_t*>("mdat"), 4);
        mdat.u64(16 + video_bytes_ + audio_bytes_);
        mdat_header_ = mdat.take();
    }
    VirtualHourSource(const VirtualHourSource&) = delete;
    VirtualHourSource& operator=(const VirtualHourSource&) = delete;
    VirtualHourSource(VirtualHourSource&&) = delete;
    VirtualHourSource& operator=(VirtualHourSource&&) = delete;

    MediaSource callbacks() {
        return {[this] {
                    ++size_calls;
                    return payload_offset + video_bytes_ + audio_bytes_;
                },
                [this](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                       const MediaReadContext& context) {
                    ++read_calls;
                    if (block) {
                        entered = true;
                        while (block && !context.should_stop()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                    auto observed = largest_offset.load();
                    while (offset > observed &&
                           !largest_offset.compare_exchange_weak(observed, offset)) {
                    }
                    if (context.should_stop() ||
                        offset >= payload_offset + video_bytes_ + audio_bytes_) {
                        return std::size_t{0};
                    }
                    const auto limit = std::min<std::size_t>(capacity, 65536);
                    if (offset < prefix_.size()) {
                        const auto count = std::min<std::size_t>(
                            limit, prefix_.size() - static_cast<std::size_t>(offset));
                        std::memcpy(output, prefix_.data() + offset, count);
                        metadata_bytes += count;
                        return count;
                    }
                    if (offset < payload_offset - 16) {
                        const auto count = static_cast<std::size_t>(
                            std::min<std::uint64_t>(limit, payload_offset - 16 - offset));
                        std::memset(output, 0, count);
                        metadata_bytes += count;
                        return count;
                    }
                    if (offset < payload_offset) {
                        const auto count = static_cast<std::size_t>(
                            std::min<std::uint64_t>(limit, payload_offset - offset));
                        std::memcpy(output, mdat_header_.data() + offset - (payload_offset - 16),
                                    count);
                        metadata_bytes += count;
                        return count;
                    }
                    const bool video = offset < payload_offset + video_bytes_;
                    const auto relative = offset - payload_offset - (video ? 0 : video_bytes_);
                    const auto& cycle = video ? video_cycle_ : audio_cycle_;
                    const auto total = video ? video_bytes_ : audio_bytes_;
                    const auto within_cycle = static_cast<std::size_t>(relative % cycle.size());
                    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                        {limit, cycle.size() - within_cycle, total - relative}));
                    std::memcpy(output, cycle.data() + within_cycle, count);
                    payload_bytes += count;
                    return count;
                }};
    }

private:
    Bytes prefix_, mdat_header_, video_cycle_, audio_cycle_;
    std::uint64_t video_bytes_ = 0, audio_bytes_ = 0;

    static std::vector<std::uint32_t> packet_cycle(const Mp4Track& track, const Bytes& source,
                                                   Bytes& cycle) {
        std::vector<std::uint32_t> sizes;
        for (const auto& sample : track.samples) {
            expect(sample.size > 0 && sample.offset <= source.size() &&
                       sample.size <= source.size() - sample.offset,
                   "virtual hour packet bounds");
            sizes.push_back(sample.size);
            cycle.insert(cycle.end(), source.begin() + static_cast<std::ptrdiff_t>(sample.offset),
                         source.begin() + static_cast<std::ptrdiff_t>(sample.offset + sample.size));
        }
        expect(!sizes.empty(), "virtual hour empty packet cycle");
        return sizes;
    }
    static std::uint64_t repeated_bytes(const std::vector<std::uint32_t>& sizes,
                                        std::uint32_t count) {
        std::uint64_t total = 0;
        for (std::uint32_t index = 0; index < count; ++index) {
            total += sizes[index % sizes.size()];
        }
        return total;
    }
    static void movie_header(BoxWriter& out) {
        const auto box = out.open_full("mvhd", 0, 0);
        out.u32(0);
        out.u32(0);
        out.u32(1000);
        out.u32(3600000);
        out.u32(fixed_point_one);
        out.u16(full_volume);
        out.zeros(10);
        out.identity_matrix();
        out.zeros(24);
        out.u32(43);
        out.close(box);
    }
    static void track(BoxWriter& out, const Mp4Track& original, std::uint32_t id,
                      std::uint32_t count, std::uint32_t sample_duration,
                      const std::vector<std::uint32_t>& sizes, std::uint64_t offset) {
        const bool video = original.kind == TrackKind::video;
        const auto trak = out.open("trak");
        const auto tkhd = out.open_full("tkhd", 0, 3);
        out.u32(0);
        out.u32(0);
        out.u32(id);
        out.u32(0);
        out.u32(3600000);
        out.zeros(8);
        out.u16(0);
        out.u16(0);
        out.u16(video ? 0 : full_volume);
        out.u16(0);
        out.identity_matrix();
        out.u32(video ? 16 * fixed_point_one : 0);
        out.u32(video ? 16 * fixed_point_one : 0);
        out.close(tkhd);
        const auto mdia = out.open("mdia");
        const auto mdhd = out.open_full("mdhd", 0, 0);
        out.u32(0);
        out.u32(0);
        out.u32(48000);
        out.u32(duration_samples);
        out.u16(packed_language("eng"));
        out.u16(0);
        out.close(mdhd);
        const auto hdlr = out.open_full("hdlr", 0, 0);
        out.u32(0);
        out.bytes(reinterpret_cast<const std::uint8_t*>(video ? "vide" : "soun"), 4);
        out.zeros(12);
        out.text("Original virtual-hour fixture");
        out.close(hdlr);
        const auto minf = out.open("minf");
        const auto media_header = out.open_full(video ? "vmhd" : "smhd", 0, video ? 1 : 0);
        out.zeros(video ? 8 : 4);
        out.close(media_header);
        const auto dinf = out.open("dinf");
        const auto dref = out.open_full("dref", 0, 0);
        out.u32(1);
        const auto url = out.open_full("url ", 0, 1);
        out.close(url);
        out.close(dref);
        out.close(dinf);
        const auto stbl = out.open("stbl");
        const auto stsd = out.open_full("stsd", 0, 0);
        out.u32(1);
        out.bytes(original.sample_entry);
        out.close(stsd);
        const auto stts = out.open_full("stts", 0, 0);
        out.u32(1);
        out.u32(count);
        out.u32(sample_duration);
        out.close(stts);
        const auto stsc = out.open_full("stsc", 0, 0);
        out.u32(1);
        out.u32(1);
        out.u32(1);
        out.u32(1);
        out.close(stsc);
        const auto stsz = out.open_full("stsz", 0, 0);
        out.u32(0);
        out.u32(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            out.u32(sizes[index % sizes.size()]);
        }
        out.close(stsz);
        const auto co64 = out.open_full("co64", 0, 0);
        out.u32(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            out.u64(offset);
            offset += sizes[index % sizes.size()];
        }
        out.close(co64);
        out.close(stbl);
        out.close(minf);
        out.close(mdia);
        out.close(trak);
    }
};
} // namespace sap2_test
