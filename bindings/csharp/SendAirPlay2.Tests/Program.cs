// SPDX-License-Identifier: Apache-2.0
// Offline tests for the C# binding: layout, ownership and lifetime, exception
// containment, credential stores and pairing refusals. Loopback and synthetic
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

    private static void VersionAndLayout()
    {
        group = "version and layout";
        Check(SendAirPlay2Library.ApiVersion == SendAirPlay2Library.BindingApiVersion,
              $"runtime API version {SendAirPlay2Library.ApiVersion} matches the binding");
        Check(SendAirPlay2Library.ResultName(ResultCode.Connection) == "connection", "result names");
        Check(SendAirPlay2Library.ResultName(ResultCode.PinTimeout) == "pin_timeout",
              "version 2 result names");

        // The native initializer writes min(our size, its size) and stores it, and
        // fills defaults at fixed offsets: a layout or size mismatch shows here.
        var cast = new SendAirPlay2.Native.CastOptions();
        var castSize = Marshal.SizeOf<SendAirPlay2.Native.CastOptions>();
        SendAirPlay2.Native.sap2_cast_options_init_sized(ref cast, new UIntPtr((uint)castSize));
        Check(cast.StructSize == castSize, $"cast options size {castSize} matches the library");
        Check(cast.ReceiverPort == 7000 && cast.StartTimeoutMs == 30000 &&
                  cast.MediaConnections == 16 && cast.CredentialStore == IntPtr.Zero,
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

    private static int Main(string[] args)
    {
        if (args.Length != 1 || !File.Exists(args[0]))
        {
            Console.Error.WriteLine("usage: SendAirPlay2.Tests PATH_TO_NATIVE_LIBRARY");
            return 2;
        }
        var libraryPath = Path.GetFullPath(args[0]);
        NativeLibrary.SetDllImportResolver(typeof(Cast).Assembly, (name, assembly, searchPath) =>
            name == "send_airplay2" ? NativeLibrary.Load(libraryPath) : IntPtr.Zero);
        try
        {
            VersionAndLayout();
            SourceOwnership();
            CallbackLifetime();
            CastWithStore();
            ProfileRemoval();
            PairingRefusals();
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
