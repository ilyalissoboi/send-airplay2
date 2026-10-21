# MP3 inventory and direct-playback fixtures

The callback-based probe reports MPEG layer-III framing and timeline metadata.
Build it and its offline tests with ordinary CMake:

```powershell
cmake --build build --config Release --target airplay2-mp3-probe mp3_audio_tests
ctest --test-dir build -C Release -R '^mp3_audio_tests$' --output-on-failure
build/Release/airplay2-mp3-probe.exe tools/mp3-probe/fixtures/cbr48.mp3
```

Output includes rate/channels, frame/coded-sample counts, Xing/Info presence,
bitrate variation, locators and known audible samples. It omits the input path
and metadata labels. Framing does not prove compressed-payload integrity or
receiver compatibility. Locators require decoder/reservoir history; they are
not standalone HTTP slice offsets.

The committed fixtures contain original tones. Regenerate deliberately with
Python and an external FFmpeg build providing libmp3lame:

```sh
python tools/mp3-probe/generate_fixtures.py
```

The generator independently decodes outputs and records actual tool version,
options, sample counts and hashes in `fixtures/provenance.json`. Its low-rate
mono Info-tag CRC repair and retained invalid control are explicit there.
No MP3 encoder or third-party media ships with the native library.

## Direct receiver playback

```powershell
build/Release/airplay2-api-host.exe --address 192.0.2.10 --profile living-room --file tools/mp3-probe/fixtures/cbr48.mp3 --content-type audio/mpeg --start-position 8
```

Repeat with `vbr441.mp3`. This deliberately tests original progressive bytes,
without remux, conversion or dummy video. API 6 also admits the finite validated
MP3 subset through [Apple Audio v1](../../docs/automatic-audio-only.md).

Both stereo fixtures have the 64-second LEFT/RIGHT/BOTH marker sequence described
in the [audio fixture guide](../audio-only-probe/README.md). Verify pause/resume,
seek32 higher BOTH, seek8 RIGHT, natural end after seek58, and a separate Stop.
Follow [manual validation](../../docs/manual-validation.md); distinguish native,
packaged brokered-source and player-queue results.
