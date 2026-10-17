# Native audio-only URL feasibility probe

Generate an original 64-second AAC-LC stereo M4A without a video track:

```powershell
python tools/audio-only-probe/generate_fixture.py build-review/audio-only-fixture
```

FFmpeg and ffprobe must be on PATH. They are external development tools, not
new library dependencies. The Python generator uses only its standard library.
It refuses to replace existing media, writes an original 48-kHz PCM source,
encodes AAC-LC at 192 kbit/s with faststart, verifies exactly one audio stream
and no video, and records tool versions and hashes in `fixture.json`.
Generated artifacts should stay outside committed source. No private media is
needed. The original generator/test signal follows the repository Apache-2.0
license; no third-party fixture material is copied.

Build the existing `airplay2-api-host` with CMake, pair the intended receiver
through the existing credential flow if needed, and run:

```powershell
build-static/Release/airplay2-api-host.exe --address RECEIVER_ADDRESS --profile SAVED_PROFILE --file build-review/audio-only-fixture/audio-only-tones.m4a --content-type audio/mp4 --start-timeout-ms 30000
```

This uses progressive HTTP with the correct audio MIME type. Do not add
`--remux`, `--automatic-audio` or a dummy video track. The current video-remux
and automatic-audio paths still require video. Addresses and credential-profile
contents do not belong in committed results.

The fixture pulses for 0.75 seconds with a 0.25-second gap each second. Its
eight-second intervals are left 440 Hz, right 660 Hz, both 880 Hz, left 440 Hz,
both 1100 Hz, right 660 Hz, left 440 Hz and both 880 Hz. There are short fades
to avoid hard edges. The generator does not change receiver volume.

For the controls check, send `status`, `pause`, `play`, `seek 32`, `seek 8`,
then `seek 58` and allow natural end. A separate run should receive `stop`
after eight seconds. For Mac pending-consent cancellation, leave the consent
prompt unanswered and use `--cancel-after-ms 5000` on a separate run. Grant
reuse may suppress a later prompt; record whether cancellation actually happened
while consent was pending. Observe audibility and controls on each identified
receiver; sender `playing` is not proof of sound.

Record runtime/fixture hashes, source commit, receiver model/firmware, user
observations, control/end statuses and source release/cleanup counters separately.
Keep all failed attempts. This is an Apple TV/Mac URL-audio feasibility probe,
not HomePod/RAOP transport validation, automatic audio-file transcoding or a
Screenbox audio-file feature. Use written manual instructions; no Computer Use
visual confirmation is needed.
