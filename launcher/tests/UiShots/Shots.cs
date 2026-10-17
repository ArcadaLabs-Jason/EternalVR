using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using EternalVR.Launcher.Core.Update;

namespace EternalVR.Launcher.UiShots
{
    /// <summary>
    /// The shots: the main window's tabs and bar, then each dialog state, opened as the launcher opens it (modal over the
    /// main window, which is on the chosen display). Each is saved as <c>&lt;dpi&gt;-open-&lt;name&gt;.png</c> (opened on a display of
    /// that scale) and, with <c>--drag</c>, <c>&lt;dpi&gt;-dragged-&lt;name&gt;.png</c> (then dragged to the other display).
    /// </summary>
    /// <summary>After a window opens, it is dragged to another display: Windows changes its scale as when a player does it.</summary>
    internal sealed class Move
    {
        public Screen Screen;

        public void Apply(Form f) => f.Location = Screen.WorkingArea.Location + new Size(60, 60);
    }

    internal sealed class Shots
    {
        private readonly string output;
        private readonly string work;
        private readonly Screen screen;
        private readonly IReadOnlyList<Move> moves;
        private readonly string only;
        private readonly List<string> summary = new List<string>();
        private MainForm main;
        private LauncherContext ctx;

        public Shots(string output, string work, Screen screen, IReadOnlyList<Move> moves, string only)
        {
            this.output = output;
            this.work = work;
            this.screen = screen;
            this.moves = moves;
            this.only = only;
        }

        /// <summary>The shots with a layout fault (<see cref="Capture.Faults"/>).</summary>
        public int Faulted { get; private set; }

        /// <summary>With <c>--live-update</c>: the folder to update for real (<see cref="LiveUpdate"/>) instead of the shots.</summary>
        public string LiveFolder { get; set; }

        public int Run()
        {
            ctx = FakeTree.Create(Path.Combine(work, "tree"));
            main = new MainForm(ctx) { StartPosition = FormStartPosition.Manual, Location = screen.WorkingArea.Location + new Size(40, 40) };
            main.Show();
            Pump(2500);
            int dpi = main.DeviceDpi;
            summary.Add($"display {screen.DeviceName} {screen.Bounds}, main window at {dpi} dpi");
            if (LiveFolder != null)
            {
                Save(main, Name(main, "live-main", null));
                int result = LiveUpdate.Run(main, LiveFolder, Path.Combine(work, "updates"), output, (f, name) => Save(f, Name(f, name, null)));
                main.Close();
                return result;
            }

            var tabs = Reflect.Get<TabControl>(main, "tabs");
            for (int i = 0; i < tabs.TabPages.Count; i++)
            {
                int index = i;
                Shoot(main, "main-" + Slug(tabs.TabPages[i].Text), () => { tabs.SelectedIndex = index; Pump(500); });
            }
            // The Advanced tab scrolled to its end: the System group (the game folder, the runtime, the headset's graphics card).
            var advanced = tabs.TabPages.Cast<TabPage>().First(p => p.Text == "Advanced");
            Shoot(main, "main-advanced-end", () =>
            {
                tabs.SelectedTab = advanced;
                Pump(300);
                advanced.AutoScrollPosition = new Point(0, advanced.DisplayRectangle.Height);
                Pump(300);
            });
            advanced.AutoScrollPosition = Point.Empty;
            tabs.SelectedIndex = 0;
            Reflect.Call(main, "ShowUpdateButton", new Version(0, 1, 14));
            Shoot(main, "main-update-button", () => Pump(300));
            // The start watchdog's two longest warnings on the status line, both asking for a report (the button is marked): no
            // picture from the game in 5 minutes, and none in the headset.
            Shoot(main, "main-start-stuck", () =>
            {
                Reflect.Call(main, "ShowStatus", new SessionStatus(StatusKind.Warning, StartupWatchdog.StuckLoadMessage));
                Pump(300);
            });
            Shoot(main, "main-start-stall", () =>
            {
                Reflect.Call(main, "ShowStatus", new SessionStatus(StatusKind.Warning, StartupWatchdog.NoFrameMessage));
                Pump(300);
            });
            // After a crash the status line asks for a report and the Export report button it names is marked (ReportHint).
            Reflect.Call(main, "ShowStatus", new SessionStatus(StatusKind.Warning,
                GameExit.StatusPrefix(unchecked((int)0xC0000005)) + "The game has exited and your settings were restored."));
            Shoot(main, "main-crash-export", () => Pump(300));

            foreach (var s in Scenarios())
            {
                if (only != null && s.Name.IndexOf(only, StringComparison.OrdinalIgnoreCase) < 0) continue;
                ShootModal(s, null);
                foreach (var m in moves) ShootModal(s, m);
            }
            ShootProfileChanges(tabs);
            // Last: its scale changes round the main window's control sizes, which the shots above compare with earlier runs.
            ShootSteps(main, "main-layout");
            File.WriteAllLines(Path.Combine(output, $"{dpi}-summary.txt"), summary);
            main.Close();
            return 0;
        }

