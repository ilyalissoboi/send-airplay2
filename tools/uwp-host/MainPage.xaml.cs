// SPDX-License-Identifier: Apache-2.0
using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Windows.ApplicationModel;
using Windows.Storage;
using Windows.Storage.AccessCache;
using Windows.Storage.Pickers;
using Windows.UI.Core;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace SendAirPlay2.UwpHost
{
    /// <summary>
    /// D49 step 2b measurements: native loading, the built-in store inside an
    /// AppContainer, pairing into PasswordVault, and casting a brokered StorageFile
    /// whose media server the receiver must reach; D54 adds multicast discovery
    /// inside the AppContainer. Casts can use the library's HLS remux (D60) and
    /// a start position, as Screenbox will. Library calls run off the UI thread;
    /// the log holds fixed fields only (no address, PIN, path, URL or receiver
    /// name).
    /// </summary>
    public sealed partial class MainPage : Page
    {
        private const string AbsentProbeProfile = "uwp-probe-absent-profile";

        private readonly object castGate = new object();
        private StorageFile? mediaFile;
        private Cast? cast;
        private bool castStarting; // Guarded by castGate: a start is in progress.
        private CancellationTokenSource? watcher;

        public MainPage()
        {
            InitializeComponent();
            string status;
            try
            {
                status = "Native library loaded: API version " + SendAirPlay2Library.ApiVersion;
            }
            catch (Exception error)
            {
                // Type name only: messages could contain local paths.
                status = "Native library failed to load: " + error.GetType().Name;
            }
            LibraryStatus.Text = status;
            Log(status);
            Log("Package capabilities: internetClientServer=" +
                (ManifestDeclares("internetClientServer") ? "yes" : "no") +
                " privateNetworkClientServer=" +
                (ManifestDeclares("privateNetworkClientServer") ? "yes" : "no"));
        }

        private static bool ManifestDeclares(string capability)
        {
            var manifest = Path.Combine(Package.Current.InstalledLocation.Path, "AppxManifest.xml");
            return File.ReadAllText(manifest).Contains("Name=\"" + capability + "\"");
        }

        private void Log(string line)
        {
            HostLog.Write(line);
            _ = Dispatcher.RunAsync(CoreDispatcherPriority.Normal, () =>
            {
                LogText.Text += line + Environment.NewLine;
                LogScroller.ChangeView(null, LogScroller.ScrollableHeight, null);
            });
        }

        private static string Describe(CastStatus status)
        {
            return "phase=" + status.Phase + " state=" + status.PlaybackState + " end=" +
                   status.EndReason + " owned=" + (status.Owned ? "yes" : "no") + " position=" +
                   (status.PositionSeconds.HasValue ? status.PositionSeconds.Value.ToString("F1") : "unknown") +
                   " failure=" + status.FailureChannel + "/" + status.FailureReason +
                   " cleaned=" + (status.CleanedUp ? "yes" : "no");
        }

        private static string Failure(Exception error)
        {
            return error is SendAirPlay2Exception library
                       ? SendAirPlay2Library.ResultName(library.Result)
                       : error.GetType().Name;
        }

        private static string AddressFamily(string address) =>
            address.Length == 0 ? "none" : address.Contains(":") ? "ipv6" : "ipv4";

        /// <summary>
        /// One default-length scan. The receiver whose advertised name equals the
        /// expected name fills the address box; names and addresses are compared
        /// and shown in the box, never logged.
        /// </summary>
        private async void OnDiscover(object sender, RoutedEventArgs e)
        {
            await DiscoverAsync(ExpectedNameBox.Text);
        }

        /// <summary>Scans once; back on the UI thread, fills the address box and
        /// returns true when the expected receiver has a castable address.
        /// Model selection requires exactly one match so a script cannot choose
        /// arbitrarily between receivers of the same model.</summary>
        private async Task<bool> DiscoverAsync(string expected, bool matchModel = false)
        {
            Log("Discover: starting (" + Receivers.DefaultDuration.TotalSeconds + " s)");
            var address = await Task.Run(() =>
            {
                try
                {
                    var receivers = Receivers.Discover(Receivers.DefaultDuration);
                    Log("Discover: ok receivers=" + receivers.Count);
                    Receiver? match = null;
                    var matchingCount = 0;
                    for (var index = 0; index < receivers.Count; ++index)
                    {
                        var receiver = receivers[index];
                        var matches = (matchModel ? receiver.Model : receiver.Name) == expected;
                        if (matches)
                        {
                            ++matchingCount;
                        }
                        if (matches && match == null)
                        {
                            match = receiver;
                        }
                        Log("Receiver: index=" + index + " address=" + AddressFamily(receiver.Address) +
                            " port_set=" + (receiver.Port != 0 ? "yes" : "no") +
                            " features=" + (receiver.Features.HasValue ? "yes" : "no") +
                            " expected=" + (matches ? "yes" : "no"));
                    }
                    if (matchModel && matchingCount != 1)
                    {
                        Log("Discover: model selection needs one match; count=" + matchingCount);
                        return null;
                    }
                    if (match == null || match.Address.Length == 0)
                    {
                        Log("Discover: expected receiver " + (match == null ? "not found" : "has no castable address"));
                        return null;
                    }
                    return match.Address;
                }
                catch (Exception error)
                {
                    Log("Discover: " + Failure(error));
                    return null;
                }
            });
            if (address == null)
            {
                return false;
            }
            AddressBox.Text = address;
            Log("Discover: expected receiver found; address box filled");
            return true;
        }

        private void OnProbeBuiltIn(object sender, RoutedEventArgs e)
        {
            Task.Run(() =>
            {
                // Removing a profile that does not exist touches the built-in store
                // (Credential Manager) without changing anything.
                try
                {
                    var deleted = Pairing.ForgetProfile(AbsentProbeProfile);
                    Log("Built-in store probe (remove absent profile): " +
                        (deleted ? "deleted (unexpected)" : "profile_not_found"));
                }
                catch (Exception error)
                {
                    Log("Built-in store probe (remove absent profile): " + Failure(error));
                }
                Log("PasswordVault credentials under '" + PasswordVaultStore.Resource + "': " +
                    new PasswordVaultStore().Count());
            });
        }

        private void OnPair(object sender, RoutedEventArgs e)
        {
            var options = new PairOptions
            {
                ReceiverAddress = AddressBox.Text.Trim(),
                Profile = ProfileBox.Text.Trim(),
                CredentialStore = new PasswordVaultStore(),
            };
            Log("Pair: starting (PasswordVault store)");
            Task.Run(() =>
            {
                try
                {
                    Pairing.Pair(options, ReadPin);
                    Log("Pair: ok");
                }
                catch (Exception error)
                {
                    Log("Pair: " + Failure(error));
                }
                Log("PasswordVault credentials under '" + PasswordVaultStore.Resource + "': " +
                    new PasswordVaultStore().Count());
            });
        }

        /// <summary>Runs on the pairing thread: shows the PIN dialog on the UI thread
        /// and waits for it.</summary>
        private bool ReadPin(char[] digits, out int length)
        {
            var answer = new TaskCompletionSource<string?>();
            _ = Dispatcher.RunAsync(CoreDispatcherPriority.Normal, async () =>
            {
                var box = new PasswordBox { MaxLength = 8 };
                var dialog = new ContentDialog
                {
                    Title = "Enter the PIN shown on the TV",
                    Content = box,
                    PrimaryButtonText = "Pair",
                    CloseButtonText = "Cancel",
                };
                var result = await dialog.ShowAsync();
                answer.TrySetResult(result == ContentDialogResult.Primary ? box.Password : null);
                box.Password = string.Empty;
            });
            var pin = answer.Task.Result;
            if (pin == null)
            {
                length = 0;
                return false;
            }
            var count = Math.Min(pin.Length, digits.Length);
            pin.CopyTo(0, digits, 0, count);
            length = pin.Length; // Above 8 is passed through so the library rejects it.
            return true;
        }

        private async void OnPickFile(object sender, RoutedEventArgs e)
        {
            var picker = new FileOpenPicker { SuggestedStartLocation = PickerLocationId.VideosLibrary };
            picker.FileTypeFilter.Add(".mp4");
            picker.FileTypeFilter.Add(".m4v");
            picker.FileTypeFilter.Add(".mov");
            picker.FileTypeFilter.Add(".mkv");
            var file = await picker.PickSingleFileAsync();
            if (file != null)
            {
                mediaFile = file;
                var size = (await file.GetBasicPropertiesAsync()).Size;
                // The extension only, as a container hint; never the name or path.
                Log("Media file picked: " + size + " bytes, type " + file.FileType.ToLowerInvariant());
                var label = FileLabelBox.Text.Trim();
                if (IsScriptToken(label))
                {
                    // A lasting grant, so scripts can name the file later.
                    StorageApplicationPermissions.FutureAccessList.AddOrReplace(label, file);
                    Log("Media file remembered as " + label);
                }
                else
                {
                    Log("Media file not remembered: a label is 1-32 of a-z, 0-9 and -");
                }
            }
        }

        private void OnCastVault(object sender, RoutedEventArgs e) =>
            _ = StartCastFromForm(ProfileBox.Text.Trim(), new PasswordVaultStore(), "PasswordVault");

        private void OnCastBuiltIn(object sender, RoutedEventArgs e) =>
            _ = StartCastFromForm(BuiltInProfileBox.Text.Trim(), null, "built-in");

        /// <summary>Casts the picked file with the form's delivery and start
        /// position. Call on the UI thread.</summary>
        private Task<bool> StartCastFromForm(string profile, ICredentialStore? store, string storeName)
        {
            var file = mediaFile;
            if (file == null)
            {
                Log("Cast: pick a media file first");
                return Task.FromResult(false);
            }
            var delivery = RemuxBox.IsChecked == true ? CastDelivery.HlsRemux : CastDelivery.Progressive;
            if (!double.TryParse(StartPositionBox.Text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture,
                                 out var startSeconds))
            {
                startSeconds = -1; // Rejected by Cast.Create as invalid options.
            }
            return StartCast(profile, store, storeName, file, delivery, startSeconds);
        }

        /// <summary>
        /// Starts one cast off the UI thread. The task completes with true once
        /// <see cref="Cast.Start"/> succeeds (the cast then holds the slot until it
        /// ends or is stopped) and false when the slot is busy or the start failed.
        /// Call on the UI thread: the address comes from the form.
        /// </summary>
        private Task<bool> StartCast(string profile, ICredentialStore? store, string storeName, StorageFile file,
                                     CastDelivery delivery, double startSeconds)
        {
            // Reserve the single cast slot before any asynchronous work, so two quick
            // clicks cannot both start a cast (one of which Stop could never reach).
            lock (castGate)
            {
                if (cast != null || castStarting)
                {
                    Log("Cast: stop the current cast first");
                    return Task.FromResult(false);
                }
                castStarting = true;
            }
            var address = AddressBox.Text.Trim();
            Log("Cast: starting (" + storeName + " store, delivery=" + delivery + ", start=" +
                startSeconds.ToString("F1", CultureInfo.InvariantCulture) + " s)");
            return Task.Run(async () =>
            {
                Cast? created = null;
                var started = false;
                var clock = Stopwatch.StartNew();
                try
                {
                    var source = await StorageFileMediaSource.OpenAsync(file);
                    created = Cast.Create(new CastOptions
                    {
                        ReceiverAddress = address,
                        Profile = profile,
                        CredentialStore = store,
                        Delivery = delivery,
                        StartPositionSeconds = startSeconds,
                    }, source);
                    lock (castGate)
                    {
                        cast = created; // Visible to Stop and the controls while starting.
                    }
                    created.Start();
                    started = true;
                }
                catch (Exception error)
                {
                    // Clean up before logging: a failed start has already torn down, and
                    // logging (file and dispatcher work) must not keep the slot occupied.
                    var status = created?.GetStatus();
                    ReleaseSlot(created);
                    Log((created == null ? "Cast create: " : "Cast start: ") + Failure(error) + " after " +
                        clock.ElapsedMilliseconds + " ms" +
                        (status.HasValue ? " " + Describe(status.Value) : string.Empty));
                    if (created != null)
                    {
                        Log("Cast disposed; media source released");
                    }
                    return false;
                }
                finally
                {
                    lock (castGate)
                    {
                        castStarting = false;
                    }
                }
                if (started && created != null)
                {
                    // Includes opening the file and, for HLS, reading its index.
                    Log("Cast start: ok after " + clock.ElapsedMilliseconds + " ms " +
                        Describe(created.GetStatus()));
                    Watch(created);
                }
                return started;
            });
        }

        /// <summary>Frees the slot and disposes a cast whose start failed (no-op for null).</summary>
        private void ReleaseSlot(Cast? failed)
        {
            lock (castGate)
            {
                if (failed != null && cast == failed)
                {
                    cast = null;
                }
            }
            failed?.Dispose();
        }

        /// <summary>Logs state changes and the end reason until the cast ends or stops.</summary>
        private void Watch(Cast active)
        {
            var token = new CancellationTokenSource();
            watcher = token;
            Task.Run(() =>
            {
                var last = active.GetStatus();
                while (!token.IsCancellationRequested)
                {
                    var current = active.WaitForChange(last.PlaybackState, TimeSpan.FromMilliseconds(500));
                    if (current.PlaybackState != last.PlaybackState)
                    {
                        Log("State: " + current.PlaybackState);
                    }
                    if (current.Phase == CastPhase.Ended && last.Phase != CastPhase.Ended)
                    {
                        Log("Ended: " + Describe(current));
                    }
                    if (current.Phase == CastPhase.Ended || current.Phase == CastPhase.Stopped)
                    {
                        if (current.Phase == CastPhase.Ended)
                        {
                            ReleaseEnded(active);
                        }
                        return;
                    }
                    last = current;
                }
            });
        }

        /// <summary>
        /// Frees the slot of a cast that ended by itself (end of media, receiver stop,
        /// connection loss), so the next Cast works without pressing Stop first. Stop
        /// takes the slot before stopping, so only one of the two disposes a cast.
        /// </summary>
        private void ReleaseEnded(Cast ended)
        {
            lock (castGate)
            {
                if (cast != ended)
                {
                    return;
                }
                cast = null;
            }
            ended.Stop();
            ended.Dispose();
            Log("Ended cast released; media source released");
        }

        /// <summary>Runs one command on the current cast off the UI thread; true
        /// when the library accepted it.</summary>
        private Task<bool> Control(string name, Action<Cast> action)
        {
            Cast? active;
            lock (castGate)
            {
                active = cast;
            }
            if (active == null)
            {
                Log("Control " + name + ": no cast");
                return Task.FromResult(false);
            }
            return Task.Run(() =>
            {
                try
                {
                    action(active);
                    Log("Control " + name + ": ok");
                    return true;
                }
                catch (Exception error)
                {
                    Log("Control " + name + ": " + Failure(error));
                    return false;
                }
            });
        }

        private void OnPause(object sender, RoutedEventArgs e) => _ = Control("pause", c => c.Pause());

        private void OnPlay(object sender, RoutedEventArgs e) => _ = Control("play", c => c.Play());

        private void OnSeekForward(object sender, RoutedEventArgs e) => _ = Control("seek 60", c => c.Seek(60));

        private void OnSeekBack(object sender, RoutedEventArgs e) => _ = Control("seek 10", c => c.Seek(10));

        private void OnStatus(object sender, RoutedEventArgs e) => _ = LogStatus();

        private Task<bool> LogStatus() => Control("status", c => Log("Status: " + Describe(c.GetStatus())));

        private void OnStop(object sender, RoutedEventArgs e) => _ = StopCast();

        /// <summary>Takes the cast out of the slot, then stops and disposes it off
        /// the UI thread; false when there was no cast.</summary>
        private Task<bool> StopCast()
        {
            Cast? active;
            lock (castGate)
            {
                active = cast;
                cast = null;
            }
            if (active == null)
            {
                Log("Stop: no cast");
                return Task.FromResult(false);
            }
            watcher?.Cancel();
            return Task.Run(() =>
            {
                active.Stop();
                Log("Stopped: " + Describe(active.GetStatus()));
                active.Dispose();
                Log("Cast disposed; media source released");
                return true;
            });
        }
    }
}
