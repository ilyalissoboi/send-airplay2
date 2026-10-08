// SPDX-License-Identifier: Apache-2.0
using System;
using System.Runtime.InteropServices;

namespace SendAirPlay2
{
    /// <summary>Host credential store results (SAP2_STORE_*).</summary>
    public enum CredentialStoreResult
    {
        /// <summary>Done.</summary>
        Ok = 0,
        /// <summary>No record under that profile.</summary>
        Absent = 1,
        /// <summary>SaveNew: a record already exists and was not replaced.</summary>
        Exists = 2,
        /// <summary>The store cannot be used now; nothing was changed.</summary>
        Unavailable = 3,
    }

    /// <summary>
    /// A host-provided credential store (D49; credentials.h), for hosts whose
    /// secure storage is reached from managed code, such as a UWP app's
    /// PasswordVault.
    /// </summary>
    /// <remarks>
    /// Records are opaque: store and return them byte for byte. A record contains
    /// the controller's private signing seed, so it belongs only in secure
    /// storage, never in plain files, logs or settings; a text-only store encodes
    /// it (for example as Base64). Managed arrays cannot be wiped reliably: the
    /// binding clears its own temporary arrays after each call, and the host is
    /// responsible for any copies it makes. A store may roam credentials; whether
    /// that is acceptable is the host's decision. Methods run synchronously on the
    /// thread that called the library (a cast's Start, Pairing.Pair or
    /// Pairing.ForgetProfile) and must not call back into this library for that
    /// operation. Exceptions are reported to the library as Unavailable.
    /// </remarks>
    public interface ICredentialStore
    {
        /// <summary>Copies the profile's record into <paramref name="buffer"/> (at
        /// least <see cref="CredentialRecords.MaxLength"/> bytes) and sets
        /// <paramref name="length"/>, or returns Absent.</summary>
        CredentialStoreResult Load(string profile, byte[] buffer, out int length);

        /// <summary>Stores a copy of <paramref name="record"/>; returns Exists, without
        /// changing anything, if the profile already has a record.</summary>
        CredentialStoreResult SaveNew(string profile, byte[] record);

        /// <summary>Deletes the profile's record; Absent if there is none.</summary>
        CredentialStoreResult Erase(string profile);
    }

    /// <summary>Credential record limits.</summary>
    public static class CredentialRecords
    {
        /// <summary>Upper bound of an encoded record, in bytes (version 1).</summary>
        public const int MaxLength = Native.CredentialRecordMax;
    }

    /// <summary>
    /// The native sap2_credential_store table for one <see cref="ICredentialStore"/>.
    /// The library copies the table; these delegates are what its function
    /// pointers refer to, so this object must stay reachable for as long as the
    /// library may call the store (until the cast is destroyed, or until a pairing
    /// or removal call returns).
    /// </summary>
    internal sealed class CredentialStoreThunks
    {
        private readonly ICredentialStore store;
        private readonly Native.StoreLoadCallback load;
        private readonly Native.StoreSaveCallback saveNew;
        private readonly Native.StoreEraseCallback erase;

        internal CredentialStoreThunks(ICredentialStore store)
        {
            this.store = store;
            load = LoadThunk;
            saveNew = SaveThunk;
            erase = EraseThunk;
        }

        internal Native.CredentialStore ToNative()
        {
            return new Native.CredentialStore
            {
                StructSize = (uint)Marshal.SizeOf<Native.CredentialStore>(),
                Context = IntPtr.Zero, // The thunks are bound to this instance.
                Load = Marshal.GetFunctionPointerForDelegate(load),
                SaveNew = Marshal.GetFunctionPointerForDelegate(saveNew),
                Erase = Marshal.GetFunctionPointerForDelegate(erase),
            };
        }

        private int LoadThunk(IntPtr context, IntPtr profile, IntPtr buffer, UIntPtr capacity,
                              IntPtr length)
        {
            var record = new byte[CredentialRecords.MaxLength];
            try
            {
                var result = store.Load(Native.ReadUtf8(profile), record, out var count);
                if (result != CredentialStoreResult.Ok)
                {
                    return (int)result;
                }
                if (count <= 0 || count > record.Length || (ulong)count > (ulong)capacity)
                {
                    Native.WriteSize(length, 0); // The library treats this as malformed.
                    return (int)CredentialStoreResult.Ok;
                }
                Marshal.Copy(record, 0, buffer, count);
                Native.WriteSize(length, count);
                return (int)CredentialStoreResult.Ok;
            }
            catch
            {
                return (int)CredentialStoreResult.Unavailable;
            }
            finally
            {
                Array.Clear(record, 0, record.Length);
            }
        }

        private int SaveThunk(IntPtr context, IntPtr profile, IntPtr record, UIntPtr length)
        {
            var copy = new byte[(int)(ulong)length];
            try
            {
                Marshal.Copy(record, copy, 0, copy.Length);
                return (int)store.SaveNew(Native.ReadUtf8(profile), copy);
            }
            catch
            {
                return (int)CredentialStoreResult.Unavailable;
            }
            finally
            {
                Array.Clear(copy, 0, copy.Length);
            }
        }

        private int EraseThunk(IntPtr context, IntPtr profile)
        {
            try
            {
                return (int)store.Erase(Native.ReadUtf8(profile));
            }
            catch
            {
                return (int)CredentialStoreResult.Unavailable;
            }
        }

        /// <summary>
        /// The table in unmanaged memory for the duration of one call. The library
        /// copies it while validating options, so freeing it after the call is safe;
        /// the delegates stay alive through this thunks object.
        /// </summary>
        internal sealed class NativeTable : IDisposable
        {
            internal IntPtr Pointer { get; private set; }

            internal NativeTable(CredentialStoreThunks? thunks)
            {
                if (thunks == null)
                {
                    return;
                }
                Pointer = Marshal.AllocHGlobal(Marshal.SizeOf<Native.CredentialStore>());
                Marshal.StructureToPtr(thunks.ToNative(), Pointer, false);
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
    }
}
