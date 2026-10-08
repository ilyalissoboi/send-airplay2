// SPDX-License-Identifier: Apache-2.0
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace SendAirPlay2
{
    /// <summary>
    /// P/Invoke declarations mirroring include/send_airplay2/*.h (API version 2,
    /// plus receivers.h).
    /// Struct layouts follow the C declarations field for field with natural
    /// alignment; every callback uses the C calling convention.
    /// </summary>
    internal static class Native
    {
        /// <summary>send_airplay2.dll on Windows, libsend_airplay2.so/.dylib elsewhere.</summary>
        internal const string Library = "send_airplay2";

        /// <summary>The header version this binding was written against.</summary>
        internal const uint ApiVersion = 2;

        /// <summary>SAP2_CREDENTIAL_RECORD_MAX: upper bound of an encoded record.</summary>
        internal const int CredentialRecordMax = 203;

        /// <summary>SAP2_MAX_PIN_DIGITS.</summary>
        internal const int MaxPinDigits = 8;

        /// <summary>SAP2_RECEIVER_PASSWORD_REQUIRED.</summary>
        internal const uint ReceiverPasswordRequired = 1;

        /// <summary>SAP2_RECEIVER_HAS_FEATURES.</summary>
        internal const uint ReceiverHasFeatures = 2;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate UIntPtr ReadAtCallback(IntPtr context, ulong offset, IntPtr buffer,
                                                 UIntPtr capacity, IntPtr control);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ReleaseCallback(IntPtr context);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int StoreLoadCallback(IntPtr context, IntPtr profile, IntPtr buffer,
                                                UIntPtr capacity, IntPtr length);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int StoreSaveCallback(IntPtr context, IntPtr profile, IntPtr record,
                                                UIntPtr length);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int StoreEraseCallback(IntPtr context, IntPtr profile);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int ReadPinCallback(IntPtr context, IntPtr digits, UIntPtr capacity,
                                              IntPtr length);

        [StructLayout(LayoutKind.Sequential)]
        internal struct MediaSource
        {
            internal uint StructSize;
            internal IntPtr Context;
            internal ulong Size;
            internal IntPtr ReadAt;
            internal IntPtr Release;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct CredentialStore
        {
            internal uint StructSize;
            internal IntPtr Context;
            internal IntPtr Load;
            internal IntPtr SaveNew;
            internal IntPtr Erase;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct CastOptions
        {
            internal uint StructSize;
            internal IntPtr ReceiverAddress;
            internal ushort ReceiverPort;
            internal IntPtr Profile;
            internal IntPtr ContentType;
            internal uint StartTimeoutMs;
            internal uint MediaConnections;
            internal double StartPositionSeconds;
            internal IntPtr CredentialStore; // Version 2.
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct CastStatus
        {
            internal uint StructSize;
            internal uint Phase;
            internal int StartResult;
            internal uint RejectedStatus;
            internal uint PlaybackState;
            internal uint EndReason;
            internal uint FailureChannel;
            internal uint FailureReason;
            internal uint Owned;
            internal uint AtEnd;
            internal uint CleanedUp;
            internal uint HasPosition;
            internal uint HasDuration;
            internal uint HasPlaybackRate;
            internal double PositionSeconds;
            internal double DurationSeconds;
            internal double PlaybackRate;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct PairOptions
        {
            internal uint StructSize;
            internal IntPtr ReceiverAddress;
            internal ushort ReceiverPort;
            internal IntPtr Profile;
            internal uint TimeoutMs;
            internal uint PinTimeoutMs;
            internal IntPtr CredentialStore;
            internal IntPtr PinContext;
            internal IntPtr ReadPin;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct ReceiverInfo
        {
            internal uint StructSize;
            internal uint Flags;
            internal IntPtr Id;
            internal IntPtr Name;
            internal UIntPtr NameLength;
            internal IntPtr Model;
            internal IntPtr Address;
            internal ushort Port;
            internal ulong Features;
        }

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint sap2_playback_api_version();

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr sap2_result_name(int result);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_read_should_stop(IntPtr control);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void sap2_cast_options_init_sized(ref CastOptions options,
                                                                 UIntPtr structSize);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_cast_create(ref CastOptions options, ref MediaSource source,
                                                    out CastSafeHandle cast);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_cast_start(CastSafeHandle cast);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_cast_get_status(CastSafeHandle cast, ref CastStatus status);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_cast_wait_for_change(CastSafeHandle cast, uint previousState,
                                                             uint timeoutMs, ref CastStatus status);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_cast_command(CastSafeHandle cast, uint command,
                                                     double positionSeconds);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_cast_stop(CastSafeHandle cast);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void sap2_cast_destroy(IntPtr cast);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void sap2_pair_options_init(ref PairOptions options,
                                                           UIntPtr structSize);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_pair(ref PairOptions options);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_forget_profile(IntPtr profile, IntPtr credentialStore);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_discover(uint durationMs, out IntPtr list);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern UIntPtr sap2_receiver_list_count(IntPtr list);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int sap2_receiver_list_get(IntPtr list, UIntPtr index,
                                                          ref ReceiverInfo info);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void sap2_receiver_list_free(IntPtr list);

        /// <summary>
        /// A NUL-terminated UTF-8 copy in unmanaged memory, freed on dispose. The
        /// library copies the strings it is given, so the copy only has to live
        /// for the duration of one call.
        /// </summary>
        internal sealed class Utf8String : IDisposable
        {
            internal IntPtr Pointer { get; private set; }

            internal Utf8String(string? value)
            {
                if (value == null)
                {
                    return;
                }
                var bytes = Encoding.UTF8.GetBytes(value);
                Pointer = Marshal.AllocHGlobal(bytes.Length + 1);
                Marshal.Copy(bytes, 0, Pointer, bytes.Length);
                Marshal.WriteByte(Pointer, bytes.Length, 0);
            }

            public void Dispose()
            {
                if (Pointer != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(Pointer);
                    Pointer = IntPtr.Zero;
                }
            }
        }

        /// <summary>A UTF-8 string borrowed from the library for one callback.</summary>
        internal static string ReadUtf8(IntPtr pointer)
        {
            if (pointer == IntPtr.Zero)
            {
                return string.Empty;
            }
            var length = 0;
            while (Marshal.ReadByte(pointer, length) != 0)
            {
                ++length;
            }
            var bytes = new byte[length];
            Marshal.Copy(pointer, bytes, 0, length);
            return Encoding.UTF8.GetString(bytes);
        }

        /// <summary>A copy of <paramref name="length"/> bytes the library owns.</summary>
        internal static byte[] CopyBytes(IntPtr pointer, int length)
        {
            var bytes = new byte[length];
            if (length > 0)
            {
                Marshal.Copy(pointer, bytes, 0, length);
            }
            return bytes;
        }

        /// <summary>Writes a size_t through a pointer the library passed in.</summary>
        internal static void WriteSize(IntPtr target, int value)
        {
            Marshal.WriteIntPtr(target, new IntPtr(value));
        }
    }
}
