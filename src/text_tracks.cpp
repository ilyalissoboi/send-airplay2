// SPDX-License-Identifier: Apache-2.0
// Text subtitles for HLS (D60 phase 3c): WebVTT (W3C WebVTT, RFC 8216bis
// 3.1.4) from Matroska text subtitle blocks. Written from the
// specifications; no third-party code.
#include "text_tracks.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <limits>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t us_per_ms = 1000;
constexpr std::uint64_t ms_per_second = 1000;
constexpr std::uint64_t seconds_per_minute = 60;
constexpr std::uint64_t minutes_per_hour = 60;
constexpr std::size_t ass_fields_before_text = 8;  // ReadOrder .. Effect.
constexpr const char* no_break_space = "\xC2\xA0"; // U+00A0 in UTF-8.

/// ISO 639-2 (bibliographic and terminology codes) to ISO 639-1, for the
/// languages most often found in subtitle tracks.
struct LanguagePair {
    std::string_view three;
    std::string_view two;
};
constexpr std::array<LanguagePair, 74> iso639_pairs{{
    {"afr", "af"}, {"alb", "sq"}, {"sqi", "sq"}, {"ara", "ar"}, {"arm", "hy"}, {"hye", "hy"},
    {"baq", "eu"}, {"eus", "eu"}, {"bel", "be"}, {"ben", "bn"}, {"bos", "bs"}, {"bul", "bg"},
    {"cat", "ca"}, {"chi", "zh"}, {"zho", "zh"}, {"hrv", "hr"}, {"cze", "cs"}, {"ces", "cs"},
    {"dan", "da"}, {"dut", "nl"}, {"nld", "nl"}, {"eng", "en"}, {"est", "et"}, {"fil", "fil"},
    {"fin", "fi"}, {"fre", "fr"}, {"fra", "fr"}, {"glg", "gl"}, {"geo", "ka"}, {"kat", "ka"},
    {"ger", "de"}, {"deu", "de"}, {"gre", "el"}, {"ell", "el"}, {"heb", "he"}, {"hin", "hi"},
    {"hun", "hu"}, {"ice", "is"}, {"isl", "is"}, {"ind", "id"}, {"gle", "ga"}, {"ita", "it"},
    {"jpn", "ja"}, {"kaz", "kk"}, {"kor", "ko"}, {"lav", "lv"}, {"lit", "lt"}, {"mac", "mk"},
    {"mkd", "mk"}, {"may", "ms"}, {"msa", "ms"}, {"nor", "no"}, {"nob", "nb"}, {"nno", "nn"},
    {"per", "fa"}, {"fas", "fa"}, {"pol", "pl"}, {"por", "pt"}, {"rum", "ro"}, {"ron", "ro"},
    {"rus", "ru"}, {"srp", "sr"}, {"slo", "sk"}, {"slk", "sk"}, {"slv", "sl"}, {"spa", "es"},
    {"swe", "sv"}, {"tam", "ta"}, {"tel", "te"}, {"tha", "th"}, {"tur", "tr"}, {"ukr", "uk"},
    {"urd", "ur"}, {"vie", "vi"},
}};

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

void append_escaped(std::string& out, char c) {
    if (c == '&') {
        out += "&amp;";
    } else if (c == '<') {
        out += "&lt;";
    } else if (c == '>') {
        out += "&gt;";
    } else {
        out += c;
    }
}

/// CR LF and CR become LF; each line loses leading and trailing spaces and
/// tabs (WebVTT would render them); empty lines, which would end a WebVTT
/// cue, are removed.
std::string tidy_lines(const std::string& text) {
    std::string lines;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                continue;
            }
            lines += '\n';
        } else {
            lines += text[index];
        }
    }
    std::string out;
    std::size_t start = 0;
    while (start <= lines.size()) {
        auto end = lines.find('\n', start);
        if (end == std::string::npos) {
            end = lines.size();
        }
        const auto line = lines.substr(start, end - start);
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos) {
            if (!out.empty()) {
                out += '\n';
            }
            out += line.substr(first, line.find_last_not_of(" \t") - first + 1);
        }
        start = end + 1;
    }
    return out;
}

