// SPDX-License-Identifier: Apache-2.0
using System;
using System.IO;
using System.Threading.Tasks;
using Windows.Storage;
using Windows.Storage.Streams;

namespace SendAirPlay2.UwpHost
{
    /// <summary>
    /// A media source over a brokered StorageFile, the access a packaged app gets
    /// from the file picker. Reads are positional under one lock; the stream is
    /// closed when the library releases the source. One source serves one cast.
    /// </summary>
    internal sealed class StorageFileMediaSource : MediaSource
    {
        private readonly object gate = new object();
        private readonly IRandomAccessStreamWithContentType stream;
        private readonly Stream reader;
        private readonly int readLimit;
        private readonly Action<long, long, int>? releasedObserver;
        internal long ReadCalls { get; private set; }
        internal long ReadBytes { get; private set; }
        internal int ReleaseCount { get; private set; }

        private StorageFileMediaSource(IRandomAccessStreamWithContentType stream, int readLimit,
                                       Action<long, long, int>? releasedObserver)
        {
            this.stream = stream;
            this.readLimit = readLimit;
            this.releasedObserver = releasedObserver;
            reader = stream.AsStreamForRead(0); // No extra buffering: offsets jump.
            Size = checked((long)stream.Size);
        }

        /// <summary>
        /// Opens one brokered source. The optional diagnostic observer receives read-call,
        /// byte and release counts after both streams close, outside the stream lock.
        /// Observer failures cannot prevent stream cleanup; the binding contains exceptions
        /// at its release-callback boundary. The observer must not retain private file data.
        /// </summary>
        internal static async Task<StorageFileMediaSource> OpenAsync(StorageFile file, int readLimit = MaxReadSize,
                                                                    Action<long, long, int>? releasedObserver = null)
        {
            if (readLimit < 1 || readLimit > MaxReadSize) { throw new ArgumentOutOfRangeException(nameof(readLimit)); }
            var stream = await file.OpenReadAsync();
            try { return new StorageFileMediaSource(stream, readLimit, releasedObserver); }
            catch { stream.Dispose(); throw; }
        }

        public override long Size { get; }

        protected override int Read(long offset, byte[] buffer, int count, ReadControl control)
        {
            if (control.ShouldStop)
            {
                return 0;
            }
            lock (gate)
            {
                if (control.ShouldStop) { return 0; }
                reader.Position = offset;
                var read = reader.Read(buffer, 0, Math.Min(count, readLimit));
                ++ReadCalls;
                ReadBytes += read;
                return control.ShouldStop ? 0 : read;
            }
        }

        protected override void OnReleased()
        {
            lock (gate)
            {
                ++ReleaseCount;
                reader.Dispose();
                stream.Dispose();
            }
            releasedObserver?.Invoke(ReadCalls, ReadBytes, ReleaseCount);
        }
    }
}