        private IEnumerable<Scenario> Scenarios()
        {
            var release = Release();
            var programDir = Path.Combine(work, "program");
            var updates = Path.Combine(work, "updates");
            Directory.CreateDirectory(programDir);
            foreach (var (name, why) in UpdateStates.Reasons())
                yield return Update("update-" + name, release, programDir, updates, why, null);
            yield return Update("update-downloading", release, programDir, updates, null, f => UpdateStates.Downloading(f, 0.42));
            yield return Update("update-installing", release, programDir, updates, null, f => UpdateStates.Installing(f));
            yield return Update("update-failed", release, programDir, updates, null,
                f => UpdateStates.Failed(f, "Response status code does not indicate success: 404 (Not Found).", installed: false));
            yield return Update("update-installed-then-failed", release, programDir, updates, null,
                f => UpdateStates.Failed(f, "Access to the path 'E:\\Games\\EternalVR\\updates\\0.1.14' is denied.", installed: true));

            var dlss = new DlssRelease(new Version(310, 4, 0, 0), new Uri("https://github.com/NVIDIA/DLSS/raw/main/lib/nvngx_dlss.dll"), 30_000_000,
                new string('0', 64), new Uri("https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt"));
            yield return Modal("dlss-download", () => new DlssDownloadDialog(dlss, Path.Combine(work, "dlss"), _ => { }), null);
            yield return Modal("dlss-downloading", () => new DlssDownloadDialog(dlss, Path.Combine(work, "dlss"), _ => { }), f =>
            {
                Reflect.Get<Button>(f, "accept").Enabled = false;
                var bar = Reflect.Get<ProgressBar>(f, "bar");
                bar.Visible = true;
                bar.Value = 420;
                var status = Reflect.Get<Label>(f, "status");
                status.Visible = true;
                status.Text = "Downloading...";
            });
            yield return Modal("dlss-failed", () => new DlssDownloadDialog(dlss, Path.Combine(work, "dlss"), _ => { }), f =>
            {
                var status = Reflect.Get<Label>(f, "status");
                status.ForeColor = Color.Firebrick;
                status.Visible = true;
                status.Text = "The download failed: The remote name could not be resolved: 'github.com' Nothing was kept.";
                Reflect.Get<Button>(f, "accept").Text = "Try again";
            });
            yield return new Scenario { Name = "controls", Open = () => Reflect.Call(main, "EditControls") };
            yield return new Scenario { Name = "controls-layout", Open = () => Reflect.Call(main, "EditControls"), Steps = f => LayoutSteps.For(f, true) };
            yield return new Scenario { Name = "controls-layout-profile", Open = () => EditControlsOf("Layout check"), Steps = f => LayoutSteps.For(f, true) };
            yield return Modal("weapon-directions", () => new WeaponDirectionsDialog("up_right=none,down_left=none"), null);
            yield return Modal("report-saved", () => new ReportSavedDialog(
                @"C:\Users\<user>\Desktop\EternalVR-report-v0.1.50-2026-10-13-A1B2C3.zip", _ => { }), null);
            foreach (var name in new[] { "restore-saves", "restore-saves-layout" })
                yield return new Scenario
                {
                    Name = name,
                    Open = () => Reflect.Call(main, "ChooseBackup", (IReadOnlyList<string>)Directory.GetDirectories(ctx.Paths.SaveBackups).OrderByDescending(d => d).ToList()),
                    Steps = name.EndsWith("-layout") ? f => LayoutSteps.For(f, false) : (Func<Form, IReadOnlyList<Step>>)null,
                };
            yield return new Scenario { Name = "new-profile", Open = () => Reflect.Call(main, "AskProfileName") };
            yield return new Scenario
            {
                Name = "rename-profile",
                Open = () => Reflect.Call(main, "AskProfileName", "Rename VR settings profile", "Rename", "Riley", (Func<string, string>)(n => null)),
            };
            yield return new Scenario
            {
                Name = "duplicate-profile",
                Open = () => Reflect.Call(main, "AskProfileName", "Duplicate VR settings profile", "Duplicate", "Riley copy", (Func<string, string>)(n => null)),
            };
        }

