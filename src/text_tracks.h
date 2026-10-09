// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_TEXT_TRACKS_H
#define SEND_AIRPLAY2_TEXT_TRACKS_H
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace send_airplay2::detail {
/// One subtitle cue on the presentation timeline, in microseconds.
/// `text` is WebVTT cue text (already escaped; lines separated by LF).
struct TextCue {
    std::uint64_t start_us = 0;
    std::uint64_t end_us = 0;
    std::string text;
};

/// A text subtitle track for HLS (D60 phase 3c), sorted by start time.
struct TextTrack {
    std::string name;     ///< Display name; may be empty.
    std::string language; ///< BCP 47 tag (RFC 5646), "und" when unknown.
    bool default_track = false;
    bool forced = false;
    bool hearing_impaired = false;
    std::vector<TextCue> cues;
};

/** SubRip (Matroska S_TEXT/UTF8) cue text as WebVTT cue text: <i>, <b> and
 * <u> are kept; other tags (a '<' followed by a letter or by '/' and a letter,
 * such as <font>) are dropped; '&', '<' and '>'
 * that are not part of a kept tag are escaped; CR LF becomes LF; each line is
 * trimmed of spaces and tabs, and blank lines (which would end a WebVTT cue)
 * are removed. */
[[nodiscard]] std::string subrip_to_webvtt(std::string_view text);

/** Matroska S_TEXT/WEBVTT cue text (already WebVTT) kept as it is, markup and
 * entities included: CR LF becomes LF, blank lines (which would end the cue)
 * are removed, and "-->", which cue text cannot contain, becomes "--&gt;". */
[[nodiscard]] std::string webvtt_cue_text(std::string_view text);

/// Bold, italic or underline over the characters [start, end) of a text,
/// counted in Unicode code points (3GPP Timed Text style records).
struct TextStyleRun {
    std::size_t start = 0;
    std::size_t end = 0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

/** UTF-8 subtitle text without markup (3GPP Timed Text) as WebVTT cue text:
 * '&', '<' and '>' escaped, CR LF and CR to LF, each line trimmed of spaces
 * and tabs, blank lines removed. `runs` become <b>, <i> and <u> spans, opened
 * and closed within each line (in that nesting order), so a style never
 * crosses a line break; overlapping runs combine. */
[[nodiscard]] std::string plain_text_to_webvtt(std::string_view text,
                                               const std::vector<TextStyleRun>& runs = {});

/** Big-endian UTF-16 (after a byte-order mark FE FF, as 3GPP Timed Text
 * allows) as UTF-8. Unpaired surrogates become U+FFFD; an odd trailing byte
 * is dropped. */
[[nodiscard]] std::string utf16be_to_utf8(std::string_view bytes);

/** The Text field of a Matroska S_TEXT/ASS or S_TEXT/SSA block (RFC 9559
 * codec mappings: ReadOrder, Layer, Style, Name, MarginL, MarginR, MarginV,
 * Effect, Text) as WebVTT cue text: override blocks {...} are dropped, \N
 * becomes a line break, \n a space and \h a no-break space. Styling is lost. */
[[nodiscard]] std::string ass_to_webvtt(std::string_view block);

/** A BCP 47 language tag for a Matroska language: `bcp47` (LanguageBCP47)
 * when given; otherwise the ISO 639-2 code (Language), mapped to its ISO
 * 639-1 two-letter code where one exists for common languages, as BCP 47
 * prefers, and kept as is otherwise. Empty or invalid input gives "und". */
[[nodiscard]] std::string language_tag(std::string_view iso639_2, std::string_view bcp47 = {});

/** One WebVTT segment (RFC 8216bis 3.1.4) for the presentation window
 * [start_us, end_us): the WEBVTT header, X-TIMESTAMP-MAP mapping cue time 0
 * to media time 0 (the presentation timeline starts at 0), and every cue
 * that overlaps the window with its full time range, so a cue spanning a
 * boundary appears in both segments. `end_us` of UINT64_MAX means "to the
 * end". Cue times print as HH:MM:SS.mmm. */
[[nodiscard]] std::string webvtt_segment(const TextTrack& track, std::uint64_t start_us,
                                         std::uint64_t end_us);
} // namespace send_airplay2::detail
#endif
