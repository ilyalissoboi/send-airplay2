// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "hls_test_support.h"
#include "media_track_inventory.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>

namespace sap2_test {
/** Repeated original 1-second PCM/FLAC/TrueHD/Opus and black all-IDR video.
 * A sparse EBML Void places every cluster beyond 4 GiB. Only headers and one
 * owned payload cycle are allocated. This is synthetic callback/index scaling,
 * not a real-film, packaged-host or receiver result. Callbacks borrow this owner.
 */
class VirtualAudioSource {
public:
    static constexpr std::uint64_t first_cluster_offset = 0x1'0000'1000;
    static constexpr std::uint64_t duration_samples = 172800000;
    std::atomic<std::uint64_t> requested{0}, payload_read{0}, largest_offset{0};
    std::atomic<std::size_t> size_calls{0};
    std::atomic_bool fail_payload{false};

    explicit VirtualAudioSource(const std::filesystem::path& path, std::uint32_t seconds = 3600)
        : seconds_(seconds) {
        Source fixture(path);
        Context context;
        const auto inventory = probe_media_tracks(fixture.callbacks(), context.value);
        const auto selection = select_source_audio(inventory, 42);
        const auto& selected = inventory.tracks[selection.track_index];
        const bool flac = selected.codec == "A_FLAC";
        packet_cycle_ = selected.codec == "A_TRUEHD" || selected.codec == "A_OPUS";
        const RandomReader read = [&](std::uint64_t offset, std::uint8_t* output,
                                      std::size_t length) {
            expect(offset <= fixture.bytes->size() && length <= fixture.bytes->size() - offset,
                   "virtual audio original payload bounds");
            std::memcpy(output, fixture.bytes->data() + offset, length);
        };
        const auto video = read_mkv_video_source(read, fixture.bytes->size()).tracks.front();
        const auto& frame = video.samples.front();
        Bytes picture(frame.size);
        read(frame.offset, picture.data(), picture.size());
        constexpr std::size_t payload_cycle_bytes = 48000 * 2 * 2;
        for (const auto& packet : read_mkv_audio_packets(read, fixture.bytes->size(), 42)) {
            if (packet_cycle_) {
                if (packet.stored_timestamp_ns >= 1'000'000'000) {
                    break;
                }
                Bytes bytes(packet.size);
                read(packet.offset, bytes.data(), bytes.size());
                const auto ticks =
                    static_cast<std::uint16_t>((packet.stored_timestamp_ns + 500000) / 1000000);
                append(payload_, element(0xa3, block(42, ticks, bytes)));
                continue;
            }
            if (flac) {
                expect(payload_.empty(),
                       "virtual FLAC has one independently authored one-second block");
                payload_.resize(packet.size);
                read(packet.offset, payload_.data(), payload_.size());
                break;
            }
            const auto remaining = payload_cycle_bytes - payload_.size();
            const auto length = std::min<std::size_t>(remaining, packet.size);
            const auto previous = payload_.size();
            payload_.resize(previous + length);
            read(packet.offset, payload_.data() + previous, length);
            if (payload_.size() == payload_cycle_bytes) {
                break;
            }
        }
        expect(packet_cycle_ || flac || payload_.size() == payload_cycle_bytes,
               "virtual PCM one-second original cycle");
        build_clusters(picture);
        build_prefix(inventory.tracks.front().configuration, selected);
    }