        /// <summary>
        /// A VR settings profile with changes from Recommended picked: the strip's count and the marked rows on both settings
        /// tabs; then <see cref="ProfileFlow"/>, which ends back on (none).
        /// </summary>
        private void ShootProfileChanges(TabControl tabs)
        {
            if (only != null && "main-profile-changes".IndexOf(only, StringComparison.OrdinalIgnoreCase) < 0) return;
            var riley = ctx.Settings.Clone();
            riley.Turn = TurnMode.Snap;
            riley.Vignette = VignetteMode.Light;
            riley.ShowArms = false;
            riley.Hud = HudMode.Wrist;
            riley.WorldScale = 1.1;
            new ProfileStore(ctx.Paths.Profiles).Save("Riley", riley);
            var list = Reflect.Get<ComboBox>(main, "profile");
            Reflect.Call(main, "FillProfiles");
            list.SelectedItem = "Riley";
            Pump(500);
            for (int i = 0; i < 2; i++)
            {
                int index = i;
                Shoot(main, "main-profile-changes-" + Slug(tabs.TabPages[i].Text), () => { tabs.SelectedIndex = index; Pump(500); });
            }
            tabs.SelectedIndex = 0;
            // Then the strip's actions as a player uses them (ProfileFlow), which ends on (none).
            var flow = new ProfileFlow(main, ctx, Pump);
            File.WriteAllLines(Path.Combine(output, $"{main.DeviceDpi}-profile-flow.txt"), flow.Run());
            if (flow.Failed) Faulted++;
            summary.Add("profile flow: " + (flow.Failed ? "FAILED" : "ok"));
        }

        /// <summary>The main window through <see cref="LayoutSteps"/>, shot after each, then back at its own scale and size.</summary>
        private void ShootSteps(Form f, string name)
        {
            if (only != null && name.IndexOf(only, StringComparison.OrdinalIgnoreCase) < 0) return;
            var bounds = f.Bounds;
            foreach (var step in LayoutSteps.For(f, false))
            {
                step.Apply(f);
                Pump(1200);
                Save(f, Name(f, name + "-" + step.Name, null));
            }
            f.WindowState = FormWindowState.Normal;
            f.Bounds = bounds;
            Pump(1200);
        }

        /// <summary>The controls editor with the VR settings profile <paramref name="profile"/> in use (made from the current settings).</summary>
        private void EditControlsOf(string profile)
        {
            new ProfileStore(ctx.Paths.Profiles).Save(profile, ctx.Settings);
            ctx.Settings.Profile = profile;
            try { Reflect.Call(main, "EditControls"); }
            finally { ctx.Settings.Profile = string.Empty; }
        }

        private Scenario Update(string name, AvailableRelease release, string programDir, string updates, InstallBlock why, Action<Form> prepare) =>
            Modal(name, () => new UpdateDialog(release, "0.1.13", programDir, updates, () => why, _ => { }), prepare);

        private Scenario Modal(string name, Func<Form> make, Action<Form> prepare) => new Scenario
        {
            Name = name,
            Prepare = prepare,
            Open = () =>
            {
                using (var f = make()) f.ShowDialog(main);
            },
        };

