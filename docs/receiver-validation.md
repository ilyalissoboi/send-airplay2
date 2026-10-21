# Compatibility and validation limits

Receiver observations are narrower than the library's parser tests or advertised
AirPlay support. The following summary covers completed project tests, not a
certification of every file, receiver, firmware or newly built package.

## Recorded receiver scope

| Receiver | Sender | Observed behavior |
| --- | --- | --- |
| Apple TV 4K, tvOS 26.6 | Windows 11 x64 desktop and packaged UWP | Pair/reconnect; progressive media; MP4/Matroska HLS; controls/start position; text subtitles; selected sleep/wake and interruption recovery; sender Stop and natural end |
| MacBook Pro AirPlay Receiver, macOS 26.7.1 | Windows 11 x64 desktop and packaged UWP | Pair/consent; URL-command fallback; progressive and HLS; controls/start position; Stop, natural end and observed pending-consent cancellation |
| Both receivers | Windows x64 audio-enabled hosts | Video AAC copy and DTS-core-to-stereo conversion controls; packaged video-format checks over the covered AAC/DTS/AC-3/E-AC-3/FLAC/Opus/Vorbis/PCM/TrueHD cases |
| Both receivers | Windows x64 audio-only hosts | AAC/M4A and CBR/VBR MP3 progressive controls; covered FLAC/PCM/Ogg/container/edit conversions with seeks and natural end; representative converted-FLAC Stops |
| Both receivers | Separate Screenbox integration | Per-item audio/video routing, conversion permission, mixed queues, rapid Next and Stop during transition; bounded HDR/DTS playback |

Native audio-only format coverage and focused packaged converted-FLAC checks are
different test scopes. Screenbox queue observations used its recorded development
runtime; they do not constitute a new receiver run of every published preview.
The published preview additionally has installation/activation evidence, not a
fresh full codec/receiver matrix.

Historical binary fingerprints and detailed attempt records are preserved in
Git history and the corresponding pull requests, including native
[#92](https://github.com/ilyalissoboi/send-airplay2/pull/92),
[#93](https://github.com/ilyalissoboi/send-airplay2/pull/93) and Screenbox
[#17](https://github.com/ilyalissoboi/Screenbox/pull/17). Consult the source scope
before applying a result to a regression; documentation cleanup does not widen it.

## Build coverage is separate

CI builds/tests native static/shared libraries on Windows, Linux and macOS and
builds/checks UWP x64/x86/ARM64. Linux/macOS OS-store tests use synthetic records.
This is not receiver playback from Linux/macOS hosts. ARM64 UWP lacks a device
playback result; historical x86 packaged tests covered basic casting, while the
optional conversion profile is x64-only. Android is not built or device-tested.

## Known limitations

- Receiver Stop/Home can report connection loss. Closure does not reliably
  distinguish receiver intent, sleep and a network interruption.
- Duration-based ownership correlation cannot guarantee exclusive ownership
  against a concurrent sender with indistinguishable media metadata.
- Sender playing/progress and HTTP writes do not prove rendered picture/sound.
  A historical video-freeze/audio-continues failure remains a regression signal
  if it recurs, even when state/cleanup telemetry appears healthy.
- macOS consent can recur independently of saved pairing; the cache's exact
  lifetime/key/renewal is unproven. See [consent behavior](macos-receiver-consent.md).
- Supported HDR signaling does not prove HLG playback, tone mapping, calibration
  or every HDR container/configuration.
- Short clips and bounded fragment samples do not establish sustained whole-file
  throughput, arbitrary large-file startup or operation within every cache quota.
- No audio-only speaker/RAOP, converted discrete-surround, gapless/sample-exact,
  additional receiver or Chromecast-video interoperability result is claimed.
- Package/import checks do not certify a host or codec runtime for Store submission.

Use the [manual procedure](manual-validation.md) for new receiver results and
keep source/binary identity, automation and human observations separate.
