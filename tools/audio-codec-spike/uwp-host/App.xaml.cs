// SPDX-License-Identifier: Apache-2.0
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading.Tasks;
using Windows.ApplicationModel;
using Windows.ApplicationModel.Activation;
using Windows.Storage;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace SendAirPlay2.CodecSpike;

/// <summary>Offline, automatically exiting test package. No network or pairing.</summary>
public sealed partial class App : Application
{
    public App() => InitializeComponent();

    protected override void OnLaunched(LaunchActivatedEventArgs args) => Run();
    protected override void OnActivated(IActivatedEventArgs args) => Run();

    private async void Run()
    {
        Window.Current.Content = new Page();
        Window.Current.Activate();
        var lines = new List<string>();
        try
        {
            lines.Add("build_ok=" + Native.BuildOk());
            var folder = await ApplicationData.Current.LocalFolder.CreateFolderAsync(
                "probe-" + Guid.NewGuid().ToString("N"), CreationCollisionOption.FailIfExists);
            try
            {
                foreach (var channels in new[] { 6, 8 })
                {
                    var file = await folder.CreateFileAsync("owned-" + channels + ".wav");
                    await FileIO.WriteBytesAsync(file, Wave(channels));
                    lines.Add(await Probe(file, folder, "pcm-" + channels, 0, 0));
                }
                foreach (var name in new[] { "aac-51.m4a", "ac3-51.mka", "eac3-51.mka", "dts-core-51.mka" })
                {
                    var file = await Package.Current.InstalledLocation.GetFileAsync("Fixtures\\" + name);
                    lines.Add(await Probe(file, folder, name, 0, 0));
                }
                var lifecycleFile = await folder.GetFileAsync("owned-6.wav");
                lines.Add(await Probe(lifecycleFile, folder, "cancel", 1, 2));
                lines.Add(await Probe(lifecycleFile, folder, "cache-failure", 2, 6));
            }
            finally
            {
                // This is the exact folder this run created; no host/user media is removed.
                await folder.DeleteAsync(StorageDeleteOption.PermanentDelete);
            }
            foreach (var name in new[] { "sap2_codec_probe.dll", "avcodec-63.dll", "avformat-63.dll", "avutil-61.dll", "swresample-7.dll" })
            {
                var file = await Package.Current.InstalledLocation.GetFileAsync(name);
                using var stream = (await file.OpenReadAsync()).AsStreamForRead(0);
                lines.Add(name + " sha256=" + Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant());
            }
            lines.Add("PASS");
        }
        catch (Exception error)
        {
            lines.Add("FAIL type=" + error.GetType().Name);
        }
        var log = await ApplicationData.Current.LocalFolder.CreateFileAsync(
            "codec-probe.txt", CreationCollisionOption.ReplaceExisting);
        await FileIO.WriteLinesAsync(log, lines);
        // Allow external module inspection after writing the completed run log.
        await Task.Delay(10000);
        Exit();
    }

    private static async Task<string> Probe(StorageFile input, StorageFolder folder,
        string label, int mode, int expected)
    {
        var cache = await folder.CreateFileAsync(Guid.NewGuid().ToString("N") + ".aac-packets");
        using var inputRandom = await input.OpenReadAsync();
        using var reader = inputRandom.AsStreamForRead(0);
        using var outputRandom = await cache.OpenAsync(FileAccessMode.ReadWrite);
        using var writer = outputRandom.AsStreamForWrite(0);
        var context = new ProbeContext(reader, writer, mode);
        var report = await Task.Run(() => Native.Run(context, inputRandom.Size, expected));
        await writer.FlushAsync();
        if (mode == 0 && (report.OutputChannels != 2 || report.OutputSampleRate != 48000 || report.VerifiedAacSamples == 0))
        {
            throw new InvalidOperationException("Output format mismatch");
        }
        return label + " result=" + expected + " input_channels=" + report.InputChannels
            + " stereo_samples=" + report.StereoSamples + " packets=" + report.PacketCount
            + " priming=" + report.InitialPadding;
    }

