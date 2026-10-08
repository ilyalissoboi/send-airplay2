// SPDX-License-Identifier: Apache-2.0
using System;
using System.Runtime.InteropServices;

namespace SendAirPlay2
{
    /// <summary>
    /// Supplies the PIN the receiver displays. Write 4..8 ASCII digits into
    /// <paramref name="digits"/>, set <paramref name="length"/> and return true;
    /// return false to cancel. Leading zeros are significant.
    /// </summary>
    /// <remarks>Called once, on the <see cref="Pairing.Pair"/> thread, after the
    /// receiver was asked to show its PIN. The binding clears
    /// <paramref name="digits"/> afterwards; never log the PIN, and do not call
    /// back into this library from here. A char array is used rather than a
    /// string because strings cannot be wiped.</remarks>
    public delegate bool PinReader(char[] digits, out int length);

    /// <summary>Options for <see cref="Pairing.Pair"/> (sap2_pair_options).</summary>
    public sealed class PairOptions
    {
        /// <summary>Numeric IPv4 or global/ULA IPv6 receiver address; no DNS or scope.</summary>
        public string ReceiverAddress { get; set; } = string.Empty;

        /// <summary>Receiver control port.</summary>
        public ushort ReceiverPort { get; set; } = 7000;

        /// <summary>The new profile; see <see cref="CastOptions.Profile"/> for the format.</summary>
        public string Profile { get; set; } = string.Empty;

        /// <summary>Each network phase: 1 ms to 60 s.</summary>
        public TimeSpan Timeout { get; set; } = TimeSpan.FromSeconds(10);

        /// <summary>Time allowed for the PIN reader: 1 ms to 60 s, checked when it returns.</summary>
        public TimeSpan PinTimeout { get; set; } = TimeSpan.FromSeconds(60);

        /// <summary>A host store, or null for the library's built-in store.</summary>
        public ICredentialStore? CredentialStore { get; set; }
    }

    /// <summary>PIN pairing and local profile removal (pairing.h).</summary>
    public static class Pairing
    {
        /// <summary>
        /// Pairs by PIN and saves the credentials under a new profile. Blocks.
        /// Refuses an existing profile (ProfileExists) before any network work; a
        /// wrong PIN throws Authentication and saves nothing. If only the final
        /// verification fails, the saved profile is kept. Removing a profile later
        /// does not revoke the receiver's pairing.
        /// </summary>
        public static void Pair(PairOptions options, PinReader readPin)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }
            if (readPin == null)
            {
                throw new ArgumentNullException(nameof(readPin));
            }
            SendAirPlay2Library.RequireCompatibleLibrary();
            var store = options.CredentialStore == null
                            ? null
                            : new CredentialStoreThunks(options.CredentialStore);
            var pin = new PinThunk(readPin);
            var nativeOptions = new Native.PairOptions();
            Native.sap2_pair_options_init(
                ref nativeOptions, new UIntPtr((uint)Marshal.SizeOf<Native.PairOptions>()));
            using (var address = new Native.Utf8String(options.ReceiverAddress))
            using (var profile = new Native.Utf8String(options.Profile))
            using (var storeTable = new CredentialStoreThunks.NativeTable(store))
            {
                nativeOptions.ReceiverAddress = address.Pointer;
                nativeOptions.ReceiverPort = options.ReceiverPort;
                nativeOptions.Profile = profile.Pointer;
                nativeOptions.TimeoutMs = Cast.Milliseconds(options.Timeout);
                nativeOptions.PinTimeoutMs = Cast.Milliseconds(options.PinTimeout);
                nativeOptions.CredentialStore = storeTable.Pointer;
                nativeOptions.PinContext = IntPtr.Zero; // The thunk is bound to its instance.
                nativeOptions.ReadPin = pin.FunctionPointer;
                var result = Native.sap2_pair(ref nativeOptions);
                GC.KeepAlive(pin);   // Callbacks may run until sap2_pair returns.
                GC.KeepAlive(store);
                SendAirPlay2Exception.ThrowIfFailed(result);
            }
        }

        /// <summary>
        /// Deletes a profile's local credentials from <paramref name="store"/>, or
        /// from the built-in store when it is null. Returns false when there were
        /// none. No network work; the receiver's pairing is unchanged.
        /// </summary>
        public static bool ForgetProfile(string profile, ICredentialStore? store = null)
        {
            if (profile == null)
            {
                throw new ArgumentNullException(nameof(profile));
            }
            SendAirPlay2Library.RequireCompatibleLibrary();
            var thunks = store == null ? null : new CredentialStoreThunks(store);
            using (var name = new Native.Utf8String(profile))
            using (var storeTable = new CredentialStoreThunks.NativeTable(thunks))
            {
                var result = Native.sap2_forget_profile(name.Pointer, storeTable.Pointer);
                GC.KeepAlive(thunks);
                if (result == (int)ResultCode.ProfileNotFound)
                {
                    return false;
                }
                SendAirPlay2Exception.ThrowIfFailed(result);
                return true;
            }
        }

        /// <summary>Adapts a <see cref="PinReader"/> to read_pin, clearing every buffer.</summary>
        private sealed class PinThunk
        {
            private readonly PinReader reader;
            private readonly Native.ReadPinCallback callback;

            internal PinThunk(PinReader reader)
            {
                this.reader = reader;
                callback = Read;
            }

            internal IntPtr FunctionPointer => Marshal.GetFunctionPointerForDelegate(callback);

            private int Read(IntPtr context, IntPtr digits, UIntPtr capacity, IntPtr length)
            {
                var managed = new char[Native.MaxPinDigits];
                var ascii = new byte[Native.MaxPinDigits];
                try
                {
                    if (!reader(managed, out var count))
                    {
                        return (int)ResultCode.Cancelled;
                    }
                    if (count < 0 || count > managed.Length || (ulong)count > (ulong)capacity)
                    {
                        Native.WriteSize(length, managed.Length + 1); // Rejected as invalid.
                        return (int)ResultCode.Ok;
                    }
                    for (var index = 0; index < count; ++index)
                    {
                        // Non-ASCII characters become an invalid byte, which the library rejects.
                        ascii[index] = managed[index] < 128 ? (byte)managed[index] : (byte)'?';
                    }
                    Marshal.Copy(ascii, 0, digits, count);
                    Native.WriteSize(length, count);
                    return (int)ResultCode.Ok;
                }
                catch
                {
                    return (int)ResultCode.Cancelled;
                }
                finally
                {
                    Array.Clear(managed, 0, managed.Length);
                    Array.Clear(ascii, 0, ascii.Length);
                }
            }
        }
    }
}
