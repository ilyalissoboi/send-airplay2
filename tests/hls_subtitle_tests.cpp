// SPDX-License-Identifier: Apache-2.0
// Subtitle text, WebVTT segments, codec strings and multivariant playlists
// with literal expectations. Synthetic data only.
#include "hls_variant.h"
#include "mp4_demux.h"
#include "sample_entries.h"
#include "text_tracks.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace send_airplay2::detail;

int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}

void subrip_tests() {
    group = "SubRip to WebVTT";
    check(subrip_to_webvtt("Hello") == "Hello", "plain text");
    check(subrip_to_webvtt("<i>Nathan.</i>") == "<i>Nathan.</i>", "italic kept");
    check(subrip_to_webvtt("<B>Bold</B> and <u>under</u>") == "<b>Bold</b> and <u>under</u>",
          "bold and underline kept, tags lower-cased");
    check(subrip_to_webvtt("<font color=\"#ffff00\">Yellow</font>") == "Yellow", "font dropped");
    check(subrip_to_webvtt("Fish & chips < 5 -> win") == "Fish &amp; chips &lt; 5 -&gt; win",
          "&, < and > escaped, so no --> can appear");
    check(subrip_to_webvtt("Line one\r\nLine two\r\n") == "Line one\nLine two", "CR LF to LF");
    check(subrip_to_webvtt("  indented  \r\n\r\n\tnext") == "indented\nnext",
          "lines trimmed and blank lines removed");
    check(subrip_to_webvtt("   ").empty(), "whitespace only is empty");
}

void ass_tests() {
    group = "ASS to WebVTT";
    check(ass_to_webvtt("0,0,Default,,0,0,0,,Hello, world") == "Hello, world",
          "text after the eighth comma, commas in the text kept");
    check(ass_to_webvtt("1,0,Default,,0,0,0,,{\\i1}Italic{\\i0}\\Nnext\\nsoft") ==
              "Italic\nnext soft",
          "override blocks dropped, \\N a line break, \\n a space");
    check(ass_to_webvtt("2,0,Default,,0,0,0,,a\\hb") == "a\xC2\xA0"
                                                        "b",
          "\\h a no-break space");
    check(ass_to_webvtt("too,few,fields").empty(), "a short block gives no text");
}

void language_tests() {
    group = "language tags";
    check(language_tag("eng") == "en" && language_tag("fre") == "fr" &&
              language_tag("fra") == "fr" && language_tag("ger") == "de" &&
              language_tag("chi") == "zh",
          "bibliographic and terminology codes map to ISO 639-1");
    check(language_tag("haw") == "haw", "a code without a two-letter form stays");
    check(language_tag("ENG") == "en", "case-insensitive");
    check(language_tag("", "") == "und" && language_tag("e1g") == "und", "invalid input is und");
    check(language_tag("eng", "en-GB") == "en-GB", "LanguageBCP47 wins");
}

TextTrack sample_track() {
    TextTrack track;
    track.language = "en";
    track.cues = {{1'000'000, 2'500'000, "one"},
                  {5'500'000, 6'500'000, "spans"},
                  {7'000'000, 8'000'000, "two"},
                  {3'723'004'000, 3'724'000'000, "late"}};
    return track;
}

void webvtt_tests() {
    group = "WebVTT segments";
    const auto track = sample_track();
    check(webvtt_segment(track, 0, 6'000'000) ==
              "WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:0,LOCAL:00:00:00.000\n"
              "\n00:00:01.000 --> 00:00:02.500\none\n"
              "\n00:00:05.500 --> 00:00:06.500\nspans\n",
          "first window: cues overlapping [0, 6 s)");
    check(webvtt_segment(track, 6'000'000, 12'000'000) ==
              "WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:0,LOCAL:00:00:00.000\n"
              "\n00:00:05.500 --> 00:00:06.500\nspans\n"
              "\n00:00:07.000 --> 00:00:08.000\ntwo\n",
          "a cue spanning the boundary appears in both windows, with its full times");
    check(webvtt_segment(track, 12'000'000, 13'000'000) ==
              "WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:0,LOCAL:00:00:00.000\n",
          "a window without cues is header only");
    check(webvtt_segment(track, 13'000'000, std::numeric_limits<std::uint64_t>::max()) ==
              "WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:0,LOCAL:00:00:00.000\n"
              "\n01:02:03.004 --> 01:02:04.000\nlate\n",
          "the open last window, hours in the timestamp");
}

Mp4Track track_with(const std::string& codec, Bytes entry) {
    Mp4Track track;
    track.codec = codec;
    track.sample_entry = std::move(entry);
    return track;
}