    private static byte[] Wave(int channels)
    {
        const int samples = 4800;
        using var bytes = new MemoryStream();
        using var writer = new BinaryWriter(bytes);
        writer.Write("RIFF"u8);
        writer.Write(60 + samples * channels * 2);
        writer.Write("WAVEfmt "u8);
        writer.Write(40);
        writer.Write((ushort)0xfffe);
        writer.Write((ushort)channels);
        writer.Write(48000);
        writer.Write(48000 * channels * 2);
        writer.Write((ushort)(channels * 2));
        writer.Write((ushort)16);
        writer.Write((ushort)22);
        writer.Write((ushort)16);
        writer.Write(channels == 6 ? 0x60f : 0x63f);
        writer.Write(Convert.FromHexString("0100000000001000800000aa00389b71"));
        writer.Write("data"u8);
        writer.Write(samples * channels * 2);
        for (var sample = 0; sample < samples * channels; sample++)
        {
            writer.Write((short)8192);
        }
        return bytes.ToArray();
    }
}

internal sealed class ProbeContext(Stream reader, Stream writer, int mode)
{
    internal Stream Reader { get; } = reader;
    internal Stream Writer { get; } = writer;
    internal int Mode { get; } = mode;
    internal bool Cancelled { get; set; }
}

internal static unsafe class Native
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Source
    {
        internal uint StructSize;
        internal nint Context;
        internal ulong Size;
        internal delegate* unmanaged[Cdecl]<nint, ulong, byte*, nuint, nuint> Read;
        internal delegate* unmanaged[Cdecl]<nint, int> Stop;
        internal delegate* unmanaged[Cdecl]<nint, byte*, nuint, int> Write;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Report
    {
        internal uint StructSize, InputChannels, InputSampleRate, OutputChannels, OutputSampleRate, InitialPadding;
        internal ulong DecodedSamples, StereoSamples, EncodedSamples, PacketCount, PacketBytes, VerifiedAacSamples;
        internal double LeftPeak, RightPeak, LeftMean, RightMean;
    }
    [DllImport("sap2_codec_probe", EntryPoint = "sap2_codec_probe_build_ok", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int BuildOk();
    [DllImport("sap2_codec_probe", EntryPoint = "sap2_codec_probe", CallingConvention = CallingConvention.Cdecl)]
    private static extern int RunProbe(Source* source, Report* report);

    internal static Report Run(ProbeContext context, ulong size, int expected)
    {
        var handle = GCHandle.Alloc(context);
        try
        {
            var source = new Source { StructSize = (uint)sizeof(Source), Context = GCHandle.ToIntPtr(handle),
                Size = size, Read = &Read, Stop = &Stop, Write = &Write };
            var report = new Report { StructSize = (uint)sizeof(Report) };
            var result = RunProbe(&source, &report);
            if (result != expected)
            {
                throw new InvalidOperationException("Probe result mismatch: " + result);
            }
            return report;
        }
        finally
        {
            handle.Free();
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static nuint Read(nint pointer, ulong offset, byte* data, nuint capacity)
    {
        try
        {
            var context = (ProbeContext)GCHandle.FromIntPtr(pointer).Target!;
            context.Reader.Position = checked((long)offset);
            return (nuint)context.Reader.Read(new Span<byte>(data, (int)Math.Min((ulong)capacity, 97ul)));
        }
        catch { return 0; }
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Stop(nint pointer) => ((ProbeContext)GCHandle.FromIntPtr(pointer).Target!).Cancelled ? 1 : 0;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Write(nint pointer, byte* data, nuint size)
    {
        try
        {
            var context = (ProbeContext)GCHandle.FromIntPtr(pointer).Target!;
            context.Writer.Write(new ReadOnlySpan<byte>(data, checked((int)size)));
            if (context.Mode == 1) { context.Cancelled = true; }
            return context.Mode == 2 ? 0 : 1;
        }
        catch { return 0; }
    }
}
