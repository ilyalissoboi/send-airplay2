// SPDX-License-Identifier: Apache-2.0
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace SendAirPlay2
{
    /// <summary>
    /// One receiver that advertised an AirPlay video service (sap2_receiver_info).
    /// Everything here is unauthenticated multicast DNS data: pairing, not
    /// discovery, pins a receiver's identity.
    /// </summary>
    public sealed class Receiver
    {
        private readonly byte[] nameBytes;

        internal Receiver(Native.ReceiverInfo info)
        {
            nameBytes = Native.CopyBytes(info.Name, checked((int)(ulong)info.NameLength));
            Id = Native.ReadUtf8(info.Id);
            Name = Encoding.UTF8.GetString(nameBytes);
            Model = Native.ReadUtf8(info.Model);
            Address = Native.ReadUtf8(info.Address);
            Port = info.Port;
            PasswordRequired = (info.Flags & Native.ReceiverPasswordRequired) != 0;
            Features = (info.Flags & Native.ReceiverHasFeatures) != 0 ? info.Features : (ulong?)null;
        }

        /// <summary>Advertised identity; stable across scans, not authenticated.</summary>
        public string Id { get; }

        /// <summary>The friendly name decoded as UTF-8; invalid bytes become U+FFFD.</summary>
        /// <remarks>Network-supplied text: escape it before display and keep it out of logs.</remarks>
        public string Name { get; }

        /// <summary>The friendly name exactly as advertised.</summary>
        public byte[] GetNameBytes() => (byte[])nameBytes.Clone();

        /// <summary>Advertised model, or empty when unknown.</summary>
        public string Model { get; }

        /// <summary>
        /// The numeric address for <see cref="CastOptions.ReceiverAddress"/>: IPv4
        /// first, otherwise a non-link-local IPv6 address; empty when it has neither.
        /// </summary>
        public string Address { get; }

        /// <summary>AirPlay control port for <see cref="Address"/>.</summary>
        public ushort Port { get; }

        /// <summary>Whether an AirPlay password is advertised.</summary>
        public bool PasswordRequired { get; }

        /// <summary>The advertised feature mask, when present; not a compatibility claim.</summary>
        public ulong? Features { get; }
    }

    /// <summary>Receiver discovery (receivers.h).</summary>
    public static class Receivers
    {
        /// <summary>SAP2_DEFAULT_DISCOVERY_MS.</summary>
        public static readonly TimeSpan DefaultDuration = TimeSpan.FromMilliseconds(5000);

        /// <summary>SAP2_MAX_DISCOVERY_MS.</summary>
        public static readonly TimeSpan MaxDuration = TimeSpan.FromMilliseconds(60000);

        /// <summary>
        /// Scans active IPv4 multicast interfaces for <paramref name="duration"/>
        /// (1 ms to 60 s) and returns the receivers that advertise AirPlay video.
        /// Blocks for the whole scan, so call it off the UI thread. An empty result
        /// does not prove that no receiver exists. Throws InvalidArgument for a bad
        /// duration and Connection when the local network setup fails.
        /// </summary>
        public static IReadOnlyList<Receiver> Discover(TimeSpan duration)
        {
            SendAirPlay2Library.RequireCompatibleLibrary();
            var list = IntPtr.Zero;
            try
            {
                SendAirPlay2Exception.ThrowIfFailed(
                    Native.sap2_discover(Cast.Milliseconds(duration), out list));
                var count = (ulong)Native.sap2_receiver_list_count(list);
                var receivers = new List<Receiver>();
                for (ulong index = 0; index < count; ++index)
                {
                    var info = new Native.ReceiverInfo
                    {
                        StructSize = (uint)Marshal.SizeOf<Native.ReceiverInfo>(),
                    };
                    SendAirPlay2Exception.ThrowIfFailed(
                        Native.sap2_receiver_list_get(list, new UIntPtr(index), ref info));
                    receivers.Add(new Receiver(info)); // Copies before the list is freed.
                }
                return receivers;
            }
            finally
            {
                Native.sap2_receiver_list_free(list);
            }
        }
    }
}
