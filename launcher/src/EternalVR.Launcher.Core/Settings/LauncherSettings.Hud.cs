using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// The HUD's places in launcher.ini (docs/VR_HANDS_HUD.md, "HUD places"): <c>hud_front</c>, <c>hud_wrist</c> and
    /// <c>hud_weapon</c>, each a switch of its own. Up to 0.1.88 one choice, <c>hud = panel|wrist|weapon</c>, held the place:
    /// the whole HUD in front, the blocks on the wrist instead of in front, or the ammo on the weapon instead of in front. A file
    /// with no switch in it but that choice is read as the same thing in the switches. Saved, the file holds the switches and
    /// that choice as well (the hand HUD, else <c>panel</c>), so an older launcher still finds the player's hand HUD; a file
    /// with any switch in it is read by its switches only.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>
        /// Health, armor, ammo and equipment on the HUD panel in front of you (the layer's <c>ETERNALVR_HUD_FRONT</c>); on by
        /// default. Off, they show only where the wrist or the weapon puts them (nowhere with neither). The rest of the HUD
        /// (subtitles, prompts, boss bars) stays on the panel either way. Without the motion controllers there is no wrist or
        /// weapon, and the panel shows them whatever this says.
        /// </summary>
        public bool HudFront { get; set; } = true;
        /// <summary>Health, armor, ammo and equipment on the inside of the off hand's wrist, shown when you turn it toward you (the layer's <c>ETERNALVR_HUD=wrist</c>); off by default.</summary>
        public bool HudWrist { get; set; } = false;
        /// <summary>The ammo above the gun in the weapon hand (the layer's <c>ETERNALVR_HUD=weapon</c>); off by default, and not with the wrist (the layer has one hand HUD at a time).</summary>
        public bool HudWeapon { get; set; } = false;

        /// <summary>The layer's <c>ETERNALVR_HUD</c> value: the wrist, else the weapon, else none (<c>panel</c>); none without the controllers.</summary>
        public string HudPlaceName() => !Controllers ? "panel" : OlderHudChoice();

        /// <summary>The HUD in front shows the readouts: switched on, or without the controllers (no hand to show them).</summary>
        public bool HudFrontShown() => HudFront || !Controllers;

        /// <summary>The <c>hud</c> choice of a launcher up to 0.1.88 that is nearest the switches: the hand HUD, else the panel.</summary>
        private string OlderHudChoice() => HudWrist ? "wrist" : HudWeapon ? "weapon" : "panel";

        private static void ReadHud(IDictionary<string, string> map, LauncherSettings s)
        {
            bool any = false;
            if (map.TryGetValue("hud_front", out var f)) { s.HudFront = Flag(f); any = true; }
            if (map.TryGetValue("hud_wrist", out var w)) { s.HudWrist = On(w); any = true; }
            if (map.TryGetValue("hud_weapon", out var wp)) { s.HudWeapon = On(wp); any = true; }
            if (!any && map.TryGetValue("hud", out var old))
            {
                var place = Pick(old, "panel", ("wrist", "wrist"), ("weapon", "weapon"));
                s.HudFront = place == "panel";
                s.HudWrist = place == "wrist";
                s.HudWeapon = place == "weapon";
            }
            // One hand HUD at a time: a file with both keeps the wrist.
            if (s.HudWrist) s.HudWeapon = false;
        }

        private void WriteHud(StringBuilder sb)
        {
            sb.AppendLine("hud = " + OlderHudChoice()); // for a launcher up to 0.1.88; this one reads the switches
            sb.AppendLine("hud_front = " + (HudFront ? "1" : "0"));
            sb.AppendLine("hud_wrist = " + (HudWrist ? "1" : "0"));
            sb.AppendLine("hud_weapon = " + (HudWeapon && !HudWrist ? "1" : "0")); // one hand HUD at a time, as read
        }
    }
}
