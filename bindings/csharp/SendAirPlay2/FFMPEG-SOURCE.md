# FFmpeg runtime source and rebuild inputs

This directory accompanies the optional FFmpeg 9.0.2 runtime, dynamically linked
by SendAirPlay2.StereoAac. It contains the unmodified release source archive,
the exact vcpkg overlay and patches, the restricted codec manifest/triplet,
the upstream/vcpkg notices and installed port records. `AUDIO-PROFILE.json` in
the package root records the source checksum, DLL hashes and configuration.

To rebuild the x64 UWP codec runtime on Windows with Visual Studio C++/UWP tools:

1. Check out vcpkg revision `434307da09bc05b2c86996dccc8b2351fc0d5d37` and bootstrap it.
2. Copy `codec-manifest.json` to a separate build folder as `vcpkg.json`.
3. Set `VCPKG_DOWNLOADS` to an owned writable download folder. Copy the included
   `ffmpeg-n9.0.2.tar.gz` there as `ffmpeg-ffmpeg-n9.0.2.tar.gz`; vcpkg checks the
   SHA-512 recorded in the overlay before extracting/applying patches.
4. Run `vcpkg install --x-manifest-root=BUILD --triplet=x64-uwp-codec-release
   --overlay-triplets=THIS_DIRECTORY --overlay-ports=THIS_DIRECTORY/overlay`,
   replacing the two directory placeholders with absolute paths.
5. Compare configuration flags with `AUDIO-PROFILE.json`. Preserve the dynamic
   app runtime, component, encoder/decoder and disabled
   GPL/nonfree/version-3/network/program settings.

The four runtime DLLs stay separate from the core DLL and can be replaced with
compatible rebuilt libraries. This provides source/build inputs, not a claim
of bit-for-bit reproducibility across toolchain versions or Store certification.
Retain the notices and source bundle with redistribution of this runtime.
