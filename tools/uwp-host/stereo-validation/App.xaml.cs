// SPDX-License-Identifier: Apache-2.0
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading.Tasks;
using Windows.ApplicationModel;
using Windows.ApplicationModel.Activation;
using Windows.Storage;
using Windows.Storage.Pickers;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace SendAirPlay2.UwpHost;

/// <summary>Offline AppContainer gate for the integrated core; no network or credential access.</summary>
public sealed partial class App : Application
{
    private bool running;
    public App() => InitializeComponent();
    protected override void OnLaunched(LaunchActivatedEventArgs args) => Run(args.Arguments == "pick");
    protected override void OnActivated(IActivatedEventArgs args) => Run(
        args.Kind == ActivationKind.CommandLineLaunch && args is ICommandLineActivatedEventArgs command
        && command.Operation.Arguments.Trim().EndsWith(" pick", StringComparison.Ordinal));
    private async void Run(bool pick)
    {
        if (running) { return; }
        running = true;
        Window.Current.Content = new Page();
        Window.Current.Activate();
        var lines = new List<string>();
        try
        {
            Require(SendAirPlay2Library.ApiVersion == 4 && SendAirPlay2Library.StereoAacAvailable,
                "integrated backend");
            lines.Add("api=4 stereo_aac=1");
            StorageFile? picked = null;
            if (pick)
            {
                var picker = new FileOpenPicker();
                picker.FileTypeFilter.Add(".mp4");
                picked = await picker.PickSingleFileAsync();
                Require(picked != null, "picker cancelled");
                // Only the recorded owned DTS fixture belongs to this offline test contract.
                using var stream = (await picked!.OpenReadAsync()).AsStreamForRead(0);
                var expected = await Package.Current.InstalledLocation.GetFileAsync("Fixtures\\dts.mp4");
                using var expectedStream = (await expected.OpenReadAsync()).AsStreamForRead(0);
                Require(SHA256.HashData(stream).SequenceEqual(SHA256.HashData(expectedStream)),
                    "picker fixture hash mismatch");
            }
            var sibling = await ApplicationData.Current.TemporaryFolder.CreateFileAsync(
                "sentinel-" + Guid.NewGuid().ToString("N"), CreationCollisionOption.FailIfExists);
            await FileIO.WriteTextAsync(sibling, "sibling survives cache clear");
            try
            {
                foreach (var fixture in new[] { "dts.mp4", "dts.mkv", "aac.mp4" })
                {
                    var file = await Package.Current.InstalledLocation.GetFileAsync("Fixtures\\" + fixture);
                    var probe = await StorageFileMediaSource.OpenAsync(file, 1021);
                    try
                    {
                        var tracks = await Task.Run(() => MediaTracks.Probe(probe, TimeSpan.FromSeconds(30)));
                        Require(tracks.Any(track => track.Id == 42 && track.Kind == MediaTrackKind.Audio),
                            "original audio identity");
                        Require(!probe.IsReleased, "probe borrows source");
                    }
                    finally { probe.ReleaseUnowned(); }
                    Require(probe.ReleaseCount == 1, "probe release once");
                    lines.Add("probe " + fixture + " id=42 borrowed=1 release=1");
                    foreach (var mode in fixture == "dts.mp4" ? new uint[] { 0, 1, 2, 3, 4, 5 } : new uint[] { 0, 1, 2, 3, 4 })
                    {
                        lines.Add("begin " + fixture + " mode=" + mode);
                        lines.Add(await Validate(file, fixture, mode));
                        Require(await FileIO.ReadTextAsync(sibling) == "sibling survives cache clear",
                            "sibling preservation");
                    }
                    foreach (var lifecycle in new[] { 0, 1, 2 })
                    { lines.Add(await PublicLifetime(file, fixture, lifecycle)); }
                }
            }
            finally { await sibling.DeleteAsync(StorageDeleteOption.PermanentDelete); }
            if (picked != null)
            {
                lines.Add("picked_fixture_hash_verified=1");
                lines.Add(await Validate(picked, "picker-granted-dts", 0));
            }
            foreach (var name in new[] { "send_airplay2.dll", "libcrypto-3-x64.dll", "avcodec-63.dll",
                "avformat-63.dll", "avutil-61.dll", "swresample-7.dll" })
            {
                var file = await Package.Current.InstalledLocation.GetFileAsync(name);
                using var stream = (await file.OpenReadAsync()).AsStreamForRead(0);
                lines.Add(name + " sha256=" + Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant());
            }
            lines.Add("PASS");
        }
        catch (Exception error)
        {
            // Fixed validation labels only. Callback and operating-system exception messages are omitted.
            lines.Add("FAIL type=" + error.GetType().Name);
            if (error is ValidationFailure failure) { lines.Add("scenario=" + failure.Scenario); }
            if (error is NativeValidationFailure native)
            { lines.Add("native_result=" + native.Result + " failure=" + native.Report.Failure
                + " segments=" + native.Report.AudioSegments + " bytes=" + native.Report.ResourceBytes); }
        }
        var log = await ApplicationData.Current.LocalFolder.CreateFileAsync(
            "stereo-runtime.txt", CreationCollisionOption.ReplaceExisting);
        await FileIO.WriteLinesAsync(log, lines);
        await Task.Delay(30000); // Allows external verification of the actually loaded module hashes.
        Exit();
    }
    private static async Task<string> Validate(StorageFile file, string label, uint mode)
    {
        var source = await StorageFileMediaSource.OpenAsync(file, mode == 5 ? 97 : 1021);
        TemporaryAacFragmentStore? cache = null;
        try
        {
            cache = await TemporaryAacFragmentStore.CreateAsync(failReads: mode == 2);
            var report = await Task.Run(() => ValidationNative.Run(source, cache, mode));
            Require(source.IsReleased && source.ReleaseCount == 1, "native source release once");
            Require(cache.ClearCount == 1 && cache.ReleaseCount == 1 && cache.DirectoryRemoved,
                "native cache clear release removal");
            if (mode <= 1)
            {
                Require(report.SelectedAudioId == 42 && report.AudioSegments > 0 && report.ResourceBytes > 0,
                    "converted resource output");
                Require((ulong)cache.SealCount == report.AudioSegments, "one seal per audio segment");
            }
            return "native " + label + " mode=" + mode + " failure=" + report.Failure
                + " prepare_ms=" + report.PreparationMs + " request_ms=" + report.RequestMs
                + " source_calls=" + source.ReadCalls + " source_bytes=" + source.ReadBytes
                + " resources=" + report.ResourceBytes + " segments=" + report.AudioSegments
                + " seals=" + cache.SealCount + " sealed_bytes=" + cache.SealedBytes
                + " clear=1 release=1 removed=1";
        }
        finally
        {
            // No-op after native ownership release; also contains construction/interop failures.
            cache?.ReleaseUnowned();
            source.ReleaseUnowned();
        }
    }
    private static async Task<string> PublicLifetime(StorageFile file, string label, int lifecycle)
    {
        var source = await StorageFileMediaSource.OpenAsync(file);
        TemporaryAacFragmentStore? cache = null;
        try
        {
            cache = await TemporaryAacFragmentStore.CreateAsync();
            try
            {
                using (var cast = Cast.Create(new CastOptions
                {
                    ReceiverAddress = "192.0.2.1", Profile = "offline-absent",
                    CredentialStore = new AbsentStore(), Delivery = CastDelivery.StereoAacHls,
                    AudioTrackId = 42, CacheQuotaBytes = lifecycle == 2 ? 0 : 512ul * 1024 * 1024,
                    FragmentStore = cache,
                }, source))
                {
                    if (lifecycle == 1)
                    {
                        var failed = false;
                        try { await Task.Run(() => cast.Start()); }
                        catch (SendAirPlay2Exception error) when (error.Result == ResultCode.ProfileNotFound)
                        { failed = true; }
                        Require(failed, "credential refusal before network");
                    }
                }
                if (lifecycle == 2) { throw new ValidationFailure("invalid create accepted"); }
            }
            catch (SendAirPlay2Exception error) when (lifecycle == 2 && error.Result == ResultCode.InvalidArgument) { }
            Require(source.ReleaseCount == 1 && cache.ClearCount == 1 && cache.ReleaseCount == 1
                && cache.DirectoryRemoved, "public destroy cleanup");
            return "public " + label + " lifecycle=" + lifecycle + " clear=1 release=1 removed=1";
        }
        finally { cache?.ReleaseUnowned(); source.ReleaseUnowned(); }
    }
    private static void Require(bool condition, string scenario)
    {
        if (!condition) { throw new ValidationFailure(scenario); }
    }
    private sealed class ValidationFailure(string scenario) : Exception
    {
        internal string Scenario { get; } = scenario;
    }
    private sealed class AbsentStore : ICredentialStore
    {
        public CredentialStoreResult Load(string profile, byte[] buffer, out int length)
        { length = 0; return CredentialStoreResult.Absent; }
        public CredentialStoreResult SaveNew(string profile, byte[] record) => CredentialStoreResult.Unavailable;
        public CredentialStoreResult Erase(string profile) => CredentialStoreResult.Unavailable;
    }
}

