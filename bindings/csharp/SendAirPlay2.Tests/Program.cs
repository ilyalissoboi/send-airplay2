// SPDX-License-Identifier: Apache-2.0
// Offline tests for the C# binding: layout, ownership and lifetime, exception
// containment, credential stores, pairing and discovery refusals. Loopback and synthetic
// data only; the only built-in store access is one removal of an absent profile.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using SendAirPlay2;

internal static class Program
{
    private static int failures;
    private static string group = "setup";

    private static void Check(bool passed, string scenario)
    {
        if (!passed)
        {
            Console.Error.WriteLine($"FAIL [{group}]: {scenario}");
            ++failures;
        }
    }

    private static ResultCode ResultOf(Action action)
    {
        try
        {
            action();
            return ResultCode.Ok;
        }
        catch (SendAirPlay2Exception error)
        {
            return error.Result;
        }
    }

    private const string ProfileName = "csharp-binding-test";

    /// <summary>Counts reads and releases; serves zero bytes (no receiver is used).</summary>
    private sealed class CountingSource : MediaSource
    {
        public int Releases;
        public override long Size => 5_000_000_000; // Above 4 GiB: carried as 64-bit.
        protected override int Read(long offset, byte[] buffer, int count, ReadControl control) => 0;
        protected override void OnReleased() => Interlocked.Increment(ref Releases);
    }

    /// <summary>A source whose Size throws: creation fails before any native call.</summary>
    private sealed class ThrowingSizeSource : MediaSource
    {
        public int Releases;
        public override long Size => throw new IOException("synthetic size failure");
        protected override int Read(long offset, byte[] buffer, int count, ReadControl control) => 0;
        protected override void OnReleased() => Interlocked.Increment(ref Releases);
    }

    /// <summary>One-slot in-memory store with scripted behavior. Synthetic records only.</summary>
    private sealed class MemoryStore : ICredentialStore
    {
        public sealed class Counters
        {
            public int Loads, Saves, Erases;
        }

        private readonly Counters counters;
        private string? profile;
        private byte[]? record;
        public bool Unavailable;
        public bool ThrowOnLoad;

        public MemoryStore(Counters? counters = null)
        {
            this.counters = counters ?? new Counters();
        }

        public Counters Count => counters;

        public void Put(string name, byte[] bytes)
        {
            profile = name;
            record = bytes;
        }

        public bool Has => profile != null;

        public CredentialStoreResult Load(string name, byte[] buffer, out int length)
        {
            Interlocked.Increment(ref counters.Loads);
            length = 0;
            if (ThrowOnLoad)
            {
                throw new InvalidOperationException("synthetic store failure");
            }
            if (Unavailable)
            {
                return CredentialStoreResult.Unavailable;
            }
            if (profile != name || record == null)
            {
                return CredentialStoreResult.Absent;
            }
            Array.Copy(record, buffer, record.Length);
            length = record.Length;
            return CredentialStoreResult.Ok;
        }

        public CredentialStoreResult SaveNew(string name, byte[] bytes)
        {
            Interlocked.Increment(ref counters.Saves);
            if (profile != null)
            {
                return CredentialStoreResult.Exists;
            }
            profile = name;
            record = (byte[])bytes.Clone();
            return CredentialStoreResult.Ok;
        }

        public CredentialStoreResult Erase(string name)
        {
            Interlocked.Increment(ref counters.Erases);
            if (profile != name)
            {
                return CredentialStoreResult.Absent;
            }
            profile = null;
            record = null;
            return CredentialStoreResult.Ok;
        }
    }

    private static CastOptions LoopbackOptions(ICredentialStore? store) => new CastOptions
    {
        ReceiverAddress = "127.0.0.1",
        Profile = ProfileName,
        CredentialStore = store,
    };