    VirtualAudioSource(const VirtualAudioSource&) = delete;
    VirtualAudioSource& operator=(const VirtualAudioSource&) = delete;
    VirtualAudioSource(VirtualAudioSource&&) = delete;
    VirtualAudioSource& operator=(VirtualAudioSource&&) = delete;
    MediaSource callbacks() {
        return {[this] {
                    ++size_calls;
                    return size_;
                },
                [this](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                       const MediaReadContext& context) {
                    requested += capacity;
                    auto previous = largest_offset.load();
                    while (offset > previous &&
                           !largest_offset.compare_exchange_weak(previous, offset)) {
                    }
                    if (context.should_stop() || offset >= size_) {
                        return std::size_t{0};
                    }
                    if (!packet_cycle_) {
                        return read_piece(offset, output, std::min<std::size_t>(capacity, 997));
                    }
                    // High-count controls deliver complete requested pages,
                    // including synthetic boundaries, so AVIO's requested-byte
                    // budget measures scanning rather than artificial short reads.
                    std::size_t done = 0;
                    while (done < capacity && offset + done < size_ && !context.should_stop()) {
                        const auto count =
                            read_piece(offset + done, output + done, capacity - done);
                        if (count == 0) {
                            break;
                        }
                        done += count;
                    }
                    return done;
                }};
    }
    void reset_reads() {
        requested = 0;
        payload_read = 0;
        largest_offset = 0;
    }

private:
    struct Cluster {
        std::uint64_t offset;
        Bytes header;
    };
    Bytes prefix_, payload_;
    std::vector<Cluster> clusters_;
    std::vector<Cluster> padding_;
    std::uint64_t size_ = 0;
    std::uint32_t seconds_;
    bool packet_cycle_ = false;
    std::size_t read_piece(std::uint64_t offset, std::uint8_t* output, std::size_t capacity) {
        const auto limit = capacity;
        if (offset < prefix_.size()) {
            const auto count = std::min<std::size_t>(limit, prefix_.size() - offset);
            std::memcpy(output, prefix_.data() + offset, count);
            return count;
        }
        if (offset < first_cluster_offset) {
            const auto after = std::upper_bound(
                padding_.begin(), padding_.end(), offset,
                [](std::uint64_t value, const Cluster& padding) { return value < padding.offset; });
            const auto& padding = *(after - 1);
            const auto relative = offset - padding.offset;
            if (relative < padding.header.size()) {
                const auto count = static_cast<std::size_t>(
                    std::min<std::uint64_t>(limit, padding.header.size() - relative));
                std::memcpy(output, padding.header.data() + relative, count);
                return count;
            }
            const auto padding_end = after == padding_.end() ? first_cluster_offset : after->offset;
            const auto count =
                static_cast<std::size_t>(std::min<std::uint64_t>(limit, padding_end - offset));
            std::memset(output, 0, count);
            return count;
        }
        const auto after = std::upper_bound(
            clusters_.begin(), clusters_.end(), offset,
            [](std::uint64_t value, const Cluster& cluster) { return value < cluster.offset; });
        const auto& cluster = *(after - 1);
        const auto relative = offset - cluster.offset;
        if (relative < cluster.header.size()) {
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(limit, cluster.header.size() - relative));
            std::memcpy(output, cluster.header.data() + relative, count);
            return count;
        }
        if (fail_payload) {
            return std::size_t{0};
        }
        const auto within = relative - cluster.header.size();
        const auto count =
            static_cast<std::size_t>(std::min<std::uint64_t>(limit, payload_.size() - within));
        std::memcpy(output, payload_.data() + within, count);
        payload_read += count;
        return count;
    }
    void build_clusters(const Bytes& picture) {
        auto position = first_cluster_offset;
        for (std::uint32_t second = 0; second < seconds_; ++second) {
            auto contents = integer(0xe7, second * 1000);
            append(contents, element(0xa3, block(11, 0, picture)));
            append(contents, element(0xa3, block(11, 500, picture)));
            if (!packet_cycle_) {
                auto audio_header = header(0xa3, payload_.size() + 4);
                append(audio_header, {0xaa, 0, 0, 0x80});
                append(contents, audio_header);
            }
            auto cluster = header(0x1f43b675, contents.size() + payload_.size());
            append(cluster, contents);
            clusters_.push_back({position, std::move(cluster)});
            position += clusters_.back().header.size() + payload_.size();
        }
        size_ = position;
    }
    void build_prefix(const Bytes& configuration, const SourceTrack& audio_track) {
        const auto ebml =
            element(0x1a45dfa3, element(0x4282, {'m', 'a', 't', 'r', 'o', 's', 'k', 'a'}));
        prefix_ = ebml;
        append(prefix_, {0x18, 0x53, 0x80, 0x67, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
        const auto segment_data = prefix_.size();
        auto info = integer(0x2ad7b1, 1000000);
        append(info, element(0x4489, floating(seconds_ * 1000.0)));
        append(prefix_, element(0x1549a966, info));
        auto video_entry = integer(0xd7, 11);
        append(video_entry, integer(0x83, 1));
        append(video_entry, element(0x86, {'V', '_', 'M', 'P', 'E', 'G', '4', '/', 'I', 'S', 'O',
                                           '/', 'A', 'V', 'C'}));
        append(video_entry, element(0x63a2, configuration));
        append(video_entry, integer(0x23e383, 500000000));
        auto dimensions = integer(0xb0, 16);
        append(dimensions, integer(0xba, 16));
        append(video_entry, element(0xe0, dimensions));
        auto audio_entry = integer(0xd7, 42);
        append(audio_entry, integer(0x83, 2));
        append(audio_entry,
               element(0x86, Bytes(audio_track.codec.begin(), audio_track.codec.end())));
        if (!audio_track.configuration.empty()) {
            append(audio_entry, element(0x63a2, audio_track.configuration));
        }
        if (audio_track.codec_delay_ns != 0) {
            append(audio_entry, integer(0x56aa, audio_track.codec_delay_ns));
        }
        auto audio = element(0xb5, floating(48000.0));
        append(audio, integer(0x9f, 2));
        if (audio_track.codec.compare(0, 6, "A_PCM/") == 0) {
            append(audio, integer(0x6264, 16));
        }
        append(audio_entry, element(0xe1, audio));
        auto tracks = element(0xae, video_entry);
        append(tracks, element(0xae, audio_entry));
        append(prefix_, element(0x1654ae6b, tracks));
        Bytes cues;
        for (std::size_t second = 0; second < clusters_.size(); second += 6) {
            auto cue = integer(0xb3, second * 1000);
            auto location = integer(0xf7, 11);
            append(location, integer(0xf1, clusters_[second].offset - segment_data));
            append(cue, element(0xb7, location));
            append(cues, element(0xbb, cue));
        }
        append(prefix_, element(0x1c53bb6b, cues));
        constexpr std::size_t void_header_bytes = 9;
        expect(prefix_.size() + void_header_bytes < first_cluster_offset,
               "virtual audio Void bounds");
        auto padding_position = static_cast<std::uint64_t>(prefix_.size());
        while (padding_position < first_cluster_offset) {
            // FFmpeg's EBML skip helper currently takes a signed-int length.
            // Sized Void elements below 1 GiB keep the synthetic gap within that
            // backend subset while all packet offsets remain above 4 GiB.
            const auto total =
                std::min<std::uint64_t>(1ULL << 30, first_cluster_offset - padding_position);
            expect(total >= void_header_bytes, "virtual audio remaining Void size");
            padding_.push_back({padding_position, header(0xec, total - void_header_bytes)});
            padding_position += total;
        }
    }
    static void append(Bytes& target, const Bytes& value) {
        target.insert(target.end(), value.begin(), value.end());
    }
    static Bytes header(std::uint32_t id, std::uint64_t size) {
        expect(size < (1ULL << 56) - 1, "virtual EBML sized element bound");
        Bytes bytes;
        for (int shift = 24; shift >= 0; shift -= 8) {
            const auto value = static_cast<std::uint8_t>(id >> shift);
            if (value || !bytes.empty()) {
                bytes.push_back(value);
            }
        }
        bytes.push_back(1);
        for (int shift = 48; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<std::uint8_t>(size >> shift));
        }
        return bytes;
    }
    static Bytes element(std::uint32_t id, const Bytes& body) {
        auto bytes = header(id, body.size());
        append(bytes, body);
        return bytes;
    }
    static Bytes integer(std::uint32_t id, std::uint64_t value) {
        Bytes bytes;
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        }
        return element(id, bytes);
    }
    static Bytes floating(double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        Bytes bytes;
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
        }
        return bytes;
    }
    static Bytes block(std::uint8_t track, std::uint16_t relative, const Bytes& payload) {
        Bytes bytes{static_cast<std::uint8_t>(0x80 | track),
                    static_cast<std::uint8_t>(relative >> 8), static_cast<std::uint8_t>(relative),
                    0x80};
        append(bytes, payload);
        return bytes;
    }
};
} // namespace sap2_test
