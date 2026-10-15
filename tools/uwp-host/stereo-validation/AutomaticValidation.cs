// SPDX-License-Identifier: Apache-2.0
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading.Tasks;
using Windows.ApplicationModel;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace SendAirPlay2.UwpHost;

public sealed partial class App
{
    private sealed record AutomaticCase(string Group, string File, uint Action, uint Family,
        uint Channels, ulong Segments, bool Stress)
    {
        internal bool Converted => Action == 3;
        internal ulong Selected => Action == 1 ? 0ul : 42ul;
        internal string Label => Group + "/" + File;
        internal uint[] Modes => !Stress ? new uint[] { 0 } : Converted
            ? new uint[] { 0, 1, 2, 3, 4, 6, 7, 8 } : new uint[] { 0, 1, 3, 4, 6, 7 };
    }
    private static async Task<List<AutomaticCase>> LoadCases()
    {
        var catalog = await Package.Current.InstalledLocation.GetFileAsync("Fixtures\\automatic-cases.tsv");
        var result = new List<AutomaticCase>();
        foreach (var line in await FileIO.ReadLinesAsync(catalog))
        {
            if (string.IsNullOrWhiteSpace(line) || line.StartsWith("#", StringComparison.Ordinal)) { continue; }
            var fields = line.Split('\t');
            Require(fields.Length == 7, "automatic catalog fields");
            result.Add(new AutomaticCase(fields[0], fields[1], uint.Parse(fields[2]), uint.Parse(fields[3]),
                uint.Parse(fields[4]), ulong.Parse(fields[5]), fields[6] == "1"));
        }
        Require(result.Count > 0, "automatic catalog empty");
        return result;
    }
    private static Task<StorageFile> Bundled(AutomaticCase item) => Package.Current.InstalledLocation.GetFileAsync(
        "Fixtures\\" + (item.Group == "hls" ? "" : item.Group + "\\") + item.File).AsTask();
    private async Task AutomaticMatrix(List<string> lines, bool pickFolder)
    {
        var cases = await LoadCases();
        StorageFolder? granted = null;
        if (pickFolder)
        {
            var picker = new FolderPicker();
            picker.FileTypeFilter.Add("*");
            granted = await picker.PickSingleFolderAsync();
            Require(granted != null, "expanded fixture folder picker cancelled");
        }
        var sibling = await ApplicationData.Current.TemporaryFolder.CreateFileAsync(
            "sentinel-" + Guid.NewGuid().ToString("N"), CreationCollisionOption.FailIfExists);
        await FileIO.WriteTextAsync(sibling, "automatic cache sibling");
        var count = 0;
        try
        {
            foreach (var item in cases)
            {
                var bundled = await Bundled(item);
                var file = granted == null ? bundled : await granted.GetFileAsync(item.Group + "\\" + item.File);
                if (granted != null)
                {
                    using var source = (await file.OpenReadAsync()).AsStreamForRead(0);
                    using var expected = (await bundled.OpenReadAsync()).AsStreamForRead(0);
                    Require(SHA256.HashData(source).SequenceEqual(SHA256.HashData(expected)), "granted owned fixture hash");
                }
                foreach (var mode in item.Modes)
                {
                    await Record(lines, "begin automatic " + item.Label + " mode=" + mode);
                    await Record(lines, await ValidateAutomatic(file, item, mode, granted != null));
                    Require(await FileIO.ReadTextAsync(sibling) == "automatic cache sibling", "automatic sibling preserved");
                    ++count;
                }
                if (item.Stress)
                {
                    await Record(lines, await PublicAutomaticCheck(file, item));
                    for (var lifecycle = 0; lifecycle < 3; ++lifecycle)
                    { await Record(lines, await PublicAutomaticLifetime(file, item, lifecycle)); }
                }
            }
        }
        finally { await sibling.DeleteAsync(StorageDeleteOption.PermanentDelete); }
        await Record(lines, "automatic_cases=" + count + " fixtures=" + cases.Count + " granted=" + (granted == null ? 0 : 1));
        var outsideProfile = await Package.Current.InstalledLocation.GetFileAsync("Fixtures\\expanded-decoder\\aac-output-rate.mkv");
        var refusedSource = await StorageFileMediaSource.OpenAsync(outsideProfile, 1021);
        var refusedCache = await TemporaryAacFragmentStore.CreateAsync();
        try
        {
            var refused = false;
            try { await Task.Run(() => AudioDelivery.Check(refusedSource, TimeSpan.FromSeconds(120), fragmentStore: refusedCache, cacheQuotaBytes: 512ul * 1024 * 1024)); }
            catch (SendAirPlay2Exception error) when (error.Result == ResultCode.MediaUnsupported) { refused = true; }
            Require(refused && !refusedSource.IsReleased && refusedCache.ClearCount == 0 && refusedCache.ReleaseCount == 1 && refusedCache.DirectoryRemoved,
                "unvalidated AAC configuration refuses without conversion/cache fallback");
            await Record(lines, "profile_refusal aac-output-rate.mkv result=20 clear=0 release=1 removed=1");
        }
        finally { refusedCache.ReleaseAfterNative(); refusedSource.ReleaseUnowned(); }
    }
    private static async Task<string> ValidateAutomatic(StorageFile file, AutomaticCase item, uint mode, bool granted)
    {
        var source = await StorageFileMediaSource.OpenAsync(file, 1021);
        TemporaryAacFragmentStore? cache = null;
        try
        {
            cache = await TemporaryAacFragmentStore.CreateAsync(failReads: mode == 2);
            var report = await Task.Run(() => AutomaticValidationNative.Run(source, cache,
                mode == 4 ? 11ul : item.Selected, mode == 8 ? 1ul : 512ul * 1024 * 1024, mode));
            var expected = mode == 4 ? ResultCode.AudioTrackInvalid
                : mode == 6 && item.Converted ? ResultCode.MediaUnsupported
                : mode == 7 ? ResultCode.Cancelled : mode == 8 ? ResultCode.MediaLimit : ResultCode.Ok;
            var prepared = expected == ResultCode.Ok;
            var clears = item.Converted && mode != 4 && mode != 6 && mode != 7 ? 1 : 0;
            var failure = mode == 2 ? 6u : mode == 3 && item.Converted ? 3u : mode == 8 ? 5u : 0u;
            Require(report.PreparationResult == (int)expected && report.Media.Failure == failure,
                "automatic preparation/failure category");
            Require(source.ReleaseCount == 1 && cache.ReleaseCount == 1 && cache.ClearCount == clears && cache.DirectoryRemoved,
                "automatic context release clear child removal");
            if (prepared)
            {
                Require(report.Decision.Action == item.Action && report.Decision.SourceFamily == item.Family &&
                    report.Decision.OutputChannels == item.Channels && report.Decision.SelectedAudioId == item.Selected &&
                    report.Media.AudioSegments == item.Segments, "automatic literal decision and segment count");
                if (item.Converted && mode <= 1)
                { Require(report.PostStopRefused == 1 && (ulong)cache.SealCount == item.Segments, "converted sealed fragments and stop"); }
            }
            else { Require(report.Decision.StructSize == 0, "automatic refusal has no stale decision"); }
            if (!item.Converted) { Require(cache.SealCount == 0 && cache.ReadCount == 0, "copy never reads or seals cache"); }
            return "automatic " + item.Label + " mode=" + mode + " granted=" + (granted ? 1 : 0)
                + " preparation=" + report.PreparationResult + " failure=" + report.Media.Failure
                + " action=" + report.Decision.Action + " family=" + report.Decision.SourceFamily
                + " channels=" + report.Decision.OutputChannels + " segments=" + report.Media.AudioSegments
                + Metrics(report) + " source_calls=" + source.ReadCalls + " source_bytes=" + source.ReadBytes
                + " seals=" + cache.SealCount + " sealed_bytes=" + cache.SealedBytes
                + " clear=" + cache.ClearCount + " release=1 removed=1";
        }
        finally { cache?.ReleaseAfterNative(); source.ReleaseUnowned(); }
    }
    private static async Task<string> PublicAutomaticCheck(StorageFile file, AutomaticCase item)
    {
        var source = await StorageFileMediaSource.OpenAsync(file, 1021);
        TemporaryAacFragmentStore? cache = null;
        try
        {
            cache = await TemporaryAacFragmentStore.CreateAsync();
            var decision = await Task.Run(() => AudioDelivery.Check(source, TimeSpan.FromSeconds(120),
                item.Selected == 0 ? null : item.Selected, cache, 512ul * 1024 * 1024));
            Require(decision.Action == (AudioDeliveryAction)item.Action && decision.SelectedAudioId == item.Selected &&
                !source.IsReleased && cache.ReleaseCount == 1 && cache.ClearCount == (item.Converted ? 1 : 0) && cache.DirectoryRemoved,
                "public automatic check borrowed source owned cache");
            return "public_check " + item.Label + " action=" + (uint)decision.Action + " borrowed=1 clear=" + cache.ClearCount + " release=1 removed=1";
        }
        finally { cache?.ReleaseAfterNative(); source.ReleaseUnowned(); }
    }
    private static async Task<string> PublicAutomaticLifetime(StorageFile file, AutomaticCase item, int lifecycle)
    {
        var source = await StorageFileMediaSource.OpenAsync(file);
        TemporaryAacFragmentStore? cache = null;
        try
        {
            cache = await TemporaryAacFragmentStore.CreateAsync();
            try
            {
                using var cast = Cast.Create(new CastOptions
                {
                    ReceiverAddress = "192.0.2.1", Profile = "offline-absent", CredentialStore = new AbsentStore(),
                    Delivery = CastDelivery.AutomaticAudioHls, AudioProfile = (AudioDeliveryProfile)(lifecycle == 2 ? 99 : 1),
                    FragmentStore = cache,
                }, source);
                if (lifecycle == 1)
                {
                    var refused = false;
                    try { await Task.Run(() => cast.Start()); }
                    catch (SendAirPlay2Exception error) when (error.Result == ResultCode.ProfileNotFound) { refused = true; }
                    Require(refused, "automatic absent credentials before network");
                }
                Require(lifecycle != 2, "automatic invalid create accepted");
            }
            catch (SendAirPlay2Exception error) when (lifecycle == 2 && error.Result == ResultCode.InvalidArgument) { }
            Require(source.ReleaseCount == 1 && source.ReadCalls == 0 && cache.ClearCount == 0 && cache.ReleaseCount == 1 && cache.DirectoryRemoved,
                "public unused automatic storage lifetime");
            return "public_automatic " + item.Label + " lifecycle=" + lifecycle + " clear=0 release=1 removed=1";
        }
        finally { cache?.ReleaseAfterNative(); source.ReleaseUnowned(); }
    }
    private static string Metrics(AutomaticValidationNative.Report report) =>
        " prepare_ms=" + report.Media.PreparationMs + " request_ms=" + report.Media.RequestMs
        + " prep_calls=" + report.PreparationReadCalls + " prep_requested=" + report.PreparationRequestedBytes
        + " prep_actual=" + report.PreparationActualBytes + " request_calls=" + report.RequestReadCalls
        + " request_requested=" + report.RequestRequestedBytes + " request_actual=" + report.RequestActualBytes;

