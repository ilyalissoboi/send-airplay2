# Audio-only test fixtures

These generators create repository-authored 64-second pulsed channel markers.
They use Python's standard library and external FFmpeg/ffprobe tools; those
executables are fixture-generation tools, not library runtime dependencies.

```sh
python tools/audio-only-probe/generate_fixture.py build-audio-fixtures/aac
python tools/audio-only-probe/generate_automatic_fixtures.py build-audio-fixtures/automatic
```

Use fresh output directories. Generators refuse to overwrite media and record
actual tool versions, input/output hashes and stream validation. The automatic
matrix covers owned AAC/FLAC/PCM/Ogg/container/edit cases. Generated output should
not be committed as a machine-specific test run. Original fixture authoring is
Apache-2.0; no private media is needed.

Each second contains 0.75 seconds of tone and a 0.25-second gap. Eight-second
intervals are LEFT 440 Hz, RIGHT 660 Hz, BOTH 880 Hz, LEFT 440 Hz, BOTH 1100 Hz,
RIGHT 660 Hz, LEFT 440 Hz and BOTH 880 Hz. Short fades avoid abrupt edges.
The edited MOV uses initial silence and an offset source interval so markers
at/after eight seconds retain their intended presentation positions.

## Receiver checks

Build `airplay2-api-host` and pair the intended receiver using its store. Legacy
progressive AAC uses an explicit MIME without a dummy video or video remux:

```powershell
build/Release/airplay2-api-host.exe --address 192.0.2.10 --profile living-room --file build-audio-fixtures/aac/audio-only-tones.m4a --content-type audio/mp4 --start-position 8
```

For automatic admission use `--automatic-audio --audio-only` and, for conversion,
a complete store/quota configuration described by the host's command help and
the [audio-only profile](../../docs/automatic-audio-only.md).

Observe RIGHT near eight seconds, pause/resume, `seek 32` (higher BOTH), `seek 8`
(RIGHT), then `seek 58` and natural end. Use a separate from-zero cast for Stop.
Record source/binary/fixture hashes and firmware separately from observations.
Only repeat pending-consent cancellation when changed code or a defect warrants
it; grant reuse is not a pending-prompt pass.

See [manual validation](../../docs/manual-validation.md) for reporting and privacy.
These checks validate the selected Mac/Apple TV URL route, not speaker/RAOP
transport or gapless playback.
