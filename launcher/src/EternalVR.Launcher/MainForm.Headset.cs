using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The Play tab's Headset box (<see cref="HeadsetView"/>): the headset and its route, its native panel, the size the runtime
    /// asks for and when it was read, the last session's refresh rate, and Detect again, which runs the launch's runtime probe on
    /// its own. The launcher runs that probe by itself when it opens if the headset's runtime is already up
    /// (<see cref="HeadsetAutoRead"/>). Also Resolution's base list and the parts of other rows that name the headset (Each eye,
    /// Frame pacing).
    /// </summary>
    public sealed partial class MainForm
    {
        private const string DetectText = "Detect again";

        private readonly Label headsetLine = Value(460);
        /// <summary>What to do, or that the values are old; hidden when there is nothing to say.</summary>
        private readonly Label headsetState = Value(560);
        private readonly Button detectAgain = new Button { Text = DetectText, Width = 100, Height = ButtonHeight };
        private readonly Label panelLine = Value(560);
        private readonly Label asksLine = Value(560);
        private readonly Label refreshLine = Value(560);
        /// <summary>The row whose label names who asks for the size ("VD asks for").</summary>
        private SettingRow asksRow;
        /// <summary>A running probe (Detect again, or the read at start); Launch VR waits for it (the probe sets the game's OpenXR
        /// variables in this process).</summary>
        private Task<OpenXrProbeResult> detecting;
        /// <summary>Why the launcher did not read the headset when it opened; null when it did, tried, or a probe ran since.</summary>
        private string notReadAtStart;

        /// <summary>Resolution's base, with the bases its entries stand for (the headset's own only when its panel is known).</summary>
        private readonly ComboBox resolutionBase = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 175, AccessibleName = "Resolution base" };
        private readonly List<ResolutionBase> baseValues = new List<ResolutionBase>();
        /// <summary>Resolution's list, "×" and number: the number goes under the list when the column is too narrow.</summary>
        private FlowLayoutPanel resolutionRow;

        /// <summary>The runtime manifest the Headset box last looked at, and its runtime.name (read again only when it changes).</summary>
        private string shownManifest, shownManifestName;

        private static Label Value(int width) =>
            new Label { AutoSize = true, MaximumSize = new Size(width, 0), Margin = new Padding(3, 4, 3, 4) };

        private GroupBox HeadsetGroup()
        {
            detectAgain.Click += (s, e) => DetectHeadset(HeadsetReadBy.Detect);
            ownTips[detectAgain] = Wrap("Reads the headset now, as Launch VR does (up to 30 s). With SteamVR as the runtime, this starts SteamVR.");
            var top = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false };
            top.Controls.AddRange(new Control[] { headsetLine, detectAgain });
            var headset = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false, FlowDirection = FlowDirection.TopDown };
            headset.Controls.AddRange(new Control[] { top, headsetState });
            // Nothing to save in these rows: they show what the launcher found (ShowHeadset).
            asksRow = Row(Setting.HeadsetAsks, asksLine, s => { }, s => { });
            return Group("Headset",
                Row(Setting.Headset, headset, s => { }, s => { }, fit: FitHeadsetLine),
                Row(Setting.NativePanel, panelLine, s => { }, s => { }),
                asksRow,
                Row(Setting.Refresh, refreshLine, s => { }, s => { }));
        }

        /// <summary>The headset's name with Detect again at the right of the box, in the same place whatever the name's length.</summary>
        private void FitHeadsetLine(int width)
        {
            var line = new Size(Math.Max(40, width - detectAgain.Width - detectAgain.Margin.Horizontal - headsetLine.Margin.Horizontal), 0);
            if (headsetLine.MaximumSize != line) headsetLine.MaximumSize = line;
            if (headsetLine.MinimumSize != line) headsetLine.MinimumSize = line;
            // The name's first line level with the button's text.
            int top = detectAgain.Margin.Top + Math.Max(0, (detectAgain.Height - headsetLine.Font.Height) / 2);
            if (headsetLine.Margin.Top != top) headsetLine.Margin = new Padding(headsetLine.Margin.Left, top, headsetLine.Margin.Right, 0);
            WrapIn(headsetState, width);
        }

        /// <summary>Resolution: its base and the number it is multiplied by.</summary>
        private SettingRow ResolutionRow() =>
            Row(Setting.Resolution, resolutionRow = WithUnits(resolutionBase, "×", renderScale),
                load: s =>
                {
                    FillResolutionBases(s.ResolutionBase);
                    renderScale.Value = (decimal)LauncherSettings.ClampRenderScale(s.RenderScale);
                },
                read: s =>
                {
                    int i = resolutionBase.SelectedIndex;
                    if (i >= 0 && i < baseValues.Count) s.ResolutionBase = baseValues[i];
                    s.RenderScale = LauncherSettings.ClampRenderScale((double)renderScale.Value);
                },
                fit: w =>
                {
                    // "× 1.00" beside the list, ending where the other lists end, while the list keeps at least
                    // ResolutionListMin; else under it.
                    int pair = resolutionRow.Controls[1].PreferredSize.Width;
                    int room = Math.Min(w, LogicalToDeviceUnits(ListCap) + resolutionBase.Margin.Horizontal) - pair;
                    FitList(resolutionBase, room >= LogicalToDeviceUnits(ResolutionListMin) ? room : w);
                    if (resolutionRow.MaximumSize.Width != w) resolutionRow.MaximumSize = new Size(Math.Max(40, w), 0);
                });

        /// <summary>The narrowest Resolution's list gets with its number beside it, at 96 DPI.</summary>
        private const int ResolutionListMin = 190;

        /// <summary>Controls in a row with a word between them.</summary>
        private static FlowLayoutPanel WithUnits(Control first, string between, Control second)
        {
            // The word and the second control stay together when the row wraps.
            var pair = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false };
            pair.Controls.AddRange(new Control[] { Caption(between), second });
            var panel = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = true };
            panel.Controls.AddRange(new Control[] { first, pair });
            return panel;
        }

        /// <summary>
        /// Resolution's list for the headset as known now (<see cref="HeadsetView.ResolutionChoices"/>), with
        /// <paramref name="current"/> selected. Filled as loading: the change events save nothing.
        /// </summary>
        private void FillResolutionBases(ResolutionBase current)
        {
            var choices = HeadsetView.ResolutionChoices(Identity(), current, CurrentRuntimeName());
            bool wasLoading = loading;
            loading = true;
            try
            {
                bool same = choices.Count == baseValues.Count;
                for (int i = 0; same && i < choices.Count; i++)
                    same = choices[i].Key == baseValues[i] && Equals(resolutionBase.Items[i], choices[i].Value);
                if (!same)
                {
                    resolutionBase.Items.Clear();
                    baseValues.Clear();
                    foreach (var c in choices)
                    {
                        baseValues.Add(c.Key);
                        resolutionBase.Items.Add(c.Value);
                    }
                }
                resolutionBase.SelectedIndex = Math.Max(0, baseValues.IndexOf(current));
            }
            finally
            {
                loading = wasLoading;
            }
        }

        private HeadsetIdentity Identity() => ctx.Identify(ctx.Headset.RuntimeName, ctx.Headset.SystemName);

        /// <summary>The <c>runtime.name</c> of the runtime the next launch uses (its manifest read again only when it changes).</summary>
        private string CurrentRuntimeName()
        {
            var manifest = ctx.EffectiveRuntime();
            if (!string.Equals(manifest, shownManifest, StringComparison.OrdinalIgnoreCase))
            {
                shownManifest = manifest;
                shownManifestName = HeadsetIdentity.ManifestRuntimeName(manifest);
            }
            return shownManifestName;
        }

        /// <summary>The Headset box, the Each eye line, Resolution's list and Frame pacing's matched choice, from what is known now.</summary>
        private void ShowHeadset()
        {
            var facts = ctx.Headset;
            var identity = Identity();
            var runtimeName = CurrentRuntimeName();
            var view = HeadsetView.For(facts, identity, shownManifest, runtimeName, DateTime.Now, notReadAtStart, detecting != null);
            headsetLine.Text = view.HeadsetLine;
            headsetState.Text = view.State ?? string.Empty;
            headsetState.Visible = view.State != null;
            headsetState.ForeColor = view.Old ? WarningText : SystemColors.GrayText;
            panelLine.Text = view.PanelLine;
            asksRow.Label.Text = view.AsksLabel;
            asksLine.Text = Lines(view.AsksLine);
            refreshLine.Text = view.RefreshLine;
            OwnTip(panelLine, view.PanelTip);
            OwnTip(asksLine, view.AsksTip);
            OwnTip(refreshLine, view.RefreshTip);
            eachEye.Text = Lines(HeadsetView.EachEye(ctx.Settings, facts, identity, ctx.LastRenderCap));
            FillResolutionBases(ctx.Settings.ResolutionBase);
            OwnTip(resolutionBase, HeadsetView.ResolutionTip(identity, runtimeName));
            ShowPacingChoice(HeadsetView.PacingChoice(facts));
        }

        private void OwnTip(Control c, string tip)
        {
            ownTips[c] = Wrap(tip);
            tips.SetToolTip(c, ownTips[c]);
        }

        private static string Lines(string text) => text.Replace("\n", Environment.NewLine);

        /// <summary>Frame pacing's matched choice names the last session's refresh rate.</summary>
        private void ShowPacingChoice(string text)
        {
            int i = (int)FramePacing.Headset;
            if (pacing.Items.Count <= i || Equals(pacing.Items[i], text)) return;
            bool wasLoading = loading;
            loading = true;
            try
            {
                int selected = pacing.SelectedIndex;
                pacing.Items[i] = text;
                pacing.SelectedIndex = selected;
            }
            finally
            {
                loading = wasLoading;
            }
        }

        /// <summary>
        /// When the window opens: reads the headset in the background if its runtime is already up (<see cref="HeadsetAutoRead"/>);
        /// otherwise the box keeps the last values and says it is read at Launch VR. Never starts a runtime.
        /// </summary>
        private void ReadHeadsetAtStart()
        {
            if (ctx.TestMode) return;
            var why = HeadsetAutoRead.WhyNot(CurrentRuntimeName(), Platform.WindowsSystem.IsProcessRunning, File.Exists(ctx.OpenXrLoader));
            if (why == null && ctx.RunningGameProcesses().Count > 0) why = "the game is running";
            if (why != null)
            {
                notReadAtStart = why;
                ctx.Log.Info("headset not read at start: " + why);
                ShowHeadset();
                return;
            }
            DetectHeadset(HeadsetReadBy.Start);
        }

        /// <summary>
        /// Detect again, or the read at start: the runtime probe on a worker thread; Launch VR, Check and Restore saves wait for
        /// it. The status line follows only a probe the player asked for.
        /// </summary>
        private void DetectHeadset(HeadsetReadBy by)
        {
            if (session != null || saveRestore != null || detecting != null) return;
            bool asked = by == HeadsetReadBy.Detect;
            SaveSettings();
            detectAgain.Enabled = false;
            detectAgain.Text = "Detecting...";
            launch.Enabled = false;
            check.Enabled = false;
            restoreSaves.Enabled = false;
            if (asked)
                ShowStatus(StatusKind.Info, $"Asking the headset's runtime (up to {OpenXrProbe.DefaultTimeoutMs / 1000} s)...");
            detecting = Task.Run(() => ctx.DetectHeadset(by));
            ShowHeadset();
            detecting.ContinueWith(t =>
            {
                detecting = null;
                if (IsDisposed) return;
                notReadAtStart = null;
                detectAgain.Text = DetectText;
                detectAgain.Enabled = true;
                check.Enabled = true;
                restoreSaves.Enabled = true;
                if (t.IsFaulted)
                {
                    ctx.Log.Error((asked ? "detect again" : "the headset read at start") + " failed: " + t.Exception?.GetBaseException());
                    if (asked) ShowStatus(StatusKind.Problem, "Detect again stopped with an error: " + t.Exception?.GetBaseException().Message);
                }
                else if (asked && t.Result.Ok)
                {
                    var identity = Identity();
                    ShowStatus(StatusKind.Good, "Headset read: " + identity.Describe() + ". " + identity.AsksLabel + " "
                        + HeadsetView.Spaced(t.Result.Limits.Recommended) + " per eye.");
                }
                else if (asked)
                {
                    ShowStatus(StatusKind.Warning, "Detect again: " + LaunchPlanBuilder.HeadsetProblemOf(t.Result) + " Showing the last values.");
                }
                RunPreflight();
                ShowHeadset();
            }, TaskScheduler.FromCurrentSynchronizationContext());
        }
    }
}
