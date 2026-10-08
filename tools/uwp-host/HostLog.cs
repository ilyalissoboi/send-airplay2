// SPDX-License-Identifier: Apache-2.0
using System;
using System.IO;
using Windows.Storage;

namespace SendAirPlay2.UwpHost
{
    /// <summary>
    /// Appends fixed-field status lines to LocalState\host-log.txt, so a test run
    /// can be read back from outside the app. Never log addresses, profiles' record
    /// bytes, PINs, URLs, file paths or receiver text.
    /// </summary>
    internal static class HostLog
    {
        private static readonly object Gate = new object();

        internal static string FilePath =>
            Path.Combine(ApplicationData.Current.LocalFolder.Path, "host-log.txt");

        internal static void Write(string line)
        {
            var stamped = DateTimeOffset.UtcNow.ToString("HH:mm:ss.fff") + " " + line;
            lock (Gate)
            {
                File.AppendAllText(FilePath, stamped + Environment.NewLine);
            }
        }
    }
}
