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

        private StorageFileMediaSource(IRandomAccessStreamWithContentType stream)
        {
            this.stream = stream;
            reader = stream.AsStreamForRead(0); // No extra buffering: offsets jump.
            Size = checked((long)stream.Size);
        }

        internal static async Task<StorageFileMediaSource> OpenAsync(StorageFile file)
        {
            return new StorageFileMediaSource(await file.OpenReadAsync());
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
                reader.Position = offset;
                return reader.Read(buffer, 0, count);
            }
        }

        protected override void OnReleased()
        {
            lock (gate)
            {
                reader.Dispose();
                stream.Dispose();
            }
        }
    }
}
