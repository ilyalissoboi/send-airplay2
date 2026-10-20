// SPDX-License-Identifier: Apache-2.0
using System;
using System.IO;
using System.Security.Cryptography;
using System.Threading.Tasks;
using Windows.Storage;
using Windows.Storage.AccessCache;
using Windows.Storage.Pickers;

namespace SendAirPlay2.UwpHost;

public sealed partial class MainPage
{
    // Literal expectations for D107's owned fixtures, independent of the native
    // decision being checked. These commands are development-test automation.
    private sealed record AudioOnlyCase(string Label, string File, uint Action, uint Family);
    private static readonly AudioOnlyCase[] AudioOnlyCases =
    [
        new("aac", "aac.m4a", 4, 1),
        new("cbr", "cbr48.mp3", 4, 10),
        new("vbr", "vbr441.mp3", 4, 10),
        new("flac", "native.flac", 3, 5),
        new("wave", "pcm.wav", 3, 8),
        new("opus", "opus.ogg", 3, 6),
        new("vorbis", "vorbis.ogg", 3, 7),
        new("mp4-flac", "flac.mp4", 3, 5),
        new("mov-pcm", "pcm.mov", 3, 8),
        new("mkv-flac", "flac.mka", 3, 5),
        new("mkv-pcm", "pcm.mka", 3, 8),
        new("edited-pcm", "edited.mov", 3, 8),
    ];

    /// <summary>Grants only the known owned filenames from a manually chosen
    /// folder. Hashes logged during validation must match the external provenance
    /// ledger before any result is accepted; no path or user filename is logged.</summary>
    private async Task<bool> PickAudioOnlyFixtures()
    {
        var picker = new FolderPicker();
        picker.FileTypeFilter.Add("*");
        var folder = await picker.PickSingleFolderAsync();
        if (folder == null) { Log("Audio fixtures: picker cancelled"); return false; }
        foreach (var item in AudioOnlyCases)
        {
            var file = await folder.GetFileAsync(item.File);
            StorageApplicationPermissions.FutureAccessList.AddOrReplace("audioonly-" + item.Label, file);
        }
        Log("Audio fixtures: twelve owned controls remembered");
        return true;
    }

    /// <summary>Runs offline public preparation over actual brokered streams and
    /// the same cache adapter as casting. No credentials or receiver are accessed.</summary>
    private async Task<bool> CheckAudioOnlyFixtures()
    {
        lock (castGate)
        {
            if (cast != null || castStarting)
            {
                Log("Audio checks: stop the current cast first");
                return false;
            }
        }
        var sibling = await ApplicationData.Current.TemporaryFolder.CreateFileAsync(
            "audio-check-sentinel-" + Guid.NewGuid().ToString("N"), CreationCollisionOption.FailIfExists);
        await FileIO.WriteTextAsync(sibling, "sibling survives cache clear");
        try
        {
            foreach (var item in AudioOnlyCases)
            {
                await CheckAudioOnlyFixture(item, preserve: false);
                RequireAudio(await FileIO.ReadTextAsync(sibling) == "sibling survives cache clear",
                    "sibling preservation");
            }
            await CheckAudioOnlyFixture(AudioOnlyCases[3], preserve: true);
            RequireAudio(await FileIO.ReadTextAsync(sibling) == "sibling survives cache clear",
                "refusal sibling preservation");
            Log("Audio checks: PASS cases=13 sibling_preserved=yes");
            return true;
        }
        catch (AudioCheckFailure error)
        {
            Log("Audio checks: FAIL invariant=" + error.Message);
            return false;
        }
        finally
        {
            await sibling.DeleteAsync(StorageDeleteOption.PermanentDelete);
        }
    }

    private async Task CheckAudioOnlyFixture(AudioOnlyCase item, bool preserve)
    {
        Log("Audio check: begin case=" + item.Label + " preserve=" + (preserve ? "yes" : "no"));
        var file = await StorageApplicationPermissions.FutureAccessList.GetFileAsync("audioonly-" + item.Label);
        // These are explicitly granted owned controls, never arbitrary/private media.
        using (var stream = (await file.OpenReadAsync()).AsStreamForRead(0))
        {
            var hash = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
            Log("Audio fixture: case=" + item.Label + " sha256=" + hash);
        }
        var source = await StorageFileMediaSource.OpenAsync(file, readLimit: 1021);
        TemporaryAacFragmentStore? cache = null;
        try
        {
            cache = await TemporaryAacFragmentStore.CreateAsync();
            AudioDecision? decision = null;
            try
            {
                decision = await Task.Run(() => AudioDelivery.Check(source, TimeSpan.FromSeconds(120),
                    fragmentStore: cache, cacheQuotaBytes: 16ul * 1024 * 1024,
                    permission: preserve ? AudioConversionPermission.PreserveOriginal : AudioConversionPermission.AllowStereo,
                    profile: AudioDeliveryProfile.AppleAudioV1));
            }
            catch (SendAirPlay2Exception error) when (preserve && error.Result == ResultCode.MediaUnsupported)
            {
                // Expected permission refusal: storage was offered but never accepted.
            }
            if (preserve)
            {
                RequireAudio(decision == null, "preserve refusal");
            }
            else
            {
                RequireAudio(decision != null && (uint)decision.Profile == 2 &&
                    (uint)decision.Action == item.Action && (uint)decision.Reason == item.Action &&
                    (uint)decision.SourceFamily == item.Family && (uint)decision.OutputFamily == 1 &&
                    decision.OutputChannels == 2 && decision.SelectedAudioId == 1, "literal decision");
            }
            RequireAudio(!source.IsReleased && source.ReadCalls > 0, "borrowed source");
            RequireAudio(cache.ReleaseCount == 1 && cache.ClearCount == (!preserve && item.Action == 3 ? 1 : 0)
                && cache.DirectoryRemoved, "store release clear removal");
            Log("Audio check: prepared case=" + item.Label + " profile=2 action=" +
                (decision == null ? "refused" : ((uint)decision.Action).ToString()) +
                " borrowed=yes clear=" + cache.ClearCount + " store_release=1 removed=yes");
        }
        finally
        {
            cache?.ReleaseAfterNative();
            source.ReleaseUnowned();
        }
        RequireAudio(source.IsReleased && source.ReleaseCount == 1, "source released once");
        Log("Audio check: PASS case=" + item.Label + " preserve=" + (preserve ? "yes" : "no") +
            " source_release=1 reads=" + source.ReadCalls + " bytes=" + source.ReadBytes);
    }

    private sealed class AudioCheckFailure(string invariant) : Exception(invariant);
    private static void RequireAudio(bool passed, string invariant)
    {
        if (!passed) { throw new AudioCheckFailure(invariant); }
    }
}