    private sealed class CacheCounters
    {
        internal int Clears, Releases;
    }
    private sealed class CountingCache : AacFragmentStore
    {
        private readonly CacheCounters counters;
        internal CountingCache(CacheCounters counters) { this.counters = counters; }
        protected override void Seal(ulong index, byte[] bytes, ReadControl control) => throw new IOException("synthetic cache failure");
        protected override int Read(ulong index, ulong offset, byte[] buffer, int count, ReadControl control) => 0;
        protected override void Clear() => Interlocked.Increment(ref counters.Clears);
        protected override void OnReleased() => Interlocked.Increment(ref counters.Releases);
    }
    private static void ConversionOwnershipAndProbe()
    {
        group = "conversion ownership and probe";
        var counters = new CacheCounters();
        var source = new CountingSource();
        var options = LoopbackOptions(new MemoryStore(new MemoryStore.Counters()));
        options.Delivery = CastDelivery.StereoAacHls;
        options.FragmentStore = new CountingCache(counters);
        options.CacheQuotaBytes = 1024 * 1024;
        if (SendAirPlay2Library.StereoAacAvailable)
        {
            using (var cast = Cast.Create(options, source))
            {
                GC.Collect(); GC.WaitForPendingFinalizers();
                Check(counters.Clears == 0 && counters.Releases == 0, "callbacks stay rooted before Stop");
                cast.Stop(); cast.Stop();
                Check(counters.Clears == 1 && counters.Releases == 0 && cast.GetStatus().MediaCleanedUp,
                      "never-started Stop clears once, and status extension layout matches");
            }
            Check(source.Releases == 1 && counters.Clears == 1 && counters.Releases == 1,
                  "destroy releases managed source and cache exactly once");
        }
        else
        {
            Check(ResultOf(() => Cast.Create(options, source)) == ResultCode.Unsupported,
                  "codec-free library explicitly refuses conversion");
            Check(source.Releases == 1 && counters.Clears == 1 && counters.Releases == 1,
                  "managed Create failure owns cache/source cleanup");
        }
        var borrowed = new CountingSource();
        var cancelled = new CancellationToken(true);
        Check(ResultOf(() => MediaTracks.Probe(borrowed, TimeSpan.FromSeconds(1), cancelled)) == ResultCode.Cancelled,
              "probe cancellation is contained at the native boundary");
        Check(borrowed.Releases == 0, "probe borrows source even on failure");
        using (Cast.Create(LoopbackOptions(new MemoryStore(new MemoryStore.Counters())), borrowed)) { }
        Check(borrowed.Releases == 1, "borrowed probe source can subsequently transfer to a cast");
    }

