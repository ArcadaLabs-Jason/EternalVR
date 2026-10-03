using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>What aims melee, or the equipment launcher and the Flame Belch, under hand aim (the layer's
    /// <c>ETERNALVR_MELEE_AIM</c> and <c>ETERNALVR_EQUIPMENT_AIM</c>): the same as <see cref="AimMode"/> (the weapon hand),
    /// the head, or the off hand.</summary>
    public enum ActionAimMode { Same, Head, OffHand }

    /// <summary>Melee and equipment aim in launcher.ini: <c>melee_aim = same|head|offhand</c> and
    /// <c>equipment_aim = same|head|offhand</c>.</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>What aims melee, Blood Punch and glory kills; the same as <see cref="Aim"/> by default.</summary>
        public ActionAimMode MeleeAim { get; set; } = ActionAimMode.Same;

        /// <summary>What aims the equipment launcher and the Flame Belch; the same as <see cref="Aim"/> by default.</summary>
        public ActionAimMode EquipmentAim { get; set; } = ActionAimMode.Same;

        /// <summary>The settings file's value: same, head or offhand.</summary>
        public static string ActionAimName(ActionAimMode m) =>
            m == ActionAimMode.Head ? "head" : m == ActionAimMode.OffHand ? "offhand" : "same";

        /// <summary>The layer's value for a choice other than <see cref="ActionAimMode.Same"/>: head or offhand.</summary>
        public static string ActionAimEnvironment(ActionAimMode m) => m == ActionAimMode.OffHand ? "offhand" : "head";

        /// <summary>Reads <c>melee_aim</c> and <c>equipment_aim</c> (any case); same for anything else.</summary>
        private void ReadActionAim(IDictionary<string, string> map)
        {
            if (map.TryGetValue("melee_aim", out var ma)) MeleeAim = ParseActionAim(ma);
            if (map.TryGetValue("equipment_aim", out var ea)) EquipmentAim = ParseActionAim(ea);
        }

        private static ActionAimMode ParseActionAim(string text) =>
            Pick(text, ActionAimMode.Same, ("head", ActionAimMode.Head), ("offhand", ActionAimMode.OffHand));

        private void WriteActionAim(StringBuilder sb)
        {
            sb.AppendLine("melee_aim = " + ActionAimName(MeleeAim));
            sb.AppendLine("equipment_aim = " + ActionAimName(EquipmentAim));
        }
    }
}
