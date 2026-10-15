// SPDX-License-Identifier: Apache-2.0
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Threading.Tasks;
using Windows.Storage;

namespace SendAirPlay2.UwpHost;

/// <summary>One cast's cache in an exclusively created app-owned TemporaryFolder child.</summary>
/// <remarks>Creation is asynchronous before native ownership transfer. Callback I/O uses
/// only this authorized child. Clear removes known files and the empty child, never its
/// parent or siblings. No eviction or source-file access is performed by this adapter.</remarks>
internal sealed class TemporaryAacFragmentStore : AacFragmentStore
{
    private readonly object gate = new();
    private readonly string directory;
    private readonly List<string> files = new();
    private readonly bool failReads;
    private bool cleared;
    internal int SealCount { get; private set; }
    internal int ReadCount { get; private set; }
    internal int ClearCount { get; private set; }
    internal int ReleaseCount { get; private set; }
    internal long SealedBytes { get; private set; }
    internal bool DirectoryRemoved => !Directory.Exists(directory);

    private TemporaryAacFragmentStore(StorageFolder folder, bool failReads)
    {
        directory = folder.Path;
        this.failReads = failReads;
    }
    internal static async Task<TemporaryAacFragmentStore> CreateAsync(bool failReads = false)
    {
        var folder = await ApplicationData.Current.TemporaryFolder.CreateFolderAsync(
            "sap2-" + Guid.NewGuid().ToString("N"), CreationCollisionOption.FailIfExists);
        return new TemporaryAacFragmentStore(folder, failReads);
    }
    private string FragmentPath(ulong index) => Path.Combine(directory,
        index.ToString(CultureInfo.InvariantCulture) + ".m4s");

    protected override void Seal(ulong index, byte[] bytes, ReadControl control)
    {
        lock (gate)
        {
            if (cleared || control.ShouldStop) { throw new OperationCanceledException(); }
            var path = FragmentPath(index);
            var temporary = path + ".partial";
            files.Add(temporary);
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                for (var offset = 0; offset < bytes.Length;)
                {
                    if (control.ShouldStop) { throw new OperationCanceledException(); }
                    var count = Math.Min(64 * 1024, bytes.Length - offset);
                    stream.Write(bytes, offset, count);
                    offset += count;
                }
                stream.Flush();
            }
            if (control.ShouldStop) { throw new OperationCanceledException(); }
            files.Add(path);
            File.Move(temporary, path); // Atomic publication, refuses an existing sealed fragment.
            ++SealCount;
            SealedBytes += bytes.Length;
        }
    }
    protected override int Read(ulong index, ulong offset, byte[] buffer, int count, ReadControl control)
    {
        lock (gate)
        {
            if (cleared || control.ShouldStop || offset > long.MaxValue) { return 0; }
            ++ReadCount;
            if (failReads) { throw new IOException("Injected cache read failure"); }
            using var stream = new FileStream(FragmentPath(index), FileMode.Open, FileAccess.Read, FileShare.Read);
            stream.Position = (long)offset;
            var read = stream.Read(buffer, 0, Math.Min(count, 997));
            return control.ShouldStop ? 0 : read;
        }
    }
    protected override void Clear()
    {
        lock (gate)
        {
            ++ClearCount;
            cleared = true;
            foreach (var file in files) { try { File.Delete(file); } catch { } }
            files.Clear();
            try { Directory.Delete(directory, false); } catch { }
        }
    }
    protected override void OnReleased()
    {
        lock (gate)
        {
            ++ReleaseCount;
            // Automatic copy/refusal releases an unused context without Clear.
            // Remove only our exclusively reserved, still-empty child; any
            // unexpected file remains visible to the validation assertions.
            if (!cleared && files.Count == 0)
            {
                try { Directory.Delete(directory, false); } catch { }
            }
        }
    }
}
