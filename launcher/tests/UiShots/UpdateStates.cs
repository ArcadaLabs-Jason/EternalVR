using System.Collections.Generic;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Update;

namespace EternalVR.Launcher.UiShots
{
    /// <summary>The <see cref="UpdateDialog"/>'s states after Download and install, set up without a download.</summary>
    internal static class UpdateStates
    {
        /// <summary>Each reason the dialog can be given for not installing, and none.</summary>
        public static IEnumerable<(string, InstallBlock)> Reasons()
        {
            yield return ("can-install", null);
            yield return ("game-running", InstallBlock.For(true, null, true));
            yield return ("not-release", InstallBlock.For(false, null, false));
            yield return ("not-writable", InstallBlock.For(true, @"Access to the path 'C:\Program Files\EternalVR\.write-probe-3f2a9c' is denied.", false));
        }

        public static void Downloading(Form f, double progress)
        {
            Reflect.Get<Button>(f, "install").Enabled = false;
            Reflect.Get<Button>(f, "skip").Enabled = false;
            var bar = Reflect.Get<ProgressBar>(f, "bar");
            bar.Visible = true;
            bar.Value = (int)(progress * bar.Maximum);
            var status = Reflect.Get<Label>(f, "status");
            status.Visible = true;
            status.Text = "Downloading EternalVR-alpha-0.1.14-3ba1728.zip...";
        }

        public static void Installing(Form f)
        {
            Downloading(f, 1);
            Reflect.Get<ProgressBar>(f, "bar").Style = ProgressBarStyle.Marquee;
            Reflect.Get<Button>(f, "later").Enabled = false;
            Reflect.Get<Label>(f, "status").Text = "Checking and installing...";
        }

        public static void Failed(Form f, string why, bool installed)
        {
            Downloading(f, 0.42);
            if (installed) Reflect.Set(f, "Choice", UpdateChoice.Installed);
            Reflect.Call(f, "Failed", why);
        }
    }
}
