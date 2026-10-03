using System;
using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>What forward on the move stick moves you toward (the layer's <c>ETERNALVR_LOCOMOTION</c>): where you look, or
    /// where the left or the right hand points, whatever the weapon hand.</summary>
    public enum LocomotionMode { Look, LeftHand, RightHand }

    /// <summary>The Move toward setting in launcher.ini: <c>locomotion = look|left|right</c>. The values from before are still
    /// read: <c>head</c> is look, and <c>hand</c> (the hand with the move stick) is the hand it was with the built-in controls,
    /// the right one with Left (buttons and sticks) and the left one otherwise. A custom stick layout is not looked at.</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>What forward on the move stick moves you toward; where you look by default.</summary>
        public LocomotionMode Locomotion { get; set; } = LocomotionMode.Look;

        /// <summary>The settings file's and the layer's <c>ETERNALVR_LOCOMOTION</c> value: look, left or right.</summary>
        public static string LocomotionName(LocomotionMode l) =>
            l == LocomotionMode.LeftHand ? "left" : l == LocomotionMode.RightHand ? "right" : "look";

        /// <summary>The hand an older file's <c>locomotion = hand</c> moved toward: the one with the move stick, which is on
        /// the right only with Left (buttons and sticks).</summary>
        public static LocomotionMode MoveHandOf(Handedness hand) =>
            hand == Handedness.LeftMirrored ? LocomotionMode.RightHand : LocomotionMode.LeftHand;

        /// <summary>Reads <c>locomotion</c> (any case); look for anything else. Needs the handedness read first.</summary>
        private void ReadLocomotion(IDictionary<string, string> map)
        {
            if (!map.TryGetValue("locomotion", out var lm)) return;
            Locomotion = string.Equals(lm?.Trim(), "hand", StringComparison.OrdinalIgnoreCase)
                ? MoveHandOf(Hand)
                : Pick(lm, LocomotionMode.Look, ("left", LocomotionMode.LeftHand), ("right", LocomotionMode.RightHand));
        }

        private void WriteLocomotion(StringBuilder sb) => sb.AppendLine("locomotion = " + LocomotionName(Locomotion));
    }
}
