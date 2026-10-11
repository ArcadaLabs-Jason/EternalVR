using System;
using System.Collections.Generic;
using System.Linq;

namespace EternalVR.Launcher.Core.Controls
{
    /// <summary>
    /// The touch sensors each controller has, per hand (the layer's interaction_profiles.cpp, which checks every path against
    /// the OpenXR specification): which of a control map's touches the controls editor offers. The layer adds a touch's
    /// binding for the session when a map binds it (rest_touch_bindings.hpp), so a controls file needs no line for it.
    /// </summary>
    public static class TouchSensors
    {
        private static readonly string[] TouchFace = { "x", "y", "trigger", "thumbstick", "thumbrest" };
        private static readonly string[] TouchFaceRight = { "a", "b", "trigger", "thumbstick", "thumbrest" };
        private static readonly string[] Index = { "a", "b", "trigger", "thumbstick", "trackpad" };
        private static readonly string[] Pad = { "trackpad" };
        private static readonly string[] Stick = { "thumbstick" };
        private static readonly string[] PicoLeft = { "x", "y", "trigger", "thumbstick" };
        private static readonly string[] PicoRight = { "a", "b", "trigger", "thumbstick" };
        private static readonly string[] FrameLeft =
            { "dpad_up", "dpad_down", "dpad_left", "dpad_right", "view", "shoulder", "bumper", "squeeze", "trigger", "thumbstick" };
        private static readonly string[] FrameRight =
            { "a", "b", "x", "y", "menu", "shoulder", "bumper", "squeeze", "trigger", "thumbstick" };

        private static readonly Dictionary<string, (string[] Left, string[] Right)> Profiles =
            new Dictionary<string, (string[], string[])>(StringComparer.Ordinal)
            {
                ["/interaction_profiles/oculus/touch_controller"] = (TouchFace, TouchFaceRight),
                ["/interaction_profiles/valve/index_controller"] = (Index, Index),
                ["/interaction_profiles/hp/mixed_reality_controller"] = (new string[0], new string[0]),
                ["/interaction_profiles/microsoft/motion_controller"] = (Pad, Pad),
                ["/interaction_profiles/htc/vive_cosmos_controller"] = (Stick, Stick),
                ["/interaction_profiles/htc/vive_controller"] = (Pad, Pad),
                ["/interaction_profiles/bytedance/pico4_controller"] = (PicoLeft, PicoRight),
                ["/interaction_profiles/valve/frame_controller_valve"] = (FrameLeft, FrameRight),
            };

        /// <summary>The interaction profiles listed here.</summary>
        internal static IEnumerable<string> ProfilePaths => Profiles.Keys;

        /// <summary>True when the controller of <paramref name="profilePath"/> has a touch sensor on <c>/input/&lt;identifier&gt;</c>.</summary>
        public static bool Has(string profilePath, ControlHand hand, string identifier)
        {
            if (profilePath == null || !Profiles.TryGetValue(profilePath, out var touches)) return false;
            return (hand == ControlHand.Left ? touches.Left : touches.Right).Contains(identifier);
        }

        /// <summary>True for a touch on a control (A/X and B/Y, the stick, the trackpad, the trigger): not the thumb rest.</summary>
        public static bool IsControlTouch(ButtonInput input) => input >= ButtonInput.PrimaryTouch && input <= ButtonInput.TriggerTouch;

        /// <summary>True for any touch sensor: the thumb rest or a touch on a control.</summary>
        public static bool IsTouch(ButtonInput input) => input == ButtonInput.ThumbRest || IsControlTouch(input);

        /// <summary>
        /// The path of a touch on a control on this controller (<c>/input/a/touch</c>), from the path its button is bound to in
        /// the <c>[profile]</c> section (<paramref name="buttonPath"/>: <c>primary</c> or <c>secondary</c>, used for their touches);
        /// null when the controller has no such sensor. The primary and secondary buttons' touch is that of an A, B, X or Y button
        /// only, as in the layer: a trackpad or D-pad bound as one has none of its own.
        /// </summary>
        public static string TouchPath(string profilePath, ControlHand hand, ButtonInput input, Func<ButtonInput, string> buttonPath)
        {
            string identifier;
            switch (input)
            {
                case ButtonInput.PrimaryTouch: identifier = FaceIdentifier(buttonPath(ButtonInput.Primary)); break;
                case ButtonInput.SecondaryTouch: identifier = FaceIdentifier(buttonPath(ButtonInput.Secondary)); break;
                case ButtonInput.StickTouch: identifier = "thumbstick"; break;
                case ButtonInput.TrackpadTouch: identifier = "trackpad"; break;
                case ButtonInput.TriggerTouch: identifier = "trigger"; break;
                default: return null;
            }
            return identifier != null && Has(profilePath, hand, identifier) ? "/input/" + identifier + "/touch" : null;
        }

        private static string FaceIdentifier(string path)
        {
            if (path == null || !path.StartsWith("/input/", StringComparison.Ordinal)) return null;
            var what = path.Substring("/input/".Length).Split('/')[0];
            return what == "a" || what == "b" || what == "x" || what == "y" ? what : null;
        }
    }
}
