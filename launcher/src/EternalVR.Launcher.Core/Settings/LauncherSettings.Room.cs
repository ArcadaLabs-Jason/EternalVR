using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>Room-scale in launcher.ini: <c>body_follow</c> (the body walks after the head) and <c>head_fade</c> (the view
    /// fades to black when the head goes into a wall or too far from the body).</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>Room-scale walking: the body follows the head (the layer's <c>ETERNALVR_BODY_FOLLOW</c>).</summary>
        public bool BodyFollow { get; set; } = true;
        /// <summary>The fade to black with the head in a wall or past the lean cap (the layer's <c>ETERNALVR_HEAD_FADE</c>);
        /// on by default.</summary>
        public bool HeadFade { get; set; } = true;

        private void ReadRoom(IDictionary<string, string> map)
        {
            if (map.TryGetValue("body_follow", out var bf)) BodyFollow = Flag(bf);
            if (map.TryGetValue("head_fade", out var hf)) HeadFade = Flag(hf);
        }

        private void WriteRoom(StringBuilder sb)
        {
            sb.AppendLine("body_follow = " + (BodyFollow ? "1" : "0"));
            sb.AppendLine("head_fade = " + (HeadFade ? "1" : "0"));
        }
    }
}
