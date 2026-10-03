using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Update;

namespace EternalVR.Launcher.UiShots
{
    /// <summary>
    /// <c>--live-update &lt;folder&gt;</c>: the update dialog's own install, for real, over a release-shaped folder: asks GitHub for
    /// the newest release after the folder's launcher version, opens the dialog over the main window, presses Download and
    /// install and shoots it while it downloads, while it installs and when it has finished or failed. The game check is
    /// left out (the folder is not a running launcher's); what the dialog logged goes to <c>live-update.txt</c>.
    /// </summary>
    internal static class LiveUpdate
    {
        public static int Run(Form main, string programDir, string updatesDir, string output, Action<Form, string> save)
        {
            var current = ReleaseFeed.Plain(AssemblyName.GetAssemblyName(Path.Combine(programDir, "EternalVR.Launcher.exe")).Version);
            var lines = new List<string> { $"folder {programDir}, launcher {current}" };
            AvailableRelease release;
            using (var http = UpdateClient.NewHttp(current))
                release = UpdateClient.NewestAsync(http, current, null, CancellationToken.None).GetAwaiter().GetResult();
            if (release == null)
            {
                lines.Add("no newer release");
                File.WriteAllLines(Path.Combine(output, "live-update.txt"), lines);
                return 2;
            }
            lines.Add($"offered {release.Version}: {release.ZipName}, {release.ZipSize} bytes, SHA-256 {release.ZipSha256}");
            var shot = new HashSet<string>();
            UpdateChoice choice;
            using (var dialog = new UpdateDialog(release, current.ToString(), programDir, updatesDir, () => null, l => lines.Add(DateTime.Now.ToString("HH:mm:ss ") + l)))
            using (var timer = new System.Windows.Forms.Timer { Interval = 100 })
            {
                bool pressed = false;
                timer.Tick += (s, e) =>
                {
                    if (!dialog.Visible) return;
                    if (!pressed)
                    {
                        pressed = true;
                        Reflect.Call(dialog, "Install");
                        return;
                    }
                    var status = Reflect.Get<Label>(dialog, "status").Text;
                    var bar = Reflect.Get<ProgressBar>(dialog, "bar");
                    string name = status.StartsWith("Downloading", StringComparison.Ordinal) && bar.Value > 0 ? "live-downloading"
                        : status.StartsWith("Checking", StringComparison.Ordinal) ? "live-installing"
                        : status.StartsWith("The update failed", StringComparison.Ordinal) ? "live-failed" : null;
                    if (name != null && shot.Add(name)) save(dialog, name);
                    if (name == "live-failed")
                    {
                        timer.Stop();
                        dialog.Close();
                    }
                };
                timer.Start();
                dialog.ShowDialog(main);
                choice = dialog.Choice;
            }
            lines.Add("choice: " + choice + "; shots: " + string.Join(", ", shot));
            File.WriteAllLines(Path.Combine(output, "live-update.txt"), lines);
            return choice == UpdateChoice.Installed ? 0 : 1;
        }
    }
}