    private async Task<bool> Benchmark(List<string> lines)
    {
        var picker = new FileOpenPicker();
        foreach (var type in new[] { ".mkv", ".mp4", ".mov" }) { picker.FileTypeFilter.Add(type); }
        var file = await picker.PickSingleFileAsync();
        Require(file != null, "benchmark picker cancelled");
        var passed = true;
        foreach (var mode in new uint[] { 9, 10, 11 })
        {
            await Record(lines, "begin benchmark mode=" + mode);
            var source = await StorageFileMediaSource.OpenAsync(file!, 32749);
            TemporaryAacFragmentStore? cache = null;
            try
            {
                cache = await TemporaryAacFragmentStore.CreateAsync();
                var clock = Stopwatch.StartNew();
                var report = await Task.Run(() => AutomaticValidationNative.Run(source, cache, 0, 512ul * 1024 * 1024, mode, allowRefusal: true));
                clock.Stop();
                Require(source.ReleaseCount == 1 && cache.ReleaseCount == 1 && cache.ClearCount <= 1 && cache.DirectoryRemoved,
                    "benchmark teardown context cleanup");
                passed &= report.PreparationResult == 0 && report.Media.Failure == 0 && report.Decision.StructSize != 0;
                await Record(lines, "benchmark mode=" + mode + " preparation=" + report.PreparationResult
                    + " failure=" + report.Media.Failure + " action=" + report.Decision.Action
                    + " family=" + report.Decision.SourceFamily + " input_bytes=" + source.Size
                    + " segments=" + report.Media.AudioSegments + " cold_ms=" + report.ColdRequestMs + " warm_ms=" + report.WarmRequestMs
                    + " elapsed_ms=" + clock.ElapsedMilliseconds + Metrics(report)
                    + " sealed_bytes=" + cache.SealedBytes + " clear=" + cache.ClearCount + " release=1 removed=1");
                if (!passed) { break; } // A deterministic refusal is not a reason to retry another route.
            }
            finally { cache?.ReleaseAfterNative(); source.ReleaseUnowned(); }
        }
        return passed;
    }
}

