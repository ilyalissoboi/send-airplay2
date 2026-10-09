// SPDX-License-Identifier: Apache-2.0
// Temporary synthetic files only: no receiver, network peer or private media.
#include "hls_directory.h"
#include "send_airplay2/media_server.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace send_airplay2;
using namespace send_airplay2::detail;

int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}

/// Owns a uniquely named temporary directory and removes it on scope exit.
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::random_device seed_source;
        path_ = fs::temp_directory_path() / ("sap2-hls-directory-" + std::to_string(seed_source()) +
                                             std::to_string(seed_source()));
        fs::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    [[nodiscard]] const fs::path& path() const {
        return path_;
    }
    void write(const std::string& name, const std::string& contents) const {
        std::ofstream stream(path_ / name, std::ios::out | std::ios::binary | std::ios::trunc);
        stream << contents;
    }

private:
    fs::path path_;
};

/// The read context a server worker would pass, with no stop and a far deadline.
struct OpenReadContext {
    std::atomic_bool stopped{false};
    std::atomic_bool cancelled{false};
    MediaReadContext context{&stopped, &cancelled,
                             std::chrono::steady_clock::now() + std::chrono::minutes(1)};
};

const MediaResource* find(const HlsDirectory& directory, const std::string& name) {
    for (const auto& resource : directory.resources) {
        if (resource.name == name) {
            return &resource;
        }
    }
    return nullptr;
}

std::string read_all(const MediaResource& resource) {
    OpenReadContext open;
    const auto size = resource.source.size();
    std::string contents;
    std::array<std::uint8_t, 7> buffer{}; // Small, so reads continue at nonzero offsets.
    while (contents.size() < size) {
        const auto count =
            resource.source.read_at(contents.size(), buffer.data(), buffer.size(), open.context);
        if (count == 0) {
            break;
        }
        contents.append(reinterpret_cast<const char*>(buffer.data()), count);
    }
    return contents;
}

// A media playlist in the layout ffmpeg writes for fMP4 VOD.
const std::string media_playlist = "#EXTM3U\r\n"
                                   "#EXT-X-VERSION:7\r\n"
                                   "#EXT-X-TARGETDURATION:6\r\n"
                                   "#EXT-X-PLAYLIST-TYPE:VOD\r\n"
                                   "#EXT-X-INDEPENDENT-SEGMENTS\r\n"
                                   "#EXT-X-MAP:URI=\"init.mp4\"\r\n"
                                   "#EXTINF:6.000000,\r\n"
                                   "s0.m4s\r\n"
                                   "## a comment naming other.m4s is not a reference\r\n"
                                   "#EXTINF:2.500000,\r\n"
                                   "  s1.m4s  \r\n"
                                   "#EXT-X-ENDLIST\r\n";

void media_playlist_tests() {
    group = "media playlist";
    TemporaryDirectory directory;
    directory.write("index.m3u8", media_playlist);
    directory.write("init.mp4", "INIT");
    directory.write("s0.m4s", "segment zero bytes");
    directory.write("s1.m4s", "one");
    directory.write("unreferenced.m4s", "must not be served");
    auto stats = std::make_shared<FileReadStats>();
    const auto opened = open_hls_directory(directory.path() / "index.m3u8", stats);

    check(opened.playlist_name == "index.m3u8", "entry is the playlist file name");
    check(opened.resources.size() == 4, "playlist, init and two segments, nothing else");
    check(find(opened, "unreferenced.m4s") == nullptr, "unreferenced file is not served");
    const auto* playlist = find(opened, "index.m3u8");
    const auto* init = find(opened, "init.mp4");
    const auto* first = find(opened, "s0.m4s");
    const auto* second = find(opened, "s1.m4s");
    check(playlist && playlist->content_type == "application/vnd.apple.mpegurl",
          "playlist content type");
    check(init && init->content_type == "video/mp4", "EXT-X-MAP init segment found");
    check(first && first->content_type == "video/mp4", "first segment found");
    check(second != nullptr, "URI line with surrounding spaces found");
    if (playlist && init && first && second) {
        check(read_all(*playlist) == media_playlist, "playlist bytes unchanged");
        check(read_all(*init) == "INIT", "init bytes");
        check(read_all(*first) == "segment zero bytes", "segment bytes across small reads");
        check(first->source.size() == 18, "size snapshot");
    }
    check(stats->reads.load() > 0 && stats->failures.load() == 0, "reads counted");

    // The resources are what the media server accepts as a set.
    MediaServerOptions options;
    options.receiver_address = "127.0.0.1";
    auto copy = open_hls_directory(directory.path() / "index.m3u8", stats);
    auto server = MediaServer::start_resource_set(std::move(copy.resources), options);
    check(server->resource_url("s1.m4s") == server->url() + "/s1.m4s",
          "media server serves the directory's names");
}

