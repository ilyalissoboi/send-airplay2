// SPDX-License-Identifier: Apache-2.0
using System;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Windows.Storage;
using Windows.Storage.AccessCache;
using Windows.UI.Xaml;

namespace SendAirPlay2.UwpHost
{
    /// <summary>
    /// Test automation, with no Screenbox counterpart: "sap2-uwp-host NAME" runs
    /// LocalState\scripts\NAME.txt, one command per line, through the same code
    /// as the buttons. Blank lines and lines starting with '#' are skipped.
    /// <list type="bullet">
    /// <item><c>discover [NAME]</c>: one scan; fills the address (default: the form's name).</item>
    /// <item><c>discover-model MODEL</c>: one scan; requires one receiver of that model.</item>
    /// <item><c>profile NAME</c>: selects a PasswordVault profile (script-token syntax).</item>
    /// <item><c>cast vault|builtin LABEL [remux|progressive] [start=SECONDS]</c>: casts the
    /// file remembered as LABEL (pick it once with that label); remux is the default.</item>
    /// <item><c>wait SECONDS</c>, <c>pause</c>, <c>play</c>, <c>seek SECONDS</c>, <c>status</c>, <c>stop</c>.</item>
    /// <item><c>wait-state STATE SECONDS</c>: until the receiver reports that PlaybackState.</item>
    /// <item><c>wait-end SECONDS</c>: until the cast ends by itself (or is stopped).</item>
    /// <item><c>exit</c>: closes the app.</item>
    /// </list>
    /// The log records the script name, line numbers and verbs, never arguments,
    /// which may hold a receiver name. The first failing command ends the script
    /// and stops a cast it left running. One script runs at a time.
    /// </summary>
    public sealed partial class MainPage
    {
        private const int MaxScriptLines = 200;
        private const double MaxWaitSeconds = 4 * 3600;
        private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(200);
        private bool scriptRunning; // UI thread only.

        /// <summary>Script names and file labels: 1-32 of a-z, 0-9 and '-'.</summary>
        internal static bool IsScriptToken(string text) => Regex.IsMatch(text, "^[a-z0-9-]{1,32}$");

        /// <summary>Runs the script the activation names. Call on the UI thread.</summary>
        /// <param name="commandLine">The activation's arguments: possibly the alias
        /// itself, then the script name as the last word.</param>
        internal async void RunScript(string commandLine)
        {
            var name = ScriptName(commandLine);
            if (name == null)
            {
                Log("Script: no valid script name (1-32 of a-z, 0-9 and -)");
                return;
            }
            if (scriptRunning)
            {
                Log("Script " + name + ": refused, another script is running");
                return;
            }
            scriptRunning = true;
            try
            {
                await RunScriptFile(name);
            }
            catch (Exception error)
            {
                Log("Script " + name + ": failed, " + error.GetType().Name);
                await StopIfCasting();
            }
            finally
            {
                scriptRunning = false;
            }
        }

        private static string? ScriptName(string commandLine)
        {
            var words = Regex.Matches(commandLine ?? string.Empty, "\"[^\"]*\"|\\S+");
            if (words.Count == 0)
            {
                return null;
            }
            var last = words[words.Count - 1].Value;
            return IsScriptToken(last) ? last : null; // The alias alone ends in ".exe".
        }

        private async Task RunScriptFile(string name)
        {
            var path = Path.Combine(ApplicationData.Current.LocalFolder.Path, "scripts", name + ".txt");
            if (!File.Exists(path))
            {
                Log("Script " + name + ": not found in LocalState\\scripts");
                return;
            }
            var lines = File.ReadAllLines(path);
            if (lines.Length > MaxScriptLines)
            {
                Log("Script " + name + ": refused, more than " + MaxScriptLines + " lines");
                return;
            }
            Log("Script " + name + ": begin");
            for (var index = 0; index < lines.Length; ++index)
            {
                var line = lines[index].Trim();
                if (line.Length == 0 || line.StartsWith("#", StringComparison.Ordinal))
                {
                    continue;
                }
                var words = line.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
                var verb = words[0];
                var where = "line " + (index + 1) + " " + (IsScriptToken(verb) ? verb : "(invalid)");
                Log("Script " + name + ": " + where);
                if (!await Execute(verb, words, line.Substring(verb.Length).Trim()))
                {
                    Log("Script " + name + ": failed at " + where);
                    await StopIfCasting();
                    return;
                }
            }
            Log("Script " + name + ": done");
        }

