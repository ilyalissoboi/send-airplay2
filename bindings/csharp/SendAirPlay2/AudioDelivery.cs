// SPDX-License-Identifier: Apache-2.0
using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace SendAirPlay2
{
    /// <summary>Explicit delivery profile, never inferred from receiver discovery.</summary>
    public enum AudioDeliveryProfile
    {
        /// <summary>The library's finite version-one Apple HLS video profile.</summary>
        AppleHlsV1 = 1,
        /// <summary>Finite audio-only progressive or permitted stereo AAC HLS profile.</summary>
        AppleAudioV1 = 2,
    }
    /// <summary>Whether automatic delivery may lose original surround/lossless audio.</summary>
    public enum AudioConversionPermission
    {
        /// <summary>Permit conversion to stereo AAC.</summary>
        AllowStereo = 0,
        /// <summary>Refuse preparation when conversion is required.</summary>
        PreserveOriginal = 1,
    }
    /// <summary>Resolved preparation route.</summary>
    public enum AudioDeliveryAction
    {
        /// <summary>No enabled audio.</summary>
        VideoOnly = 1,
        /// <summary>Copy the selected original audio.</summary>
        Copy = 2,
        /// <summary>Convert the selected original audio to stereo AAC.</summary>
        StereoAac = 3,
        /// <summary>Serve the validated original audio file progressively.</summary>
        Progressive = 4,
    }
    /// <summary>Fixed preparation reason, not receiver rejection.</summary>
    public enum AudioDeliveryReason
    {
        /// <summary>No enabled audio track.</summary>
        NoEnabledAudio = 1,
        /// <summary>The selected audio satisfies the profile's copy table.</summary>
        ProfileCopy = 2,
        /// <summary>The profile requires conversion of this selected input.</summary>
        ProfileRequiresStereo = 3,
        /// <summary>The profile admits the original progressive audio representation.</summary>
        ProfileProgressive = 4,
    }
    /// <summary>Validated selected audio family.</summary>
    public enum AudioFamily
    {
        /// <summary>No audio.</summary>
        None = 0,
        /// <summary>AAC.</summary>
        Aac = 1,
        /// <summary>AC-3.</summary>
        Ac3 = 2,
        /// <summary>E-AC-3.</summary>
        Eac3 = 3,
        /// <summary>Validated DTS subset.</summary>
        Dts = 4,
        /// <summary>FLAC.</summary>
        Flac = 5,
        /// <summary>Opus.</summary>
        Opus = 6,
        /// <summary>Vorbis.</summary>
        Vorbis = 7,
        /// <summary>Validated PCM subset.</summary>
        Pcm = 8,
        /// <summary>TrueHD.</summary>
        TrueHd = 9,
        /// <summary>Validated MPEG layer III subset.</summary>
        Mp3 = 10,
    }
    /// <summary>Successful preparation decision; not full-file decode or receiver proof.</summary>
    public sealed class AudioDecision
    {
        internal AudioDecision(Native.AudioDecision decision)
        {
            Profile = (AudioDeliveryProfile)decision.Profile;
            Action = (AudioDeliveryAction)decision.Action;
            Reason = (AudioDeliveryReason)decision.Reason;
            SourceFamily = (AudioFamily)decision.SourceFamily;
            OutputFamily = (AudioFamily)decision.OutputFamily;
            OutputChannels = decision.OutputChannels;
            SelectedAudioId = decision.SelectedAudioId;
        }
        /// <summary>Explicit profile used for preparation.</summary>
        public AudioDeliveryProfile Profile { get; }
        /// <summary>Resolved route.</summary>
        public AudioDeliveryAction Action { get; }
        /// <summary>Fixed preparation reason.</summary>
        public AudioDeliveryReason Reason { get; }
        /// <summary>Selected source family.</summary>
        public AudioFamily SourceFamily { get; }
        /// <summary>Prepared output family.</summary>
        public AudioFamily OutputFamily { get; }
        /// <summary>Prepared channel count, zero for video-only.</summary>
        public uint OutputChannels { get; }
        /// <summary>Original selected source ID, zero for video-only.</summary>
        public ulong SelectedAudioId { get; }
    }
    /// <summary>Automatic audio preparation without credentials or receiver access.</summary>
    public static class AudioDelivery
    {
        /// <summary>Borrow the immutable source, prepare and discard a presentation,
        /// returning its decision. Later casting prepares again. Timeout is 1 ms..120 s.
        /// Copy requires no codec backend or cache. Conversion requires a dedicated
        /// store/quota. This call consumes the offered managed store on all paths;
        /// accepted conversion clears it after draining workers, unused copy releases
        /// it without Clear. The source remains usable and is never released here.</summary>
        public static AudioDecision Check(MediaSource source, TimeSpan timeout,
            ulong? audioTrackId = null, AacFragmentStore? fragmentStore = null,
            ulong cacheQuotaBytes = 0,
            AudioConversionPermission permission = AudioConversionPermission.AllowStereo,
            AudioDeliveryProfile profile = AudioDeliveryProfile.AppleHlsV1,
            CancellationToken cancellationToken = default)
        {
            Native.CancelCallback cancel = _ => cancellationToken.IsCancellationRequested ? 1 : 0;
            try
            {
                if (source == null) { throw new ArgumentNullException(nameof(source)); }
                SendAirPlay2Library.RequireCompatibleLibrary();
                if (audioTrackId == 0) { throw new ArgumentOutOfRangeException(nameof(audioTrackId)); }
                using (var table = new AacFragmentStore.NativeTable(fragmentStore))
                {
                    var options = new Native.AudioOptions
                    {
                        StructSize = (uint)Marshal.SizeOf<Native.AudioOptions>(),
                        Profile = (uint)profile, ConversionPermission = (uint)permission,
                        AudioTrackId = audioTrackId ?? 0, FragmentStore = table.Pointer,
                        CacheQuotaBytes = cacheQuotaBytes, TimeoutMs = Cast.Milliseconds(timeout),
                    };
                    var nativeSource = source.ToNative();
                    var decision = new Native.AudioDecision { StructSize = (uint)Marshal.SizeOf<Native.AudioDecision>() };
                    SendAirPlay2Exception.ThrowIfFailed(Native.sap2_media_check_audio(
                        ref nativeSource, ref options, IntPtr.Zero, cancel, ref decision));
                    return new AudioDecision(decision);
                }
            }
            finally
            {
                fragmentStore?.ReleaseAfterNative();
                GC.KeepAlive(cancel); GC.KeepAlive(source?.Callbacks); GC.KeepAlive(fragmentStore);
            }
        }
    }
}
