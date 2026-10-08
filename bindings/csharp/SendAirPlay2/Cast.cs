// SPDX-License-Identifier: Apache-2.0
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace SendAirPlay2
{
    /// <summary>Options for <see cref="Cast.Create"/> (sap2_cast_options).</summary>
    public sealed class CastOptions
    {
        /// <summary>Numeric IPv4 or global/ULA IPv6 receiver address; no DNS or scope.</summary>
        public string ReceiverAddress { get; set; } = string.Empty;

        /// <summary>Receiver control port.</summary>
        public ushort ReceiverPort { get; set; } = 7000;

        /// <summary>Paired credential profile: 1..64 lower-case letters, digits and
        /// ._- characters, starting with a letter or digit.</summary>
        public string Profile { get; set; } = string.Empty;

        /// <summary>Plain type/subtype; null means "video/mp4".</summary>
        public string? ContentType { get; set; }

        /// <summary>1 ms to 120 s.</summary>
        public TimeSpan StartTimeout { get; set; } = TimeSpan.FromSeconds(30);

        /// <summary>1..16 concurrent HTTP requests.</summary>
        public int MediaConnections { get; set; } = 16;

        /// <summary>Finite and at least 0.</summary>
        public double StartPositionSeconds { get; set; }

        /// <summary>A host store, or null for the library's built-in store.</summary>
        public ICredentialStore? CredentialStore { get; set; }
    }

    /// <summary>Owns one native cast; releases it in the finalizer if not disposed.</summary>
    /// <remarks>Keeping the callback delegates here (not in <see cref="Cast"/>)
    /// keeps them reachable until sap2_cast_destroy returns, even when the handle
    /// is released by its finalizer. P/Invoke reference counting on this handle
    /// also delays destroy until every in-flight call has returned.</remarks>
    internal sealed class CastSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private object? callbacks;

        private CastSafeHandle()
            : base(true)
        {
        }

        internal void KeepAlive(object callbackOwners)
        {
            callbacks = callbackOwners;
        }

        protected override bool ReleaseHandle()
        {
            Native.sap2_cast_destroy(handle); // Stops if needed; releases the source.
            callbacks = null;                 // Only now may the delegates be collected.
            return true;
        }
    }

    /// <summary>
    /// One cast of one media source to one paired receiver (sap2_cast).
    /// </summary>
    /// <remarks>
    /// <see cref="Start"/> blocks. <see cref="GetStatus"/>, <see cref="WaitForChange"/>,
    /// the commands and <see cref="Stop"/> are safe from any thread, alongside each
    /// other and a pending start; Stop from another thread cancels a start.
    /// <see cref="Dispose"/> waits for in-flight calls, stops if needed, and releases
    /// the media source. Failures throw <see cref="SendAirPlay2Exception"/>.
    /// </remarks>
    public sealed class Cast : IDisposable
    {
        private readonly CastSafeHandle handle;

        private Cast(CastSafeHandle handle)
        {
            this.handle = handle;
        }

        /// <summary>
        /// Validates options and creates the cast; no network, credential or file
        /// access. The cast owns <paramref name="source"/> from here on; if creation
        /// fails for any reason, including a missing or older native library, the
        /// source is released at once.
        /// </summary>
        public static Cast Create(CastOptions options, MediaSource source)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }
            if (source == null)
            {
                throw new ArgumentNullException(nameof(source));
            }
            // Until sap2_cast_create succeeds the caller still owns the source; every
            // failure before that (a missing or older native library, invalid options,
            // a throwing Size) releases it here, so it is released exactly once.
            var transferred = false;
            try
            {
                SendAirPlay2Library.RequireCompatibleLibrary();
                var store = options.CredentialStore == null
                                ? null
                                : new CredentialStoreThunks(options.CredentialStore);
                var nativeOptions = new Native.CastOptions();
                Native.sap2_cast_options_init_sized(
                    ref nativeOptions, new UIntPtr((uint)Marshal.SizeOf<Native.CastOptions>()));
                if (store != null &&
                    nativeOptions.StructSize < Marshal.SizeOf<Native.CastOptions>())
                {
                    throw new NotSupportedException(
                        "The loaded library has no credential store support.");
                }
                using (var address = new Native.Utf8String(options.ReceiverAddress))
                using (var profile = new Native.Utf8String(options.Profile))
                using (var contentType = new Native.Utf8String(options.ContentType))
                using (var storeTable = new CredentialStoreThunks.NativeTable(store))
                {
                    nativeOptions.ReceiverAddress = address.Pointer;
                    nativeOptions.ReceiverPort = options.ReceiverPort;
                    nativeOptions.Profile = profile.Pointer;
                    nativeOptions.ContentType = contentType.Pointer;
                    nativeOptions.StartTimeoutMs = Milliseconds(options.StartTimeout);
                    nativeOptions.MediaConnections = (uint)Math.Max(0, options.MediaConnections);
                    nativeOptions.StartPositionSeconds = options.StartPositionSeconds;
                    nativeOptions.CredentialStore = storeTable.Pointer;
                    var nativeSource = source.ToNative();
                    var result = Native.sap2_cast_create(ref nativeOptions, ref nativeSource,
                                                         out var created);
                    if (result != (int)ResultCode.Ok)
                    {
                        created.SetHandleAsInvalid();
                        throw new SendAirPlay2Exception((ResultCode)result);
                    }
                    // The native cast owns the source now; its destroy releases it.
                    transferred = true;
                    created.KeepAlive(new object?[] { source.Callbacks, store });
                    return new Cast(created);
                }
            }
            finally
            {
                if (!transferred)
                {
                    source.ReleaseUnowned();
                }
            }
        }

        /// <summary>Loads credentials, authenticates and starts playback. Blocks
        /// until playback is confirmed, start fails, or <see cref="Stop"/> cancels it.</summary>
        public void Start()
        {
            SendAirPlay2Exception.ThrowIfFailed(Native.sap2_cast_start(handle));
        }

        /// <summary>The current snapshot.</summary>
        public CastStatus GetStatus()
        {
            var status = NewStatus();
            SendAirPlay2Exception.ThrowIfFailed(Native.sap2_cast_get_status(handle, ref status));
            return new CastStatus(status);
        }

        /// <summary>Waits until the playback state differs from
        /// <paramref name="previous"/>, the phase leaves Starting, the session ends,
        /// or <paramref name="timeout"/> passes; then returns the snapshot.</summary>
        public CastStatus WaitForChange(PlaybackState previous, TimeSpan timeout)
        {
            var status = NewStatus();
            SendAirPlay2Exception.ThrowIfFailed(Native.sap2_cast_wait_for_change(
                handle, (uint)previous, Milliseconds(timeout), ref status));
            return new CastStatus(status);
        }

        /// <summary>Asks the receiver to resume.</summary>
        public void Play() => Command(1, 0);

        /// <summary>Asks the receiver to pause.</summary>
        public void Pause() => Command(2, 0);

        /// <summary>Asks the receiver to stop; local teardown is <see cref="Stop"/>.</summary>
        public void StopPlayback() => Command(3, 0);

        /// <summary>Seeks to an absolute position; finite and at least 0.</summary>
        public void Seek(double positionSeconds) => Command(4, positionSeconds);

        /// <summary>Local teardown: cancels a pending start or command, stops the
        /// session, then the media server. Idempotent.</summary>
        public void Stop()
        {
            SendAirPlay2Exception.ThrowIfFailed(Native.sap2_cast_stop(handle));
        }

        /// <summary>Stops if needed, releases the media source and frees the cast.</summary>
        public void Dispose()
        {
            handle.Dispose();
        }

        private void Command(uint command, double positionSeconds)
        {
            SendAirPlay2Exception.ThrowIfFailed(
                Native.sap2_cast_command(handle, command, positionSeconds));
        }

        private static Native.CastStatus NewStatus()
        {
            return new Native.CastStatus { StructSize = (uint)Marshal.SizeOf<Native.CastStatus>() };
        }

        internal static uint Milliseconds(TimeSpan value)
        {
            var milliseconds = value.TotalMilliseconds;
            return milliseconds <= 0 ? 0u
                   : milliseconds >= uint.MaxValue ? uint.MaxValue
                                                   : (uint)Math.Ceiling(milliseconds);
        }
    }
}