void codec_string_tests() {
    group = "codec strings";
    check(codec_string(track_with("avc1", visual_sample_entry("avc1", 1920, 1080, 1920, 1080,
                                                              {1, 0x64, 0x00, 0x28, 0xff}))) ==
              "avc1.640028",
          "H.264 High 4.0");
    // hvcC: profile space 0, Main tier, profile 1; compatibility 0x60000000;
    // constraint 0x90; level 120 (4.0).
    const Bytes main_hvcc{1,   0x01, 0x60, 0,    0,    0,    0x90, 0, 0, 0,    0, 0,
                          120, 0xf0, 0,    0xfc, 0xfd, 0xf8, 0xf8, 0, 0, 0x0f, 0};
    check(codec_string(track_with("hvc1", visual_sample_entry("hvc1", 1920, 1080, 1920, 1080,
                                                              main_hvcc))) == "hvc1.1.6.L120.90",
          "HEVC Main L4.0: compatibility flags reversed, trailing zero constraints omitted");
    // High tier, Main 10 (profile 2), compatibility 0x20000000, constraint
    // 0xb0, level 150 (5.0).
    const Bytes main10_hvcc{1,   0x22, 0x20, 0,    0,    0,    0xb0, 0, 0, 0,    0, 0,
                            150, 0xf0, 0,    0xfc, 0xfd, 0xfa, 0xfa, 0, 0, 0x0f, 0};
    check(codec_string(track_with("hvc1", visual_sample_entry("hvc1", 3840, 2160, 3840, 2160,
                                                              main10_hvcc))) == "hvc1.2.4.H150.B0",
          "HEVC Main 10 high tier L5.0");
    check(codec_string(track_with("mp4a", aac_sample_entry(2, 48000, {0x11, 0x90}))) == "mp4a.40.2",
          "AAC-LC");
    check(codec_string(track_with("mp4a", aac_sample_entry(2, 24000, {0x2b, 0x11, 0x88}))) ==
              "mp4a.40.5",
          "HE-AAC (explicit SBR, object type 5)");
    check(codec_string(track_with("ec-3", {})) == "ec-3" &&
              codec_string(track_with("ac-3", {})) == "ac-3",
          "Dolby codecs are their own strings");
    try {
        (void)codec_string(track_with("avc1", Bytes(8 + 78, 0)));
        check(false, "avc1 without avcC accepted");
    } catch (const RemuxException& error) {
        check(error.reason() == RemuxFailure::malformed, "avc1 without avcC is malformed");
    }
}

void multivariant_tests() {
    group = "multivariant playlist";
    VariantStream variant;
    variant.uri = "index.m3u8";
    variant.peak_bits_per_second = 20'844'378;
    variant.average_bits_per_second = 7'408'170;
    variant.width = 1920;
    variant.height = 802;
    variant.frame_rate_milli = 23'976;
    variant.codecs = "avc1.640028,ec-3";
    std::vector<SubtitleRendition> subtitles(3);
    subtitles[0] = {"t0.m3u8", "English", "en", true, false, false};
    subtitles[1] = {"t1.m3u8", "English \"SDH\"", "en", false, false, true};
    subtitles[2] = {"t2.m3u8", "Forced", "fr", false, true, false};
    const std::string expected =
        "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-INDEPENDENT-SEGMENTS\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"English\",LANGUAGE=\"en\","
        "DEFAULT=YES,AUTOSELECT=YES,FORCED=NO,URI=\"t0.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"English SDH\",LANGUAGE=\"en\","
        "DEFAULT=NO,AUTOSELECT=YES,FORCED=NO,CHARACTERISTICS=\"public.accessibility."
        "transcribes-spoken-dialog,public.accessibility.describes-music-and-sound\","
        "URI=\"t1.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"Forced\",LANGUAGE=\"fr\","
        "DEFAULT=NO,AUTOSELECT=YES,FORCED=YES,URI=\"t2.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=20844378,AVERAGE-BANDWIDTH=7408170,"
        "CODECS=\"avc1.640028,ec-3\",RESOLUTION=1920x802,FRAME-RATE=23.976,SUBTITLES=\"subs\"\n"
        "index.m3u8\n";
    check(multivariant_playlist(variant, subtitles) == expected,
          "renditions, accessibility characteristics, quotes removed from names, stream info");
}
} // namespace

int main() {
    try {
        subrip_tests();
        ass_tests();
        language_tests();
        webvtt_tests();
        codec_string_tests();
        multivariant_tests();
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << group << "]: test infrastructure exception: " << error.what()
                  << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
