using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>The Gestures group in launcher.ini: <c>roll_wrist</c> (1 or 0, on by default), <c>swing_gesture</c> and
    /// <c>hands_jump</c> (1 or 0, off by default).</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>A fast roll of the weapon hand's wrist switches the weapon mod (the layer's <c>ETERNALVR_WRIST_FLICK</c>); on by default.</summary>
        public bool RollWrist { get; set; } = true;
        /// <summary>The weapon hand's overhead swing presses the Crucible (the layer's <c>ETERNALVR_SWING</c>); off by default.</summary>
        public bool SwingGesture { get; set; } = false;
        /// <summary>Both hands thrown up above the head jump (the layer's <c>ETERNALVR_HANDS_JUMP</c>); off by default.</summary>
        public bool HandsJump { get; set; } = false;

        private void ReadGestures(IDictionary<string, string> map)
        {
            if (map.TryGetValue("roll_wrist", out var rw)) RollWrist = On(rw);
            if (map.TryGetValue("swing_gesture", out var sg)) SwingGesture = On(sg);
            if (map.TryGetValue("hands_jump", out var hj)) HandsJump = On(hj);
        }

        private void WriteGestures(StringBuilder sb)
        {
            sb.AppendLine("roll_wrist = " + (RollWrist ? "1" : "0"));
            sb.AppendLine("swing_gesture = " + (SwingGesture ? "1" : "0"));
            sb.AppendLine("hands_jump = " + (HandsJump ? "1" : "0"));
        }
    }
}