        /// <summary>One command; true when it succeeded. `rest` is the line after the verb.</summary>
        private async Task<bool> Execute(string verb, string[] words, string rest)
        {
            switch (verb)
            {
                case "discover":
                    return await DiscoverAsync(rest.Length > 0 ? rest : ExpectedNameBox.Text);
                case "discover-model":
                    return words.Length == 2 && await DiscoverAsync(words[1], matchModel: true);
                case "profile":
                    if (words.Length != 2 || !IsScriptToken(words[1]))
                    {
                        return false;
                    }
                    ProfileBox.Text = words[1];
                    return true;
                case "cast":
                    return await ScriptCast(words);
                case "wait":
                    if (!TrySeconds(words, 1, out var wait))
                    {
                        return false;
                    }
                    await Task.Delay(TimeSpan.FromSeconds(wait));
                    return true;
                case "pause":
                    return await Control("pause", c => c.Pause());
                case "play":
                    return await Control("play", c => c.Play());
                case "seek":
                    if (!TrySeconds(words, 1, out var position))
                    {
                        return false;
                    }
                    return await Control("seek " + position.ToString("F1", CultureInfo.InvariantCulture),
                                         c => c.Seek(position));
                case "status":
                    return await LogStatus();
                case "stop":
                    return await StopCast();
                case "wait-state":
                    return await WaitForState(words);
                case "wait-end":
                    return await WaitForEnd(words);
                case "exit":
                    await StopIfCasting();
                    Application.Current.Exit();
                    return true;
                default:
                    Log("Script: unknown command");
                    return false;
            }
        }

        private async Task<bool> ScriptCast(string[] words)
        {
            if (words.Length < 3 || (words[1] != "vault" && words[1] != "builtin") || !IsScriptToken(words[2]))
            {
                Log("Script: cast needs vault|builtin and a file label");
                return false;
            }
            var delivery = CastDelivery.HlsRemux;
            var startSeconds = 0.0;
            for (var index = 3; index < words.Length; ++index)
            {
                var option = words[index];
                if (option == "remux")
                {
                    delivery = CastDelivery.HlsRemux;
                }
                else if (option == "progressive")
                {
                    delivery = CastDelivery.Progressive;
                }
                else if (!option.StartsWith("start=", StringComparison.Ordinal) ||
                         !TryParseSeconds(option.Substring("start=".Length), out startSeconds))
                {
                    Log("Script: unknown cast option");
                    return false;
                }
            }
            var label = words[2];
            if (!StorageApplicationPermissions.FutureAccessList.ContainsItem(label))
            {
                Log("Script: no file remembered as " + label + "; pick it with that label first");
                return false;
            }
            var file = await StorageApplicationPermissions.FutureAccessList.GetFileAsync(label);
            var size = (await file.GetBasicPropertiesAsync()).Size;
            Log("Script: file " + label + " is " + size + " bytes, type " + file.FileType.ToLowerInvariant());
            return words[1] == "vault"
                       ? await StartCast(ProfileBox.Text.Trim(), new PasswordVaultStore(), "PasswordVault", file,
                                         delivery, startSeconds)
                       : await StartCast(BuiltInProfileBox.Text.Trim(), null, "built-in", file, delivery,
                                         startSeconds);
        }

        private async Task<bool> WaitForState(string[] words)
        {
            if (words.Length < 3 || !Enum.TryParse<PlaybackState>(words[1], true, out var wanted) ||
                !TrySeconds(words, 2, out var limit))
            {
                Log("Script: wait-state needs a playback state and seconds");
                return false;
            }
            var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(limit);
            while (DateTimeOffset.UtcNow < deadline)
            {
                var active = CurrentCast();
                if (active == null)
                {
                    Log("Script: no cast while waiting for " + wanted);
                    return false;
                }
                if (active.GetStatus().PlaybackState == wanted)
                {
                    Log("Script: state " + wanted + " reached");
                    return true;
                }
                await Task.Delay(PollInterval);
            }
            Log("Script: state " + wanted + " not reached in time");
            return false;
        }

        private async Task<bool> WaitForEnd(string[] words)
        {
            if (!TrySeconds(words, 1, out var limit))
            {
                return false;
            }
            var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(limit);
            while (DateTimeOffset.UtcNow < deadline)
            {
                lock (castGate)
                {
                    if (cast == null && !castStarting)
                    {
                        Log("Script: the cast has ended");
                        return true;
                    }
                }
                await Task.Delay(PollInterval);
            }
            Log("Script: the cast did not end in time");
            return false;
        }

        private Cast? CurrentCast()
        {
            lock (castGate)
            {
                return cast;
            }
        }

        private async Task StopIfCasting()
        {
            if (CurrentCast() != null)
            {
                await StopCast();
            }
        }

        private bool TrySeconds(string[] words, int index, out double seconds)
        {
            seconds = 0;
            if (index < words.Length && TryParseSeconds(words[index], out seconds))
            {
                return true;
            }
            Log("Script: expected seconds, 0 to " + MaxWaitSeconds);
            return false;
        }

        private static bool TryParseSeconds(string text, out double seconds)
        {
            return double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out seconds) &&
                   !double.IsNaN(seconds) && seconds >= 0 && seconds <= MaxWaitSeconds;
        }
    }
}
