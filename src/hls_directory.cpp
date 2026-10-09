// SPDX-License-Identifier: Apache-2.0
#include "hls_directory.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace send_airplay2::detail {
namespace {
namespace fs = std::filesystem;
constexpr std::uintmax_t max_playlist_bytes = 4 * 1024 * 1024;
constexpr std::size_t max_files = 65536;         // MediaServer's resource set bound.
constexpr std::size_t max_reference_length = 64; // MediaResource name bound.
constexpr std::string_view playlist_header = "#EXTM3U";
constexpr std::string_view uri_attribute = "URI=\"";

[[noreturn]] void refuse(const std::string& reason) {
    throw std::invalid_argument("HLS directory: " + reason);
}

bool alphanumeric(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/// The MediaResource name rule: one path segment, nothing to percent-encode.
bool plain_file_name(std::string_view name) {
    return !name.empty() && name.size() <= max_reference_length && alphanumeric(name.front()) &&
           std::all_of(name.begin(), name.end(),
                       [](char c) { return alphanumeric(c) || c == '.' || c == '_' || c == '-'; });
}

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

/// A reference may be echoed in an error: it was checked to be short, and it
/// names a file inside the presentation, never the directory path.
std::string printable(std::string_view reference) {
    if (reference.size() > max_reference_length ||
        !std::all_of(reference.begin(), reference.end(),
                     [](char c) { return c >= 0x20 && c < 0x7f; })) {
        return "(unprintable or long reference)";
    }
    return std::string(reference);
}

std::uintmax_t regular_file_size(const fs::path& path, std::string_view name) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) {
        refuse("referenced file is missing: " + printable(name));
    }
    const auto size = fs::file_size(path, error);
    if (error || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max())) {
        refuse("file size is unavailable: " + printable(name));
    }
    return size;
}

/// Every file a playlist names, in order: URI lines and URI="..." attributes.
std::vector<std::string_view> playlist_references(std::string_view text) {
    std::vector<std::string_view> references;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty()) {
            continue;
        }
        if (line.front() != '#') {
            references.push_back(line);
            continue;
        }
        if (line.compare(0, 4, "#EXT") != 0) {
            continue; // A comment.
        }
        for (auto start = line.find(uri_attribute); start != std::string_view::npos;
             start = line.find(uri_attribute, start)) {
            start += uri_attribute.size();
            const auto close = line.find('"', start);
            if (close == std::string_view::npos) {
                refuse("unterminated URI attribute");
            }
            references.push_back(line.substr(start, close - start));
            start = close + 1;
        }
    }
    return references;
}

std::string read_playlist(const fs::path& path, std::string_view name) {
    if (regular_file_size(path, name) > max_playlist_bytes) {
        refuse("playlist is larger than 4 MiB: " + printable(name));
    }
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (!stream.good() && !stream.eof()) {
        refuse("playlist cannot be read: " + printable(name));
    }
    if (text.compare(0, playlist_header.size(), playlist_header) != 0) {
        refuse("not an HLS playlist: " + printable(name));
    }
    return text;
}

/// Opens the file for each call, so thousands of segments hold no handles.
/// Returns 0 on cancellation, a short file or any stream error.
std::size_t read_file_at(const fs::path& path, std::uint64_t offset, std::uint8_t* output,
                         std::size_t capacity, const MediaReadContext& context) {
    if (context.should_stop()) {
        return 0;
    }
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    // Offsets are below the size snapshot, which was checked to fit streamoff.
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream) {
        return 0;
    }
    // At most 64 KiB from the server; clamp anyway so the signed count stays valid.
    const auto request = std::min<std::size_t>(
        capacity, static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()));
    stream.read(reinterpret_cast<char*>(output), static_cast<std::streamsize>(request));
    return static_cast<std::size_t>(stream.gcount());
}

MediaSource lazy_file_source(fs::path path, std::uint64_t size,
                             std::shared_ptr<FileReadStats> stats) {
    return {[size] { return size; },
            [path = std::move(path),
             stats = std::move(stats)](std::uint64_t offset, std::uint8_t* output,
                                       std::size_t capacity, const MediaReadContext& context) {
                const auto count = read_file_at(path, offset, output, capacity, context);
                if (count == 0) {
                    ++stats->failures;
                } else {
                    ++stats->reads;
                    stats->bytes += count;
                }
                return count;
            }};
}
} // namespace

std::string_view hls_content_type(std::string_view name) {
    struct Mapping {
        std::string_view extension;
        std::string_view type;
    };
    // RFC 8216 section 4 names the playlist type; segment types follow their containers.
    constexpr Mapping mappings[] = {
        {".m3u8", "application/vnd.apple.mpegurl"},
        {".mp4", "video/mp4"},
        {".m4s", "video/mp4"},
        {".m4v", "video/mp4"},
        {".m4a", "audio/mp4"},
        {".aac", "audio/aac"},
        {".ts", "video/mp2t"},
    };
    for (const auto& mapping : mappings) {
        if (ends_with(name, mapping.extension)) {
            return mapping.type;
        }
    }
    return {};
}

HlsDirectory open_hls_directory(const fs::path& playlist, std::shared_ptr<FileReadStats> stats) {
    if (!stats) {
        throw std::invalid_argument("HLS directory requires a statistics owner");
    }
    const auto entry = playlist.filename().string();
    if (!plain_file_name(entry) || !ends_with(entry, ".m3u8")) {
        refuse("the playlist file name must be a plain name ending in .m3u8");
    }
    const auto directory = playlist.parent_path();

    HlsDirectory result;
    result.playlist_name = entry;
    std::set<std::string, std::less<>> seen{entry};
    std::deque<std::string> playlists{entry};
    const auto add = [&](const std::string& name) {
        const auto path = directory / name;
        const auto size = regular_file_size(path, name);
        result.resources.push_back({name,
                                    std::string(hls_content_type(name)),
                                    lazy_file_source(path, static_cast<std::uint64_t>(size), stats),
                                    {}});
    };
    add(entry);
    while (!playlists.empty()) {
        const auto name = std::move(playlists.front());
        playlists.pop_front();
        const auto text = read_playlist(directory / name, name);
        for (const auto reference : playlist_references(text)) {
            if (!plain_file_name(reference) || hls_content_type(reference).empty()) {
                refuse("only plain file names with a known HLS extension are served: " +
                       printable(reference));
            }
            if (seen.find(reference) != seen.end()) {
                continue;
            }
            if (seen.size() == max_files) {
                refuse("more than 65536 files");
            }
            const std::string owned(reference);
            seen.insert(owned);
            add(owned);
            if (ends_with(owned, ".m3u8")) {
                playlists.push_back(owned);
            }
        }
    }
    return result;
}
} // namespace send_airplay2::detail
