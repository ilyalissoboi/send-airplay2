// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "hls_test_support.h"
#include "media_track_inventory.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

namespace sap2_test {
/** Original synthetic mux around owned DTS/AVC packets. Each 1.024-second
 * cluster repeats 96 DTS frames in eight-frame fixed laces and two AVC samples.
 * AVC filler NALs make skipped video payload large without allocating the whole
 * movie. Both cue variants expose identical media and differ only in whether
 * selected-audio cue coordinates are present. Callbacks borrow this owner.
 */
class VirtualDtsSeekSource {
public:
    std::uint64_t requested = 0;
    std::size_t calls = 0;
    bool fail_reads = false;
    enum class VideoCues { ordinary, absent, late };
    explicit VirtualDtsSeekSource(const std::filesystem::path& path, bool audio_cues,
                                  VideoCues video_cues = VideoCues::ordinary) {
        Source fixture(path);
        Context context;
        const auto inventory = probe_media_tracks(fixture.callbacks(), context.value);
        const RandomReader read = [&](std::uint64_t offset, std::uint8_t* output,
                                      std::size_t count) {
            expect(offset <= fixture.bytes->size() && count <= fixture.bytes->size() - offset,
                   "virtual DTS owned packet bounds");
            std::memcpy(output, fixture.bytes->data() + offset, count);
        };
        const auto video = read_mkv_video_source(read, fixture.bytes->size()).tracks.front();
        const auto& first = video.samples.front();
        Bytes picture(first.size);
        read(first.offset, picture.data(), picture.size());
        // AVC length-prefixed filler_data NAL: header, filler bytes, trailing bits.
        constexpr std::uint32_t filler_bytes = 4 * 1024 * 1024;
        constexpr std::size_t nal_header_and_trailing_bytes = 2;
        for (int shift = 24; shift >= 0; shift -= 8) {
            picture.push_back(static_cast<std::uint8_t>(filler_bytes >> shift));
        }
        picture.push_back(0x0c);
        picture.insert(picture.end(), filler_bytes - nal_header_and_trailing_bytes, 0xff);
        picture.push_back(0x80);
        append(payload_, element(0xa3, block(11, 0, picture)));
        append(payload_, element(0xa3, block(11, 512, picture)));
        const auto packets = read_mkv_audio_packets(read, fixture.bytes->size(), 42);
        expect(packets.size() >= 96, "virtual DTS 96 owned frames");
        for (std::size_t frame = 0; frame < 96; frame += 8) {
            const auto tick = static_cast<std::uint16_t>((frame * 512 * 1000 + 24000) / 48000);
            Bytes lace{7};
            for (std::size_t offset = 0; offset < 8; ++offset) {
                const auto& packet = packets.at(frame + offset);
                expect(packet.size == packets.front().size, "virtual DTS fixed lace sizes");
                const auto previous = lace.size();
                lace.resize(previous + packet.size);
                read(packet.offset, lace.data() + previous, packet.size);
            }
            auto data = block(42, tick, lace);
            data[3] |= 4;
            append(payload_, element(0xa3, data));
        }
        build_prefix(inventory.tracks.front().configuration, audio_cues, video_cues);
    }
    VirtualDtsSeekSource(const VirtualDtsSeekSource&) = delete;
    VirtualDtsSeekSource& operator=(const VirtualDtsSeekSource&) = delete;
    VirtualDtsSeekSource(VirtualDtsSeekSource&&) = delete;
    VirtualDtsSeekSource& operator=(VirtualDtsSeekSource&&) = delete;
    MediaSource callbacks() {
        return {[this] { return size_; },
                [this](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                       const MediaReadContext& context) {
                    requested += capacity;
                    ++calls;
                    if (fail_reads || context.should_stop() || offset >= size_) {
                        return std::size_t{0};
                    }
                    const auto limit = static_cast<std::size_t>(
                        std::min<std::uint64_t>({capacity, 32749, size_ - offset}));
                    std::size_t done = 0;
                    while (done < limit) {
                        const auto count = read_piece(offset + done, output + done, limit - done);
                        expect(count != 0, "virtual DTS forward read progress");
                        done += count;
                    }
                    return done;
                }};
    }
    void reset_reads() {
        requested = 0;
        calls = 0;
    }

private:
    static constexpr std::size_t cluster_count = 128;
    static constexpr std::size_t misleading_cue_cluster_shift = 4;
    struct Cluster {
        std::uint64_t offset;
        Bytes header;
    };
    Bytes prefix_, payload_;
    std::vector<Cluster> clusters_;
    std::uint64_t size_ = 0;
    std::size_t read_piece(std::uint64_t offset, std::uint8_t* output, std::size_t capacity) {
        if (offset < prefix_.size()) {
            const auto count = std::min<std::size_t>(capacity, prefix_.size() - offset);
            std::memcpy(output, prefix_.data() + offset, count);
            return count;
        }
        const auto after = std::upper_bound(clusters_.begin(), clusters_.end(), offset,
                                            [](std::uint64_t position, const Cluster& cluster) {
                                                return position < cluster.offset;
                                            });
        const auto& cluster = *(after - 1);
        const auto relative = offset - cluster.offset;
        if (relative < cluster.header.size()) {
            const auto count = std::min<std::size_t>(capacity, cluster.header.size() - relative);
            std::memcpy(output, cluster.header.data() + relative, count);
            return count;
        }
        const auto within = relative - cluster.header.size();
        const auto count = std::min<std::size_t>(capacity, payload_.size() - within);
        std::memcpy(output, payload_.data() + within, count);
        return count;
    }
    void build_prefix(const Bytes& configuration, bool audio_cues, VideoCues video_cues) {
        prefix_ = element(0x1a45dfa3, element(0x4282, {'m', 'a', 't', 'r', 'o', 's', 'k', 'a'}));
        append(prefix_, {0x18, 0x53, 0x80, 0x67, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
        const auto segment_start = prefix_.size();
        auto info = integer(0x2ad7b1, 1000000);
        append(info, element(0x4489, floating(cluster_count * 1024.0)));
        append(prefix_, element(0x1549a966, info));
        auto video = integer(0xd7, 11);
        append(video, integer(0x83, 1));
        append(video, element(0x86, {'V', '_', 'M', 'P', 'E', 'G', '4', '/', 'I', 'S', 'O', '/',
                                     'A', 'V', 'C'}));
        append(video, element(0x63a2, configuration));
        append(video, integer(0x23e383, 512000000));
        auto dimensions = integer(0xb0, 16);
        append(dimensions, integer(0xba, 16));
        append(video, element(0xe0, dimensions));
        auto audio = integer(0xd7, 42);
        append(audio, integer(0x83, 2));
        append(audio, element(0x86, {'A', '_', 'D', 'T', 'S'}));
        append(audio, integer(0x23e383, 10666667));
        auto format = element(0xb5, floating(48000.0));
        append(format, integer(0x9f, 6));
        append(audio, element(0xe1, format));
        auto tracks = element(0xae, video);
        append(tracks, element(0xae, audio));
        append(prefix_, element(0x1654ae6b, tracks));
        const auto cues = [&](std::uint64_t start) {
            Bytes points;
            auto position = start;
            for (std::size_t index = 0; index < cluster_count; ++index) {
                const auto cue_tick =
                    video_cues == VideoCues::late && index >= misleading_cue_cluster_shift
                        ? (index - misleading_cue_cluster_shift) * 1024
                        : index * 1024;
                auto cue = integer(0xb3, cue_tick);
                bool has_position = false;
                for (const auto track : {11U, 42U}) {
                    if (track == 42 && !audio_cues) {
                        continue;
                    }
                    if (track == 11 &&
                        (video_cues == VideoCues::absent ||
                         (video_cues == VideoCues::late && index < misleading_cue_cluster_shift))) {
                        continue;
                    }
                    auto location = integer(0xf7, track);
                    append(location, integer(0xf1, position));
                    append(cue, element(0xb7, location));
                    has_position = true;
                }
                if (has_position) {
                    append(points, element(0xbb, cue));
                }
                const auto time = integer(0xe7, index * 1024);
                position += header(0x1f43b675, time.size() + payload_.size()).size() + time.size() +
                            payload_.size();
            }
            return element(0x1c53bb6b, points);
        };
        const auto cue_index = cues(prefix_.size() - segment_start + cues(0).size());
        append(prefix_, cue_index);
        auto position = static_cast<std::uint64_t>(prefix_.size());
        for (std::size_t index = 0; index < cluster_count; ++index) {
            const auto time = integer(0xe7, index * 1024);
            auto cluster = header(0x1f43b675, time.size() + payload_.size());
            append(cluster, time);
            clusters_.push_back({position, std::move(cluster)});
            position += clusters_.back().header.size() + payload_.size();
        }
        size_ = position;
    }
    static void append(Bytes& target, const Bytes& value) {
        target.insert(target.end(), value.begin(), value.end());
    }
    static Bytes header(std::uint32_t id, std::uint64_t length) {
        Bytes bytes;
        for (int shift = 24; shift >= 0; shift -= 8) {
            const auto byte = static_cast<std::uint8_t>(id >> shift);
            if (byte || !bytes.empty()) {
                bytes.push_back(byte);
            }
        }
        bytes.push_back(1);
        for (int shift = 48; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<std::uint8_t>(length >> shift));
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
    static Bytes block(std::uint8_t track, std::uint16_t tick, const Bytes& payload) {
        Bytes bytes{static_cast<std::uint8_t>(0x80 | track), static_cast<std::uint8_t>(tick >> 8),
                    static_cast<std::uint8_t>(tick), 0x80};
        append(bytes, payload);
        return bytes;
    }
};
} // namespace sap2_test