void multivariant_tests() {
    group = "multivariant playlist";
    TemporaryDirectory directory;
    directory.write("main.m3u8", "#EXTM3U\n"
                                 "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"en\",LANGUAGE="
                                 "\"en\",URI=\"audio.m3u8\"\n"
                                 "#EXT-X-STREAM-INF:BANDWIDTH=1000000,CODECS=\"avc1.4d401f,mp4a."
                                 "40.2\",RESOLUTION=1280x720,AUDIO=\"a\"\n"
                                 "video.m3u8\n");
    directory.write("video.m3u8", "#EXTM3U\n#EXT-X-MAP:URI=\"v-init.mp4\"\n#EXTINF:6,\nv0.m4s\n"
                                  "#EXT-X-ENDLIST\n");
    directory.write("audio.m3u8", "#EXTM3U\n#EXT-X-MAP:URI=\"a-init.mp4\"\n#EXTINF:6,\na0.m4s\n"
                                  "#EXT-X-ENDLIST\n");
    for (const auto* name : {"v-init.mp4", "v0.m4s", "a-init.mp4", "a0.m4s"}) {
        directory.write(name, name);
    }
    const auto opened =
        open_hls_directory(directory.path() / "main.m3u8", std::make_shared<FileReadStats>());
    check(opened.resources.size() == 7, "three playlists and four segment files");
    for (const auto* name : {"main.m3u8", "audio.m3u8", "video.m3u8", "v-init.mp4", "v0.m4s",
                             "a-init.mp4", "a0.m4s"}) {
        check(find(opened, name) != nullptr, std::string("serves ") + name);
    }
}

void refusal_tests() {
    group = "refusals";
    struct Case {
        const char* scenario;
        const char* playlist;
    };
    const Case cases[] = {
        {"absolute URL", "#EXTM3U\nhttp://example.invalid/s0.m4s\n"},
        {"subdirectory", "#EXTM3U\nsegments/s0.m4s\n"},
        {"parent reference", "#EXTM3U\n../s0.m4s\n"},
        {"query string", "#EXTM3U\ns0.m4s?token=1\n"},
        {"unknown extension", "#EXTM3U\ns0.bin\n"},
        {"missing file", "#EXTM3U\nabsent.m4s\n"},
        {"URI attribute outside the directory", "#EXTM3U\n#EXT-X-MAP:URI=\"/init.mp4\"\n"},
        {"unterminated URI attribute", "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\n"},
        {"no #EXTM3U header", "#EXT-X-VERSION:7\ns0.m4s\n"},
    };
    for (const auto& test : cases) {
        TemporaryDirectory directory;
        directory.write("index.m3u8", test.playlist);
        directory.write("s0.m4s", "x");
        directory.write("s0.bin", "x");
        try {
            (void)open_hls_directory(directory.path() / "index.m3u8",
                                     std::make_shared<FileReadStats>());
            check(false, std::string(test.scenario) + ": accepted");
        } catch (const std::invalid_argument& error) {
            check(std::string(error.what()).find(directory.path().string()) == std::string::npos,
                  std::string(test.scenario) + ": message omits the directory path");
        }
    }
    TemporaryDirectory directory;
    directory.write("index.txt", media_playlist);
    try {
        (void)open_hls_directory(directory.path() / "index.txt", std::make_shared<FileReadStats>());
        check(false, "entry without .m3u8 accepted");
    } catch (const std::invalid_argument&) {
    }
    try {
        (void)open_hls_directory(directory.path() / "absent.m3u8",
                                 std::make_shared<FileReadStats>());
        check(false, "missing entry playlist accepted");
    } catch (const std::invalid_argument&) {
    }
}

void content_type_tests() {
    group = "content types";
    check(hls_content_type("a.m3u8") == "application/vnd.apple.mpegurl", ".m3u8");
    check(hls_content_type("a.m4s") == "video/mp4", ".m4s");
    check(hls_content_type("a.mp4") == "video/mp4", ".mp4");
    check(hls_content_type("a.m4a") == "audio/mp4", ".m4a");
    check(hls_content_type("a.ts") == "video/mp2t", ".ts");
    check(hls_content_type("a.aac") == "audio/aac", ".aac");
    check(hls_content_type("a.mkv").empty(), ".mkv is not an HLS file");
    check(hls_content_type("m3u8").empty(), "an extension needs its dot");
}
} // namespace

int main() {
    try {
        media_playlist_tests();
        multivariant_tests();
        refusal_tests();
        content_type_tests();
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << group << "]: test infrastructure exception: " << error.what()
                  << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
