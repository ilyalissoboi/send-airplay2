# Reference sender comparison

A reference sender can help distinguish a receiver constraint from a native
implementation defect. It is a diagnostic tool, not a dependency or compatibility
oracle for send-airplay2. Keep protocol/version differences explicit.

The implementation's researched baseline is MIT pyatv at
`b277a4c8222ecdcbaab8a24e3e713ca44765adb4`, with URL-playback research from
`robkochman/pyatv@8144c77c6cecbed4f9ba2adb5a350ad86a8f6604`.
The released pyatv 0.18.0 URL path did not establish the recorded tvOS 26 baseline;
the reference fork did. Do not assume a released or moving upstream version has
the same behavior. See [source/license provenance](dependencies.md).

## Comparison procedure

1. Install the deliberately selected reference revision in an isolated environment.
   Record its exact version/revision and use the same receiver/firmware and an
   owned fixture as the native test.
2. Scan and pair according to that sender's documented workflow. Its pairing
   store is separate; never import/export credentials as a shortcut.
3. Compare direct reference serving or a URL supplied by `airplay2-cli serve`.
   Using the native server isolates playback commands from the HTTP source path.
4. Observe picture/sound, controls, natural end and receiver state separately
   from reference log output. Repeat a native cast with matched conditions.

Example native serving command after building the CLI:

```powershell
build/Release/airplay2-cli.exe serve --address 192.0.2.10 --file clip.mp4
```

The command prints a private bearer URL for the selected receiver. Keep it
private, including in reference tools' logs. The reference sender's own timing
and control sockets also need appropriate network reachability; a failed SETUP
does not prove a codec failure.

Reference tools may print credentials or persist plaintext configuration.
Keep their storage outside tracked source and never share credential output,
PINs, bearer URLs or raw private transcripts. Do not install a moving fork merely
because an old reproduction used it; select the comparison revision deliberately.
Native regressions should normally be reproduced with this project's own tools.
