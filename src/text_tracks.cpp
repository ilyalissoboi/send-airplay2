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

std::string plain_text_to_webvtt(std::string_view text, const std::vector<TextStyleRun>& runs) {
    struct Character {
        std::string_view bytes;
        unsigned style = 0; // Bit 0 bold, 1 italic, 2 underline.
    };
    constexpr unsigned bold = 1;
    constexpr unsigned italic = 2;
    constexpr unsigned underline = 4;
    // Split into code points (an invalid byte counts as one) and style them.
    std::vector<Character> characters;
    for (std::size_t at = 0; at < text.size();) {
        const auto lead = static_cast<unsigned char>(text[at]);
        std::size_t length = lead < 0x80    ? 1
                             : lead >= 0xF0 ? 4
                             : lead >= 0xE0 ? 3
                             : lead >= 0xC0 ? 2
                                            : 1;
        length = std::min(length, text.size() - at);
        characters.push_back({text.substr(at, length), 0});
        at += length;
    }
    for (const auto& run : runs) {
        const unsigned style =
            (run.bold ? bold : 0) | (run.italic ? italic : 0) | (run.underline ? underline : 0);
        for (auto index = run.start; index < run.end && index < characters.size(); ++index) {
            characters[index].style |= style;
        }
    }
    const auto open = [&](std::string& out, unsigned style) {
        out += (style & bold) ? "<b>" : "";
        out += (style & italic) ? "<i>" : "";
        out += (style & underline) ? "<u>" : "";
    };
    const auto close = [&](std::string& out, unsigned style) {
        out += (style & underline) ? "</u>" : "";
        out += (style & italic) ? "</i>" : "";
        out += (style & bold) ? "</b>" : "";
    };
    const auto blank = [](const Character& c) { return c.bytes == " " || c.bytes == "\t"; };
    std::string out;
    std::size_t start = 0;
    while (start <= characters.size()) {
        // One line: up to LF, CR or CR LF.
        auto end = start;
        while (end < characters.size() && characters[end].bytes != "\n" &&
               characters[end].bytes != "\r") {
            ++end;
        }
        auto first = start;
        auto last = end;
        while (first < last && blank(characters[first])) {
            ++first;
        }
        while (last > first && blank(characters[last - 1])) {
            --last;
        }
        if (first < last) {
            if (!out.empty()) {
                out += '\n';
            }
            unsigned current = 0;
            for (auto index = first; index < last; ++index) {
                const auto& c = characters[index];
                if (c.style != current) {
                    close(out, current);
                    open(out, c.style);
                    current = c.style;
                }
                for (const char byte : c.bytes) {
                    append_escaped(out, byte);
                }
            }
            close(out, current);
        }
        if (end < characters.size() && characters[end].bytes == "\r" &&
            end + 1 < characters.size() && characters[end + 1].bytes == "\n") {
            ++end;
        }
        start = end + 1;
    }
    return out;
}

std::string utf16be_to_utf8(std::string_view bytes) {
    const auto unit = [&bytes](std::size_t at) {
        return static_cast<std::uint32_t>((static_cast<unsigned char>(bytes[at]) << 8) |
                                          static_cast<unsigned char>(bytes[at + 1]));
    };
    const auto append = [](std::string& out, std::uint32_t code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    };
    constexpr std::uint32_t replacement = 0xFFFD;
    std::string out;
    for (std::size_t at = 0; at + 1 < bytes.size(); at += 2) {
        auto code = unit(at);
        if (code >= 0xD800 && code <= 0xDBFF) {
            if (at + 3 < bytes.size() && unit(at + 2) >= 0xDC00 && unit(at + 2) <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (unit(at + 2) - 0xDC00);
                at += 2;
            } else {
                code = replacement;
            }
        } else if (code >= 0xDC00 && code <= 0xDFFF) {
            code = replacement;
        }
        append(out, code);
    }
    return out;
}

std::string webvtt_cue_text(std::string_view text) {
    std::string out;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text.compare(index, 3, "-->") == 0) {
            out += "--&gt;";
            index += 2;
        } else if (text[index] == '\r') {
            if (index + 1 >= text.size() || text[index + 1] != '\n') {
                out += '\n';
            }
        } else {
            out += text[index];
        }
    }
    // Remove blank lines only: unlike converted text, leading spaces are kept.
    std::string kept;
    std::size_t start = 0;
    while (start <= out.size()) {
        auto end = out.find('\n', start);
        if (end == std::string::npos) {
            end = out.size();
        }
        const auto line = out.substr(start, end - start);
        if (line.find_first_not_of(" \t") != std::string::npos) {
            kept += (kept.empty() ? "" : "\n") + line;
        }
        start = end + 1;
    }
    return kept;
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