std::string timestamp(std::uint64_t microseconds) {
    const auto total_ms = microseconds / us_per_ms;
    const auto ms = total_ms % ms_per_second;
    const auto total_seconds = total_ms / ms_per_second;
    const auto seconds = total_seconds % seconds_per_minute;
    const auto minutes = (total_seconds / seconds_per_minute) % minutes_per_hour;
    const auto hours = total_seconds / seconds_per_minute / minutes_per_hour;
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%02llu:%02llu:%02llu.%03llu",
                  static_cast<unsigned long long>(hours), static_cast<unsigned long long>(minutes),
                  static_cast<unsigned long long>(seconds), static_cast<unsigned long long>(ms));
    return buffer.data();
}
} // namespace

std::string subrip_to_webvtt(std::string_view text) {
    std::string out;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char c = text[index];
        // A tag starts with a letter, or '/' and a letter: "< 5" is text.
        const auto letter_at = [&text](std::size_t at) {
            return at < text.size() && std::isalpha(static_cast<unsigned char>(text[at]));
        };
        if (c == '<' &&
            (letter_at(index + 1) ||
             (index + 1 < text.size() && text[index + 1] == '/' && letter_at(index + 2)))) {
            const auto close = text.find('>', index);
            if (close != std::string_view::npos) {
                const auto tag = lower(text.substr(index + 1, close - index - 1));
                if (tag == "i" || tag == "/i" || tag == "b" || tag == "/b" || tag == "u" ||
                    tag == "/u") {
                    out += '<' + tag + '>';
                }
                index = close; // Other tags, such as <font ...>, are dropped.
                continue;
            }
        }
        append_escaped(out, c);
    }
    return tidy_lines(out);
}

std::string ass_to_webvtt(std::string_view block) {
    std::size_t start = 0;
    for (std::size_t field = 0; field < ass_fields_before_text; ++field) {
        const auto comma = block.find(',', start);
        if (comma == std::string_view::npos) {
            return {};
        }
        start = comma + 1;
    }
    const auto text = block.substr(start);
    std::string out;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char c = text[index];
        if (c == '{') {
            const auto close = text.find('}', index);
            if (close != std::string_view::npos) {
                index = close; // An override block such as {\i1} or {\pos(...)}.
                continue;
            }
        }
        if (c == '\\' && index + 1 < text.size()) {
            const char next = text[index + 1];
            if (next == 'N') {
                out += '\n';
                ++index;
                continue;
            }
            if (next == 'n') {
                out += ' ';
                ++index;
                continue;
            }
            if (next == 'h') {
                out += no_break_space;
                ++index;
                continue;
            }
        }
        append_escaped(out, c);
    }
    return tidy_lines(out);
}

std::string language_tag(std::string_view iso639_2, std::string_view bcp47) {
    const auto valid = [](std::string_view tag) {
        return !tag.empty() && tag.size() <= 35 &&
               std::all_of(tag.begin(), tag.end(),
                           [](unsigned char c) { return std::isalnum(c) || c == '-'; });
    };
    if (valid(bcp47)) {
        return std::string(bcp47);
    }
    const auto code = lower(iso639_2);
    if (code.size() != 3 || !std::all_of(code.begin(), code.end(),
                                         [](unsigned char c) { return c >= 'a' && c <= 'z'; })) {
        return "und";
    }
    for (const auto& pair : iso639_pairs) {
        if (pair.three == code) {
            return std::string(pair.two);
        }
    }
    return code; // A three-letter ISO 639-2 code is a valid BCP 47 language subtag.
}

std::string webvtt_segment(const TextTrack& track, std::uint64_t start_us, std::uint64_t end_us) {
    // MPEGTS is a 90 kHz media time; 0 maps cue time 0 to the presentation start.
    std::string out = "WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:0,LOCAL:00:00:00.000\n";
    for (const auto& cue : track.cues) {
        if (cue.start_us >= end_us) {
            break; // Cues are sorted by start time.
        }
        if (cue.end_us <= start_us) {
            continue;
        }
        out += '\n' + timestamp(cue.start_us) + " --> " + timestamp(cue.end_us) + '\n' + cue.text +
               '\n';
    }
    return out;
}
} // namespace send_airplay2::detail