        private static AvailableRelease Release() => new AvailableRelease(new Version(0, 1, 14), "EternalVR v0.1.14 (alpha)",
            new Uri("https://github.com/ArcadaLabs-Jason/EternalVR/releases/tag/v0.1.14"), "EternalVR-alpha-0.1.14-3ba1728.zip",
            new Uri("https://github.com/ArcadaLabs-Jason/EternalVR/releases/download/v0.1.14/EternalVR-alpha-0.1.14-3ba1728.zip"), 2901264,
            "3fc524dd27cb68eb5c9b99dde4319a1a3efe020efaa6d86119df5af619198f7d", null,
            "# EternalVR v0.1.14 (alpha)\n\nA launcher fix: the launcher now reads your headset from a separate, short-lived helper, so your "
            + "headset's runtime\nno longer stays loaded inside the launcher.\n\n## Download\n");

        /// <summary>
        /// Opens the scenario's window modal over the main window; a timer sets it up, shoots it, takes each of its
        /// <see cref="Scenario.Steps"/> and shoots it again, and closes it.
        /// </summary>
        private void ShootModal(Scenario s, Move move)
        {
            int step = 0;
            Form seen = null;
            IReadOnlyList<Step> steps = null;
            using (var timer = new System.Windows.Forms.Timer { Interval = 250 })
            {
                timer.Tick += (o, e) =>
                {
                    var f = seen ?? Application.OpenForms.Cast<Form>().LastOrDefault(x => x != main && x.Visible);
                    if (f == null) return;
                    seen = f;
                    step++;
                    if (step == 1)
                    {
                        s.Prepare?.Invoke(f);
                        move?.Apply(f);
                    }
                    else if (step == 4)
                    {
                        Save(f, Name(f, s.Name, move));
                        steps = s.Steps?.Invoke(f) ?? new Step[0];
                    }
                    // Each step two ticks after the last shot, shot two ticks later (the layout settles in between).
                    else if (step > 4 && (step - 4) % 4 == 2 && (step - 4) / 4 < steps.Count) steps[(step - 4) / 4].Apply(f);
                    else if (step > 4 && (step - 4) % 4 == 0 && (step - 4) / 4 <= steps.Count) Save(f, Name(f, s.Name + "-" + steps[(step - 4) / 4 - 1].Name, move));
                    if (step >= 4 && (step - 4) / 4 >= steps.Count)
                    {
                        timer.Stop();
                        if (f.WindowState != FormWindowState.Normal) f.WindowState = FormWindowState.Normal;
                        f.DialogResult = DialogResult.Cancel;
                        f.Close();
                    }
                };
                timer.Start();
                s.Open();
            }
        }

        private void Shoot(Form f, string name, Action prepare)
        {
            if (only != null && name.IndexOf(only, StringComparison.OrdinalIgnoreCase) < 0) return;
            prepare();
            Save(f, Name(f, name, null));
            foreach (var m in moves)
            {
                var at = f.Location;
                m.Apply(f);
                Pump(1200);
                Save(f, Name(f, name, m));
                f.Location = at;
                Pump(1200);
            }
        }

        private string Name(Form f, string name, Move move) => $"{f.DeviceDpi}-{(move == null ? "open" : "dragged")}-{name}";

        private void Save(Form f, string name)
        {
            using (var bmp = Capture.Window(f)) ImageSave.Png(bmp, Path.Combine(output, name + ".png"));
            var faults = Capture.Faults(f);
            if (faults.Count > 0) Faulted++;
            File.WriteAllText(Path.Combine(output, name + ".txt"), Capture.Describe(f)
                + (faults.Count == 0 ? "no layout faults found\n" : "FAULTS:\n  " + string.Join("\n  ", faults) + "\n"));
            summary.Add($"{name}: {(faults.Count == 0 ? "ok" : faults.Count + " fault(s)")}");
        }

        private static string Slug(string text) => new string(text.ToLowerInvariant().Select(c => char.IsLetterOrDigit(c) ? c : '-').ToArray()).Trim('-');

        private static void Pump(int ms)
        {
            var until = Stopwatch.StartNew();
            while (until.ElapsedMilliseconds < ms)
            {
                Application.DoEvents();
                Thread.Sleep(15);
            }
        }
    }
}
