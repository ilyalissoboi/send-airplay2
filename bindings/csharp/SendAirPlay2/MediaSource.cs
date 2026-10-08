// SPDX-License-Identifier: Apache-2.0
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace SendAirPlay2
{
    /// <summary>Stop conditions for one read; valid only inside that Read call.</summary>
    public readonly struct ReadControl
    {
        private readonly IntPtr control;

        internal ReadControl(IntPtr control)
        {
            this.control = control;
        }

        /// <summary>True when the read should end now: the server is stopping,
        /// the request was cancelled, or its deadline passed. Poll during slow work.</summary>
        public bool ShouldStop => Native.sap2_read_should_stop(control) != 0;
    }

    /// <summary>
    /// An immutable media representation served to the receiver, such as a file or
    /// a brokered StorageFile. Implement <see cref="Size"/> and <see cref="Read"/>.
    /// </summary>
    /// <remarks>
    /// Read runs concurrently on up to MediaConnections library threads, so it must
    /// be thread-safe. It must not call any <see cref="Cast"/> method of the cast it
    /// serves (Stop joins those threads). Exceptions are caught and fail that one
    /// HTTP request. A source serves one cast; create a new one per cast.
    /// </remarks>
    public abstract class MediaSource
    {
        /// <summary>Largest read the library requests (64 KiB).</summary>
        public const int MaxReadSize = 64 * 1024;

        [ThreadStatic] private static byte[]? readBuffer;

        private readonly Native.ReadAtCallback readAt;
        private readonly Native.ReleaseCallback release;
        private int released;

        /// <summary>Creates the native callback thunks.</summary>
        protected MediaSource()
        {
            readAt = ReadAtThunk;
            release = ReleaseThunk;
        }

        /// <summary>Representation size in bytes; sources above 4 GiB are allowed.</summary>
        public abstract long Size { get; }

        /// <summary>
        /// Copies up to <paramref name="count"/> bytes starting at
        /// <paramref name="offset"/> into <paramref name="buffer"/> and returns the
        /// number copied. Short reads are fine; returning 0 before the end fails the
        /// request (the receiver may retry). Do not retain the buffer.
        /// </summary>
        protected abstract int Read(long offset, byte[] buffer, int count, ReadControl control);

        /// <summary>Called exactly once: after the last read when the cast is
        /// disposed, or right away when <see cref="Cast.Create"/> fails.</summary>
        protected virtual void OnReleased()
        {
        }

        /// <summary>The library never took ownership (create failed): release now,
        /// so a source is released exactly once on every path.</summary>
        internal void ReleaseUnowned()
        {
            ReleaseThunk(IntPtr.Zero);
        }

        /// <summary>True once the library has released this source.</summary>
        public bool IsReleased => Volatile.Read(ref released) != 0;

        internal Native.MediaSource ToNative()
        {
            return new Native.MediaSource
            {
                StructSize = (uint)Marshal.SizeOf<Native.MediaSource>(),
                Context = IntPtr.Zero, // The thunks are bound to this instance.
                Size = checked((ulong)Size),
                ReadAt = Marshal.GetFunctionPointerForDelegate(readAt),
                Release = Marshal.GetFunctionPointerForDelegate(release),
            };
        }

        /// <summary>The delegates the native table points to; keep them alive.</summary>
        internal object[] Callbacks => new object[] { readAt, release, this };

        private UIntPtr ReadAtThunk(IntPtr context, ulong offset, IntPtr buffer, UIntPtr capacity,
                                    IntPtr control)
        {
            try
            {
                var count = (int)Math.Min((ulong)capacity, MaxReadSize);
                var managed = readBuffer ??= new byte[MaxReadSize];
                var read = Read(checked((long)offset), managed, count, new ReadControl(control));
                if (read <= 0 || read > count)
                {
                    return UIntPtr.Zero;
                }
                Marshal.Copy(managed, 0, buffer, read);
                return new UIntPtr((uint)read);
            }
            catch
            {
                return UIntPtr.Zero; // An exception must not unwind into native code.
            }
        }

        private void ReleaseThunk(IntPtr context)
        {
            if (Interlocked.Exchange(ref released, 1) == 0)
            {
                try
                {
                    OnReleased();
                }
                catch
                {
                    // Release runs inside sap2_cast_destroy; nothing can be reported.
                }
            }
        }
    }

    /// <summary>A regular file served by positional reads under one lock.</summary>
    /// <remarks>The file must not change while it is served. The stream is closed
    /// when the library releases the source.</remarks>
    public sealed class FileMediaSource : MediaSource
    {
        private readonly object gate = new object();
        private readonly FileStream stream;

        /// <summary>Opens <paramref name="path"/> for shared reading.</summary>
        public FileMediaSource(string path)
        {
            stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            Size = stream.Length;
        }

        /// <inheritdoc/>
        public override long Size { get; }

        /// <inheritdoc/>
        protected override int Read(long offset, byte[] buffer, int count, ReadControl control)
        {
            if (control.ShouldStop)
            {
                return 0;
            }
            lock (gate)
            {
                stream.Position = offset;
                return stream.Read(buffer, 0, count);
            }
        }

        /// <inheritdoc/>
        protected override void OnReleased()
        {
            lock (gate)
            {
                stream.Dispose();
            }
        }
    }
}