internal static unsafe class ValidationNative
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct Report
    {
        internal uint StructSize, Failure;
        internal ulong SelectedAudioId, PreparationMs, RequestMs, ResourceBytes, AudioSegments;
    }
    [DllImport("send_airplay2", EntryPoint = "sap2_runtime_validate", CallingConvention = CallingConvention.Cdecl)]
    private static extern int Validate(Native.MediaSource* source, IntPtr cache, ulong id,
        uint mode, Report* report);

    internal static Report Run(StorageFileMediaSource source, TemporaryAacFragmentStore cache, uint mode)
    {
        var nativeSource = source.ToNative();
        using var nativeCache = new AacFragmentStore.NativeTable(cache);
        var report = new Report { StructSize = (uint)Marshal.SizeOf<Report>() };
        var result = Validate(&nativeSource, nativeCache.Pointer, mode == 4 ? 11ul : 42ul, mode, &report);
        GC.KeepAlive(source.Callbacks);
        GC.KeepAlive(cache);
        if (result != 0) { throw new NativeValidationFailure(result, report); }
        return report;
    }
}

internal sealed class NativeValidationFailure(int result, ValidationNative.Report report) : Exception
{
    internal int Result { get; } = result;
    internal ValidationNative.Report Report { get; } = report;
}