internal static unsafe class AutomaticValidationNative
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct Report
    {
        internal ValidationNative.Report Media;
        internal Native.AudioDecision Decision;
        internal int PreparationResult;
        internal uint PostStopRefused;
        internal ulong PreparationReadCalls, PreparationRequestedBytes, PreparationActualBytes;
        internal ulong RequestReadCalls, RequestRequestedBytes, RequestActualBytes;
        internal ulong ColdRequestMs, WarmRequestMs;
    }
    [DllImport("send_airplay2", EntryPoint = "sap2_runtime_validate_automatic", CallingConvention = CallingConvention.Cdecl)]
    private static extern int Validate(Native.MediaSource* source, IntPtr cache, ulong id, ulong quota, uint mode, Report* report);
    internal static Report Run(StorageFileMediaSource source, TemporaryAacFragmentStore cache, ulong id, ulong quota, uint mode,
        bool allowRefusal = false)
    {
        var nativeSource = source.ToNative();
        using var table = new AacFragmentStore.NativeTable(cache);
        var report = new Report { Media = new ValidationNative.Report { StructSize = (uint)Marshal.SizeOf<Report>() } };
        RequireLayout();
        var result = Validate(&nativeSource, table.Pointer, id, quota, mode, &report);
        GC.KeepAlive(source.Callbacks); GC.KeepAlive(cache);
        if (result != 0 && !(allowRefusal && (report.PreparationResult != 0 || report.Media.Failure != 0)))
        { throw new AutomaticValidationFailure(result, report); }
        return report;
    }
    private static void RequireLayout()
    { if (Marshal.SizeOf<Report>() != 160) { throw new InvalidOperationException("private automatic report layout"); } }
}
internal sealed class AutomaticValidationFailure(int result, AutomaticValidationNative.Report report) : Exception
{
    internal int Result { get; } = result;
    internal AutomaticValidationNative.Report Report { get; } = report;
}
