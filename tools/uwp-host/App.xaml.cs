// SPDX-License-Identifier: Apache-2.0
using System;
using Windows.ApplicationModel.Activation;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace SendAirPlay2.UwpHost
{
    /// <summary>Minimal application shell: one page, no navigation or suspension state.</summary>
    public sealed partial class App : Application
    {
        public App()
        {
            NativeAbortTrace.Install();
            InitializeComponent();
        }

        protected override void OnLaunched(LaunchActivatedEventArgs args)
        {
            ShowMainPage();
        }

        /// <summary>
        /// Command-line activation through the "sap2-uwp-host" alias: the argument
        /// names a script in LocalState\scripts. A running instance receives later
        /// activations here too, so scripts can follow one another.
        /// </summary>
        protected override void OnActivated(IActivatedEventArgs args)
        {
            var page = ShowMainPage();
            HostLog.Write("Activated: " + args.Kind);
            if (args.Kind == ActivationKind.CommandLineLaunch && args is ICommandLineActivatedEventArgs commandLine)
            {
                page.RunScript(commandLine.Operation.Arguments);
            }
        }

        private static MainPage ShowMainPage()
        {
            if (Window.Current.Content is not Frame frame)
            {
                frame = new Frame();
                Window.Current.Content = frame;
            }
            if (frame.Content == null)
            {
                frame.Navigate(typeof(MainPage));
            }
            Window.Current.Activate();
            return frame.Content as MainPage ?? throw new InvalidOperationException("MainPage did not load");
        }
    }
}
