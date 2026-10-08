// SPDX-License-Identifier: Apache-2.0
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
            if (Window.Current.Content is not Frame frame)
            {
                frame = new Frame();
                Window.Current.Content = frame;
            }
            if (frame.Content == null)
            {
                frame.Navigate(typeof(MainPage), args.Arguments);
            }
            Window.Current.Activate();
        }
    }
}
