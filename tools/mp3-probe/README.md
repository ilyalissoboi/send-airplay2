# Private MP3 inventory and direct-playback fixtures

The callback-only parser is separate from public/default format inventory and
automatic conversion. Build the offline tool and native tests with normal CMake:

~~~powershell
cmake --build build-static --config Release --target airplay2-mp3-probe mp3_audio_tests
ctest --test-dir build-static -C Release -R '^mp3_audio_tests$' --output-on-failure
build-static/Release/airplay2-mp3-probe.exe tools/mp3-probe/fixtures/cbr48.mp3
~~~

Output contains rate, channels, physical audio-frame count, coded samples,
Xing/Info presence, observed bitrate variation, locator count and metadata-derived
audible samples (or unknown). It omits the input path and metadata labels.
This inspection does not prove compressed-payload integrity or receiver support.
Locators require reservoir/decoder history before decoding; they are not byte
offsets for standalone HTTP slices.

The committed fixtures contain original repository-authored tones. Regenerate
with Python and an external development FFmpeg containing libmp3lame:

~~~powershell
python tools/mp3-probe/generate_fixtures.py
~~~

The generator reuses D97's tone authoring, independently decodes each output,
and records actual tool version, encoder options, sample counts and hashes in
provenance.json. Low-rate mono Info-tag CRC repair and the retained invalid file
are explicit in that manifest; encoded audio/trim are unchanged. No encoder or
third-party media ships with the library.

For a coordinated receiver check, use the existing codec-free C API host:

~~~powershell
build-static/Release/airplay2-api-host.exe --address RECEIVER_ADDRESS --profile SAVED_PROFILE --file tools/mp3-probe/fixtures/cbr48.mp3 --content-type audio/mpeg --start-position 8 --start-timeout-ms 30000
~~~

Repeat with vbr441.mp3. Do not enable remux/automatic audio or a dummy video.
Both stereo clips are 64 seconds, with eight-second LEFT 440 Hz, RIGHT 660 Hz,
BOTH 880 Hz, LEFT 440 Hz, BOTH 1100 Hz, RIGHT 660 Hz, LEFT 440 Hz, BOTH 880 Hz
intervals. Tones pulse for 0.75 seconds, followed by 0.25 seconds silence.
Send status/pause/play, seek 32, seek 8, then seek 58 and allow natural end.
A separate run starts from zero and receives Stop after eight seconds. Mac
pending-consent cancellation is a separate run with --cancel-after-ms 5000 and
the prompt unanswered. Require written listening/prompt observations; telemetry
alone is not an audible result. No volume setting is changed.

Keep native, packaged brokered-source and Screenbox queue gates separate.
Record exact source/binary/fixture hashes and receiver OS. Do not commit receiver
addresses, identifiers, saved credentials, PINs or bearer URLs.
