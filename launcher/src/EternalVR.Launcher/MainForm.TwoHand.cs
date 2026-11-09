using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>The Advanced tab's Two-handed aim group (LauncherSettings.TwoHand.cs).</summary>
    public sealed partial class MainForm
    {
        private readonly CheckBox twoHandAim = new CheckBox { AutoSize = true };

        private GroupBox TwoHandGroup() => Group("Two-handed aim",
            Row(Setting.TwoHandAim, twoHandAim, s => twoHandAim.Checked = s.TwoHandAim, s => s.TwoHandAim = twoHandAim.Checked));
    }
}
