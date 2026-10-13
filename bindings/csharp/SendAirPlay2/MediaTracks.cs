// SPDX-License-Identifier: Apache-2.0
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace SendAirPlay2
{
    /// <summary>Original source track category.</summary>
    public enum MediaTrackKind
    {
        /// <summary>Other tracks, including subtitles.</summary>
        Other = 0,
        /// <summary>Video.</summary>
        Video = 1,
        /// <summary>Audio.</summary>
        Audio = 2,
    }
    /// <summary>Metadata before codec filtering; support is determined later.</summary>
    public sealed class MediaTrack
    {
        internal MediaTrack(Native.MediaTrack track)
        {
            Id = track.Id; Kind = (MediaTrackKind)track.Kind;
            Enabled = track.Enabled != 0; IsDefault = track.DefaultTrack != 0;
            Encoded = track.Encoded != 0; StandardTimeline = track.StandardTimeline != 0;
            Codec = Text(track.Codec); Language = Text(track.Language); Name = Text(track.Name);
        }
        private static string Text(byte[] bytes)
        {
            var length = Array.IndexOf(bytes, (byte)0);
            return Encoding.UTF8.GetString(bytes, 0, length < 0 ? bytes.Length : length);
        }
        /// <summary>Original MP4 track ID or Matroska track number.</summary>
        public ulong Id { get; }
        /// <summary>Source track category.</summary>
        public MediaTrackKind Kind { get; }
        /// <summary>Enabled in the source container.</summary>
        public bool Enabled { get; }
        /// <summary>Matroska default flag; MP4 has no equivalent here.</summary>
        public bool IsDefault { get; }
        /// <summary>Encrypted or otherwise content-encoded source.</summary>
        public bool Encoded { get; }
        /// <summary>No unsupported source timeline scaling/offset detected.</summary>
        public bool StandardTimeline { get; }
        /// <summary>Bounded untrusted codec label from the file.</summary>
        public string Codec { get; }
        /// <summary>Bounded untrusted language label from the file.</summary>
        public string Language { get; }
        /// <summary>Bounded untrusted name from the file.</summary>
        public string Name { get; }
    }
    /// <summary>Metadata-only source probing, available without codec support.</summary>
    public static class MediaTracks
    {
        /// <summary>Borrow the source for this synchronous call. Never releases it;
        /// it remains usable for Cast.Create. Deadline must be 1 ms..120 s. The host
        /// must keep the immutable source alive, and must not probe after transferring
        /// it to a cast. Cancellation also reaches controls inside slow callbacks.</summary>
        public static IReadOnlyList<MediaTrack> Probe(MediaSource source, TimeSpan timeout,
            CancellationToken cancellationToken = default)
        {
            if (source == null) { throw new ArgumentNullException(nameof(source)); }
            SendAirPlay2Library.RequireCompatibleLibrary();
            Native.CancelCallback cancel = _ => cancellationToken.IsCancellationRequested ? 1 : 0;
            var native = source.ToNative();
            IntPtr list = IntPtr.Zero;
            try
            {
                SendAirPlay2Exception.ThrowIfFailed(Native.sap2_media_probe(ref native,
                    Cast.Milliseconds(timeout), IntPtr.Zero, cancel, out list));
                var count = checked((int)Native.sap2_track_list_count(list).ToUInt64());
                var tracks = new List<MediaTrack>(count);
                for (var index = 0; index < count; ++index)
                {
                    var track = new Native.MediaTrack { StructSize = (uint)Marshal.SizeOf<Native.MediaTrack>() };
                    SendAirPlay2Exception.ThrowIfFailed(Native.sap2_track_list_get(list, new UIntPtr((uint)index), ref track));
                    tracks.Add(new MediaTrack(track));
                }
                return tracks.AsReadOnly();
            }
            finally
            {
                Native.sap2_track_list_free(list);
                GC.KeepAlive(cancel); GC.KeepAlive(source.Callbacks);
            }
        }
    }
}
