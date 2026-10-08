// SPDX-License-Identifier: Apache-2.0
using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace SendAirPlay2.UwpHost
{
    /// <summary>
    /// Logs the native call stack when anything in the process calls abort()
    /// (std::terminate ends there), as module+offset frames. UCRT's abort()
    /// raises SIGABRT before it fails fast, and a packaged app gets no crash dump
    /// without machine-wide settings. Offsets resolve against the linker map of
    /// the same build. Frames carry no addresses, paths or receiver data.
    /// </summary>
    internal static unsafe class NativeAbortTrace
    {
        private const int SigAbrt = 22;
        private const int MaxFrames = 62;
        private const uint FromAddress = 0x4;        // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        private const uint UnchangedRefcount = 0x2;  // GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT

        [DllImport("ucrtbase.dll", EntryPoint = "signal")]
        private static extern IntPtr Signal(int signal, delegate* unmanaged[Cdecl]<int, void> handler);

        [DllImport("kernel32.dll", EntryPoint = "RtlCaptureStackBackTrace")]
        private static extern ushort CaptureStackBackTrace(uint skip, uint count, IntPtr* frames, uint* hash);

        [DllImport("kernel32.dll", EntryPoint = "GetModuleHandleExW")]
        private static extern int GetModuleHandleEx(uint flags, IntPtr address, IntPtr* module);

        [DllImport("kernel32.dll", EntryPoint = "GetModuleFileNameW")]
        private static extern uint GetModuleFileName(IntPtr module, char* name, uint size);

        /// <summary>Installs the handler once; failures leave the default behavior.</summary>
        internal static void Install()
        {
            try
            {
                Signal(SigAbrt, &OnAbort);
            }
            catch (Exception error)
            {
                HostLog.Write("Abort trace: not installed (" + error.GetType().Name + ")");
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static void OnAbort(int signal)
        {
            try
            {
                var frames = stackalloc IntPtr[MaxFrames];
                var count = CaptureStackBackTrace(0, MaxFrames, frames, null);
                HostLog.Write("Abort trace: SIGABRT, " + count + " frames");
                var name = stackalloc char[260];
                for (var index = 0; index < count; ++index)
                {
                    IntPtr module;
                    var frame = frames[index];
                    if (GetModuleHandleEx(FromAddress | UnchangedRefcount, frame, &module) == 0)
                    {
                        HostLog.Write("  #" + index + " <unknown module>");
                        continue;
                    }
                    var length = (int)GetModuleFileName(module, name, 260);
                    var file = Path.GetFileName(new string(name, 0, length));
                    var offset = frame.ToInt64() - module.ToInt64();
                    HostLog.Write("  #" + index + " " + file + "+0x" + offset.ToString("x"));
                }
            }
            catch
            {
                // Nothing can be reported; abort() continues to fail fast.
            }
        }
    }
}
