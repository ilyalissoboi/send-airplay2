// SPDX-License-Identifier: Apache-2.0
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace SendAirPlay2
{
    /// <summary>Host-owned per-cast storage for immutable AAC fragments.</summary>
    /// <remarks>Implementations must atomically seal each complete fragment, support
    /// concurrent positional reads, and poll the borrowed control during slow work.
    /// Clear runs after workers drain and removes only this cast's data. A store must
    /// be used by exactly one Cast.Create call. Callback exceptions are contained.
    /// Create owns failure cleanup too; do not dispose a store while its cast lives.</remarks>
    public abstract class AacFragmentStore
    {
        private const int MaxFragmentBytes = 1024 * 1024;
        private const int MaxReadBytes = 64 * 1024;
        private readonly Native.SealCallback seal;
        private readonly Native.FragmentReadCallback read;
        private readonly Native.ReleaseCallback clear;
        private readonly Native.ReleaseCallback release;
        private int cleared;
        private int released;
        [ThreadStatic] private static byte[]? readBuffer;

        /// <summary>Creates the rooted callback delegates.</summary>
        protected AacFragmentStore()
        {
            seal = SealThunk; read = ReadThunk;
            clear = _ => ClearOnce(); release = _ => ReleaseUnowned();
        }
        /// <summary>Publish a complete immutable fragment or throw; never retain control.</summary>
        protected abstract void Seal(ulong index, byte[] bytes, ReadControl control);
        /// <summary>Read up to count bytes at offset. Short reads are allowed.</summary>
        protected abstract int Read(ulong index, ulong offset, byte[] buffer, int count, ReadControl control);
        /// <summary>Remove only the objects created for this cast.</summary>
        protected abstract void Clear();
        /// <summary>Release backing resources after Clear and all callbacks.</summary>
        protected virtual void OnReleased() { }

        private int SealThunk(IntPtr context, ulong index, IntPtr bytes, UIntPtr length, IntPtr control)
        {
            try
            {
                var size = length.ToUInt64();
                var readControl = new ReadControl(control);
                if (size == 0 || size > MaxFragmentBytes || readControl.ShouldStop) { return (int)ResultCode.MediaCache; }
                Seal(index, Native.CopyBytes(bytes, (int)size), readControl);
                return readControl.ShouldStop ? (int)ResultCode.Cancelled : (int)ResultCode.Ok;
            }
            catch { return (int)ResultCode.MediaCache; }
        }
        private UIntPtr ReadThunk(IntPtr context, ulong index, ulong offset, IntPtr buffer, UIntPtr capacity, IntPtr control)
        {
            try
            {
                var size = capacity.ToUInt64();
                var readControl = new ReadControl(control);
                if (size == 0 || size > MaxReadBytes || readControl.ShouldStop) { return UIntPtr.Zero; }
                var bytes = readBuffer ?? (readBuffer = new byte[MaxReadBytes]);
                var count = Read(index, offset, bytes, (int)size, readControl);
                if (count <= 0 || count > (int)size || readControl.ShouldStop) { return UIntPtr.Zero; }
                Marshal.Copy(bytes, 0, buffer, count);
                return new UIntPtr((uint)count);
            }
            catch { return UIntPtr.Zero; }
        }
        private void ClearOnce()
        {
            if (Interlocked.Exchange(ref cleared, 1) == 0) { try { Clear(); } catch { } }
        }
        internal void ReleaseUnowned()
        {
            if (Interlocked.Exchange(ref released, 1) == 0)
            {
                ClearOnce();
                try { OnReleased(); } catch { }
            }
        }
        internal sealed class NativeTable : IDisposable
        {
            internal IntPtr Pointer { get; private set; }
            internal NativeTable(AacFragmentStore? store)
            {
                if (store == null) { return; }
                var table = new Native.FragmentStore
                {
                    StructSize = (uint)Marshal.SizeOf<Native.FragmentStore>(),
                    Seal = Marshal.GetFunctionPointerForDelegate(store.seal),
                    ReadAt = Marshal.GetFunctionPointerForDelegate(store.read),
                    Clear = Marshal.GetFunctionPointerForDelegate(store.clear),
                    Release = Marshal.GetFunctionPointerForDelegate(store.release),
                };
                Pointer = Marshal.AllocHGlobal(Marshal.SizeOf<Native.FragmentStore>());
                Marshal.StructureToPtr(table, Pointer, false);
            }
            public void Dispose()
            {
                if (Pointer != IntPtr.Zero) { Marshal.FreeHGlobal(Pointer); Pointer = IntPtr.Zero; }
            }
        }
    }

    /// <summary>Filesystem storage under a directory explicitly selected by the host.</summary>
    /// <remarks>Each instance reserves an exclusive random child directory on first
    /// seal. It deletes only its own known fragment files and empty child directory.
    /// It never recursively deletes the host's chosen directory. This desktop adapter
    /// is not evidence of packaged UWP filesystem permission or receiver compatibility.</remarks>
    public sealed class FileAacFragmentStore : AacFragmentStore
    {
        private readonly string root;
        private string? directory;
        private readonly object gate = new object();
        private readonly System.Collections.Generic.List<string> files = new System.Collections.Generic.List<string>();
        /// <summary>Choose an existing writable directory; construction performs no I/O.</summary>
        public FileAacFragmentStore(string rootDirectory)
        {
            root = Path.GetFullPath(rootDirectory ?? throw new ArgumentNullException(nameof(rootDirectory)));
        }
        private void EnsureDirectory()
        {
            if (directory != null) { return; }
            var candidate = Path.Combine(root, "sap2-" + Guid.NewGuid().ToString("N"));
            // Atomic exclusive reservation protects against adopting an existing child.
            var reservation = candidate + ".lock";
            using (new FileStream(reservation, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { }
            try
            {
                if (Directory.Exists(candidate) || File.Exists(candidate)) { throw new IOException("Cache reservation failed."); }
                Directory.CreateDirectory(candidate);
                directory = candidate;
            }
            finally { File.Delete(reservation); }
        }
        /// <inheritdoc />
        protected override void Seal(ulong index, byte[] bytes, ReadControl control)
        {
            lock (gate)
            {
                EnsureDirectory();
                var path = Path.Combine(directory!, index.ToString(System.Globalization.CultureInfo.InvariantCulture) + ".m4s");
                var temporary = path + ".partial";
                files.Add(temporary);
                using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    for (var offset = 0; offset < bytes.Length;)
                    {
                        if (control.ShouldStop) { throw new OperationCanceledException(); }
                        var count = Math.Min(64 * 1024, bytes.Length - offset);
                        stream.Write(bytes, offset, count); offset += count;
                    }
                    stream.Flush();
                }
                if (control.ShouldStop) { throw new OperationCanceledException(); }
                files.Add(path);
                File.Move(temporary, path);
            }
        }
        /// <inheritdoc />
        protected override int Read(ulong index, ulong offset, byte[] buffer, int count, ReadControl control)
        {
            lock (gate)
            {
                if (directory == null || offset > long.MaxValue || control.ShouldStop) { return 0; }
                var path = Path.Combine(directory, index.ToString(System.Globalization.CultureInfo.InvariantCulture) + ".m4s");
                using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
                {
                    stream.Position = (long)offset;
                    return stream.Read(buffer, 0, count);
                }
            }
        }
        /// <inheritdoc />
        protected override void Clear()
        {
            lock (gate)
            {
                foreach (var file in files) { try { File.Delete(file); } catch { } }
                files.Clear();
                if (directory != null) { try { Directory.Delete(directory, false); } catch { } directory = null; }
            }
        }
    }
}