    private static void FilesystemCacheCallbacks()
    {
        group = "filesystem cache callbacks";
        var root = Path.Combine(Path.GetTempPath(), "sap2-managed-cache-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        var sibling = Path.Combine(root, "keep.txt");
        File.WriteAllText(sibling, "host-owned sibling");
        var cache = new FileAacFragmentStore(root);
        var checkedCallbacks = false;
        try
        {
            using (var table = new AacFragmentStore.NativeTable(cache))
            {
                var callbacks = Marshal.PtrToStructure<SendAirPlay2.Native.FragmentStore>(table.Pointer);
                var seal = Marshal.GetDelegateForFunctionPointer<SendAirPlay2.Native.SealCallback>(callbacks.Seal);
                var read = Marshal.GetDelegateForFunctionPointer<SendAirPlay2.Native.FragmentReadCallback>(callbacks.ReadAt);
                SendAirPlay2.Native.ReadAtCallback sourceRead = (_, offset, output, capacity, control) =>
                {
                    if (!checkedCallbacks)
                    {
                        checkedCallbacks = true;
                        var bytes = Marshal.AllocHGlobal(6);
                        var result = Marshal.AllocHGlobal(4);
                        try
                        {
                            Marshal.Copy(new byte[] { 10, 20, 30, 40, 50, 60 }, 0, bytes, 6);
                            Check(seal(IntPtr.Zero, 9, bytes, new UIntPtr(6), control) == (int)ResultCode.Ok,
                                  "managed filesystem seal accepts real borrowed native control");
                            Check(read(IntPtr.Zero, 9, 2, result, new UIntPtr(4), control).ToUInt64() == 4 &&
                                  Marshal.ReadByte(result) == 30 && Marshal.ReadByte(result, 3) == 60,
                                  "managed cache preserves positional bytes");
                            Check(seal(IntPtr.Zero, 9, bytes, new UIntPtr(6), control) == (int)ResultCode.MediaCache,
                                  "duplicate fragment exception is contained in seal thunk");
                        }
                        finally { Marshal.FreeHGlobal(bytes); Marshal.FreeHGlobal(result); }
                    }
                    return UIntPtr.Zero; // Probe failure is expected; only its borrowed control is needed.
                };
                var source = new SendAirPlay2.Native.MediaSource
                {
                    StructSize = (uint)Marshal.SizeOf<SendAirPlay2.Native.MediaSource>(), Size = 4096,
                    ReadAt = Marshal.GetFunctionPointerForDelegate(sourceRead)
                };
                SendAirPlay2.Native.CancelCallback cancel = _ => 0;
                var probeResult = SendAirPlay2.Native.sap2_media_probe(ref source, 10000, IntPtr.Zero, cancel, out var tracks);
                SendAirPlay2.Native.sap2_track_list_free(tracks);
                Check(probeResult != 0 && checkedCallbacks, "native probe supplied callback control without taking source ownership");
                GC.KeepAlive(sourceRead); GC.KeepAlive(cancel);
            }
        }
        finally { cache.ReleaseUnowned(); }
        Check(File.Exists(sibling) && Directory.GetDirectories(root).Length == 0,
              "managed clear removes cast child and keeps host-owned sibling");
        File.Delete(sibling); Directory.Delete(root, false);
    }

    private sealed class OwnedFixtureSource : MediaSource
    {
        private readonly byte[] bytes;
        internal int Releases;
        internal OwnedFixtureSource(string path) { bytes = File.ReadAllBytes(path); }
        public override long Size => bytes.Length;
        protected override int Read(long offset, byte[] buffer, int count, ReadControl control)
        {
            if (control.ShouldStop || offset >= bytes.Length) { return 0; }
            count = Math.Min(count, bytes.Length - checked((int)offset));
            Array.Copy(bytes, offset, buffer, 0, count);
            return count;
        }
        protected override void OnReleased() => ++Releases;
    }
    private static void AutomaticAudioChecks(string fixtures)
    {
        group = "automatic audio public binding";
        var unused = new CacheCounters();
        bool rejectedNull = false;
        try { AudioDelivery.Check(null!, TimeSpan.FromSeconds(30), fragmentStore: new CountingCache(unused)); }
        catch (ArgumentNullException) { rejectedNull = true; }
        Check(rejectedNull && unused.Releases == 1 && unused.Clears == 0,
              "argument failure releases unused managed check storage without Clear");
        var source = new OwnedFixtureSource(Path.Combine(fixtures, "audio-policy", "aac-48000-2.mp4"));
        var counters = new CacheCounters();
        var decision = AudioDelivery.Check(source, TimeSpan.FromSeconds(30), fragmentStore: new CountingCache(counters),
            permission: AudioConversionPermission.PreserveOriginal);
        Check(decision.Action == AudioDeliveryAction.Copy && decision.Reason == AudioDeliveryReason.ProfileCopy &&
              decision.SelectedAudioId == 42 && decision.SourceFamily == AudioFamily.Aac &&
              decision.OutputFamily == AudioFamily.Aac && decision.OutputChannels == 2,
              "managed decision fields match native selected copy");
        Check(source.Releases == 0 && counters.Clears == 0 && counters.Releases == 1,
              "offline copy borrows source and releases unused managed store without Clear");
        var options = LoopbackOptions(new MemoryStore());
        options.Delivery = CastDelivery.AutomaticAudioHls;
        counters = new CacheCounters();
        options.FragmentStore = new CountingCache(counters);
        using (var cast = Cast.Create(options, source))
        {
            Check(cast.GetStatus().AudioDecision == null, "no decision before preparation");
            cast.Stop(); cast.Stop();
        }
        Check(source.Releases == 1 && counters.Clears == 0 && counters.Releases == 1,
              "unstarted automatic handle releases source/store exactly once without cache cleanup");
        var dts = new OwnedFixtureSource(Path.Combine(fixtures, "decoder", "dts-commentary-first.mp4"));
        Check(ResultOf(() => AudioDelivery.Check(dts, TimeSpan.FromSeconds(30))) == ResultCode.AudioSelectionRequired,
              "managed ambiguity precedes eligibility filtering");
        Check(ResultOf(() => AudioDelivery.Check(dts, TimeSpan.FromSeconds(30), audioTrackId: 42)) ==
              (SendAirPlay2Library.StereoAacAvailable ? ResultCode.MediaCache : ResultCode.Unsupported),
              "managed conversion checks backend/storage after resolving original ID");
        using (var cancel = new CancellationTokenSource())
        {
            cancel.Cancel();
            Check(ResultOf(() => AudioDelivery.Check(dts, TimeSpan.FromSeconds(30), cancellationToken: cancel.Token)) == ResultCode.Cancelled,
                  "offline managed check cancels before source work");
        }
        Check(dts.Releases == 0, "failed offline checks preserve source ownership");
        dts.ReleaseUnowned();
    }
    private static void VersionAndLayout()
    {
        group = "version and layout";
        Check(SendAirPlay2Library.ApiVersion == SendAirPlay2Library.BindingApiVersion,
              $"runtime API version {SendAirPlay2Library.ApiVersion} matches the binding");
        Check(SendAirPlay2Library.ResultName(ResultCode.Connection) == "connection", "result names");
        Check(SendAirPlay2Library.ResultName(ResultCode.PinTimeout) == "pin_timeout",
              "version 2 result names");
        Check(SendAirPlay2Library.ResultName(ResultCode.MediaUnsupported) == "media_unsupported" &&
                  SendAirPlay2Library.ResultName(ResultCode.MediaMalformed) == "media_malformed",
              "version 3 result names");

        // The native initializer writes min(our size, its size) and stores it, and
        // fills defaults at fixed offsets: a layout or size mismatch shows here.
        var cast = new SendAirPlay2.Native.CastOptions();
        var castSize = Marshal.SizeOf<SendAirPlay2.Native.CastOptions>();
        SendAirPlay2.Native.sap2_cast_options_init_sized(ref cast, new UIntPtr((uint)castSize));
        Check(cast.StructSize == castSize, $"cast options size {castSize} matches the library");
        Check(castSize == (IntPtr.Size == 8 ? 120 : 96) &&
              Marshal.SizeOf<SendAirPlay2.Native.CastStatus>() == 144 &&
              Marshal.SizeOf<SendAirPlay2.Native.AudioDecision>() == 40 &&
              Marshal.SizeOf<SendAirPlay2.Native.AudioOptions>() == 48,
              "literal v5 option/status/decision sizes preserve both native ABIs");
        Check(cast.ReceiverPort == 7000 && cast.StartTimeoutMs == 30000 &&
                  cast.MediaConnections == 16 && cast.CredentialStore == IntPtr.Zero &&
                  cast.Delivery == (uint)CastDelivery.Progressive,
              "cast option defaults land on the expected fields");
        var pair = new SendAirPlay2.Native.PairOptions();
        var pairSize = Marshal.SizeOf<SendAirPlay2.Native.PairOptions>();
        SendAirPlay2.Native.sap2_pair_options_init(ref pair, new UIntPtr((uint)pairSize));
        Check(pair.StructSize == pairSize, $"pair options size {pairSize} matches the library");
        Check(pair.ReceiverPort == 7000 && pair.TimeoutMs == 10000 && pair.PinTimeoutMs == 60000 &&
                  pair.ReadPin == IntPtr.Zero,
              "pair option defaults land on the expected fields");
    }

    private static void SourceOwnership()
    {
        group = "source ownership";
        var rejected = new CountingSource();
        var result = ResultOf(() => Cast.Create(new CastOptions { ReceiverAddress = "" }, rejected));
        Check(result == ResultCode.InvalidArgument, "empty address is refused");
        Check(rejected.Releases == 1 && rejected.IsReleased,
              "a source is released once when create fails");

        var throwing = new ThrowingSizeSource();
        var thrown = false;
        try
        {
            Cast.Create(LoopbackOptions(new MemoryStore()), throwing).Dispose();
        }
        catch (IOException)
        {
            thrown = true;
        }
        Check(thrown && throwing.Releases == 1,
              "a managed failure before the native create still releases the source once");

        var source = new CountingSource();
        using (var cast = Cast.Create(LoopbackOptions(new MemoryStore()), source))
        {
            var status = cast.GetStatus();
            Check(status.Phase == CastPhase.Created && status.StartResult == ResultCode.Ok &&
                      status.PlaybackState == PlaybackState.None,
                  "created snapshot");
            Check(ResultOf(cast.Pause) == ResultCode.InvalidState, "command before start");
            Check(source.Releases == 0, "the cast owns the source until disposed");
        }
        Check(source.Releases == 1, "dispose releases the source exactly once");

        var temporary = Path.GetTempFileName();
        try
        {
            File.WriteAllBytes(temporary, new byte[] { 1, 2, 3 });
            var file = new FileMediaSource(temporary);
            Check(file.Size == 3, "file source size");
            Cast.Create(LoopbackOptions(new MemoryStore()), file).Dispose();
            Check(file.IsReleased, "file source released (stream closed) with the cast");
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    /// <summary>Creates a cast whose only references to its store and source are
    /// inside the cast, so a GC would collect them if the binding failed to keep
    /// its delegates alive.</summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static Cast CreateDetached(MemoryStore.Counters counters, out WeakReference<CountingSource> source)
    {
        var detached = new CountingSource();
        source = new WeakReference<CountingSource>(detached);
        return Cast.Create(LoopbackOptions(new MemoryStore(counters)), detached);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void AbandonCast(CountingSource source)
    {
        Cast.Create(LoopbackOptions(new MemoryStore()), source); // Never disposed.
    }

    private static void ForceCollection()
    {
        for (var pass = 0; pass < 3; ++pass)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }
    }

    private static void CallbackLifetime()
    {
        group = "callback lifetime";
        var counters = new MemoryStore.Counters();
        using (var cast = CreateDetached(counters, out _))
        {
            ForceCollection();
            Check(ResultOf(cast.Start) == ResultCode.ProfileNotFound,
                  "the store still answers after forced collections");
            Check(counters.Loads == 1, "start called the detached store once");
        }

        var abandoned = new CountingSource();
        AbandonCast(abandoned);
        ForceCollection();
        Check(abandoned.Releases == 1, "an undisposed cast is destroyed by its finalizer, " +
                                       "releasing the source through a live delegate");
    }

    private static void CastWithStore()
    {
        group = "cast with a host store";
        var store = new MemoryStore();
        using (var cast = Cast.Create(LoopbackOptions(store), new CountingSource()))
        {
            Check(store.Count.Loads == 0, "create does not touch the store");
            Check(ResultOf(cast.Start) == ResultCode.ProfileNotFound,
                  "empty host store: profile_not_found on every platform");
            var status = cast.GetStatus();
            Check(status.Phase == CastPhase.StartFailed &&
                      status.StartResult == ResultCode.ProfileNotFound,
                  "failed start snapshot");
            Check(store.Count.Loads == 1 && store.Count.Saves == 0, "start loads once, never saves");
            Check(ResultOf(cast.Start) == ResultCode.InvalidState, "start is valid once");
        }

        var unavailable = new MemoryStore { Unavailable = true };
        using (var cast = Cast.Create(LoopbackOptions(unavailable), new CountingSource()))
        {
            Check(ResultOf(cast.Start) == ResultCode.CredentialStore, "unavailable store");
        }
        var throwing = new MemoryStore { ThrowOnLoad = true };
        using (var cast = Cast.Create(LoopbackOptions(throwing), new CountingSource()))
        {
            Check(ResultOf(cast.Start) == ResultCode.CredentialStore,
                  "an exception in Load is contained and reported as credential_store");
        }
        var malformed = new MemoryStore();
        malformed.Put(ProfileName, new byte[16]);
        using (var cast = Cast.Create(LoopbackOptions(malformed), new CountingSource()))
        {
            Check(ResultOf(cast.Start) == ResultCode.CredentialStore, "malformed record");
        }

        var remuxStore = new MemoryStore();
        var remux = LoopbackOptions(remuxStore);
        remux.Delivery = CastDelivery.HlsRemux;
        using (var cast = Cast.Create(remux, new CountingSource()))
        {
            Check(ResultOf(cast.Start) == ResultCode.ProfileNotFound && remuxStore.Count.Loads == 1,
                  "HLS delivery: credentials load before the remux reads the source");
        }
        var unknown = LoopbackOptions(new MemoryStore());
        unknown.Delivery = (CastDelivery)7;
        var unknownSource = new CountingSource();
        try
        {
            using (Cast.Create(unknown, unknownSource))
            {
            }
            Check(false, "an unknown delivery is refused");
        }
        catch (SendAirPlay2Exception error)
        {
            Check(error.Result == ResultCode.InvalidArgument && unknownSource.Releases == 1,
                  "an unknown delivery is invalid_argument and releases the source");
        }
    }

    private static void ProfileRemoval()
    {
        group = "profile removal";
        var store = new MemoryStore();
        Check(!Pairing.ForgetProfile(ProfileName, store), "absent profile returns false");
        store.Put(ProfileName, new byte[16]);
        Check(Pairing.ForgetProfile(ProfileName, store) && !store.Has, "stored profile is deleted");
        var erasesBefore = store.Count.Erases;
        Check(ResultOf(() => Pairing.ForgetProfile("Not-Valid", store)) == ResultCode.InvalidArgument &&
                  store.Count.Erases == erasesBefore,
              "invalid profile is refused before calling the store");
        try
        {
            Check(!Pairing.ForgetProfile("csharp-binding-absent-profile"),
                  "built-in store: absent profile returns false");
        }
        catch (SendAirPlay2Exception error)
        {
            Check(error.Result == ResultCode.Unsupported, "built-in store unsupported off Windows");
        }
    }

    private static void PairingRefusals()
    {
        group = "pairing refusals";
        var pinCalls = 0;
        bool ReadPin(char[] digits, out int length)
        {
            ++pinCalls;
            "0123".CopyTo(0, digits, 0, 4);
            length = 4;
            return true;
        }
        PairOptions Options(ICredentialStore store) => new PairOptions
        {
            ReceiverAddress = "127.0.0.1",
            ReceiverPort = 9, // Discard port: nothing listens on loopback.
            Profile = ProfileName,
            Timeout = TimeSpan.FromSeconds(2),
            CredentialStore = store,
        };

        var store = new MemoryStore();
        var options = Options(store);
        options.Timeout = TimeSpan.Zero;
        Check(ResultOf(() => Pairing.Pair(options, ReadPin)) == ResultCode.InvalidArgument,
              "zero timeout");
        options = Options(store);
        options.Profile = "Not-Valid";
        Check(ResultOf(() => Pairing.Pair(options, ReadPin)) == ResultCode.InvalidArgument,
              "invalid profile");

        store.Put(ProfileName, new byte[16]);
        Check(ResultOf(() => Pairing.Pair(Options(store), ReadPin)) == ResultCode.CredentialStore,
              "malformed existing record stops pairing");
        var empty = new MemoryStore();
        Check(ResultOf(() => Pairing.Pair(Options(empty), ReadPin)) == ResultCode.Connection,
              "unreachable receiver");
        Check(pinCalls == 0 && store.Count.Saves == 0 && empty.Count.Saves == 0,
              "no PIN is requested and nothing is saved before the receiver answers");
    }

    /// <summary>Layout and duration refusals only: no scan starts, so nothing is sent.</summary>
    private static void DiscoveryRefusals()
    {
        group = "discovery refusals";
        // sap2_receiver_info: two uint32, five pointer-sized fields, a uint16 and a
        // uint64 at its natural alignment (offset 48 on 64-bit, 32 on 32-bit).
        var expectedInfoSize = IntPtr.Size == 8 ? 64 : 40;
        var infoSize = Marshal.SizeOf<SendAirPlay2.Native.ReceiverInfo>();
        Check(infoSize == expectedInfoSize, $"receiver info size {infoSize} is {expectedInfoSize}");
        Check(ResultOf(() => Receivers.Discover(TimeSpan.Zero)) == ResultCode.InvalidArgument,
              "zero duration refused");
        Check(ResultOf(() => Receivers.Discover(Receivers.MaxDuration + TimeSpan.FromMilliseconds(1))) ==
                  ResultCode.InvalidArgument,
              "duration above the maximum refused");
        Check(ResultOf(() => Receivers.Discover(TimeSpan.FromMilliseconds(-5))) ==
                  ResultCode.InvalidArgument,
              "negative duration refused");
    }

    private static int Main(string[] args)
    {
        if (args.Length != 2 || !File.Exists(args[0]))
        {
            Console.Error.WriteLine("usage: SendAirPlay2.Tests PATH_TO_NATIVE_LIBRARY FIXTURES_ROOT");
            return 2;
        }
        var libraryPath = Path.GetFullPath(args[0]);
        NativeLibrary.SetDllImportResolver(typeof(Cast).Assembly, (name, assembly, searchPath) =>
            name == "send_airplay2" ? NativeLibrary.Load(libraryPath) : IntPtr.Zero);
        try
        {
            VersionAndLayout();
            AutomaticAudioChecks(args[1]);
            ConversionOwnershipAndProbe();
            FilesystemCacheCallbacks();
            SourceOwnership();
            CallbackLifetime();
            CastWithStore();
            ProfileRemoval();
            PairingRefusals();
            DiscoveryRefusals();
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Unexpected exception [{group}]: {error.GetType().Name}: {error.Message}");
            return 1;
        }
        if (failures != 0)
        {
            Console.Error.WriteLine($"{failures} failure(s)");
            return 1;
        }
        Console.WriteLine("csharp binding tests passed");
        return 0;
    }
}
