using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Controls
{
    public enum ControlHand { Left, Right }

    /// <summary>The buttons of a control map (the layer's <c>ButtonInput</c>, features/input/binding_profile.hpp).</summary>
    public enum ButtonInput { Trigger, Grip, StickClick, Primary, Secondary, Menu }

    /// <summary>How a button is pressed: held down, a short tap, or held for a quarter of a second.</summary>
    public enum PressKind { Press, Tap, Hold }

    /// <summary>The turn stick's gestures.</summary>
    public enum StickGesture { Up, DownTap, DownHold }

    public enum StickRole { None, Move, Turn }

    public enum BindingKeyKind { WeaponHand, StickRole, Button, Gesture }

    /// <summary>
    /// A key of a control map, as the layer reads it (features/input/binding_keys.hpp): <c>weapon_hand</c>,
    /// <c>&lt;hand&gt;.stick.role</c>, <c>&lt;hand&gt;.&lt;input&gt;.&lt;press&gt;</c> or <c>&lt;hand&gt;.stick.&lt;gesture&gt;</c>.
    /// </summary>
    public readonly struct BindingKey : IEquatable<BindingKey>
    {
        private BindingKey(BindingKeyKind kind, ControlHand hand, ButtonInput input, PressKind press, StickGesture gesture)
        {
            Kind = kind;
            Hand = hand;
            Input = input;
            Press = press;
            Gesture = gesture;
        }

        public BindingKeyKind Kind { get; }
        public ControlHand Hand { get; }
        public ButtonInput Input { get; }
        public PressKind Press { get; }
        public StickGesture Gesture { get; }

        public static BindingKey WeaponHand() => new BindingKey(BindingKeyKind.WeaponHand, ControlHand.Right, default, default, default);
        public static BindingKey Role(ControlHand hand) => new BindingKey(BindingKeyKind.StickRole, hand, default, default, default);
        public static BindingKey Button(ControlHand hand, ButtonInput input, PressKind press) =>
            new BindingKey(BindingKeyKind.Button, hand, input, press, default);
        public static BindingKey StickGestureOf(ControlHand hand, StickGesture gesture) =>
            new BindingKey(BindingKeyKind.Gesture, hand, default, default, gesture);

        /// <summary>Reads a key the way the layer does; false for anything it does not know.</summary>
        public static bool TryParse(string text, out BindingKey key)
        {
            key = default;
            if (text == null) return false;
            if (text == ControlNames.WeaponHandKey)
            {
                key = WeaponHand();
                return true;
            }
            var parts = text.Split('.');
            if (parts.Length != 3 || !ControlNames.TryParseHand(parts[0], out var hand)) return false;
            if (parts[1] == "stick")
            {
                if (parts[2] == "role")
                {
                    key = Role(hand);
                    return true;
                }
                int g = Array.IndexOf(ControlNames.GestureNames, parts[2]);
                if (g < 0) return false;
                key = StickGestureOf(hand, (StickGesture)g);
                return true;
            }
            int input = Array.IndexOf(ControlNames.InputNames, parts[1]);
            int press = Array.IndexOf(ControlNames.PressNames, parts[2]);
            if (input < 0 || press < 0) return false;
            key = Button(hand, (ButtonInput)input, (PressKind)press);
            return true;
        }

        public override string ToString()
        {
            var hand = ControlNames.HandName(Hand);
            switch (Kind)
            {
                case BindingKeyKind.WeaponHand: return ControlNames.WeaponHandKey;
                case BindingKeyKind.StickRole: return hand + ".stick.role";
                case BindingKeyKind.Button: return hand + "." + ControlNames.InputNames[(int)Input] + "." + ControlNames.PressNames[(int)Press];
                default: return hand + ".stick." + ControlNames.GestureNames[(int)Gesture];
            }
        }

        /// <summary>
        /// Where the key goes in a map, in the order the layer writes binding text (binding_text.cpp): the weapon hand, then per
        /// hand its stick role, its buttons and its stick gestures.
        /// </summary>
        public int Rank
        {
            get
            {
                if (Kind == BindingKeyKind.WeaponHand) return 0;
                int hand = 1000 + (int)Hand * 100;
                if (Kind == BindingKeyKind.StickRole) return hand;
                if (Kind == BindingKeyKind.Button) return hand + 10 + (int)Input * 3 + (int)Press;
                return hand + 50 + (int)Gesture;
            }
        }

        public bool Equals(BindingKey other) => ToString() == other.ToString();
        public override bool Equals(object obj) => obj is BindingKey k && Equals(k);
        public override int GetHashCode() => ToString().GetHashCode();
    }

    /// <summary>An action a control map can bind (the layer's <c>game::gameActionName</c>), with its name in the launcher.</summary>
    public sealed class ControlAction
    {
        public ControlAction(string name, string label)
        {
            Name = name;
            Label = label;
        }

        public string Name { get; }
        public string Label { get; }
        public override string ToString() => Label;
    }

    /// <summary>The names of control maps: hands, inputs, presses, gestures, stick roles, actions and map sections.</summary>
    public static class ControlNames
    {
        public const string WeaponHandKey = "weapon_hand";
        /// <summary>Written as an action, leaves the button free (the layer's <c>kUnboundValue</c>).</summary>
        public const string Unbound = "none";
        public const string ProfileSection = "profile";
        public const string MapSectionPrefix = "map.";

        internal static readonly string[] InputNames = { "trigger", "grip", "stick_click", "primary", "secondary", "menu" };
        internal static readonly string[] PressNames = { "press", "tap", "hold" };
        internal static readonly string[] GestureNames = { "up", "down_tap", "down_hold" };
        internal static readonly string[] RoleNames = { "none", "move", "turn" };

        /// <summary>The OpenXR action of each button in a <c>[profile]</c> section (<c>gameplay.&lt;hand&gt;.&lt;action&gt;</c>).</summary>
        internal static readonly string[] InputActions = { "trigger", "grip", "thumbstick_click", "primary", "secondary", "menu" };
        internal const string StickAction = "thumbstick";

        /// <summary>Every action, in the order of the layer's <c>GameAction</c> (features/input, game/eternal/game_action.cpp).</summary>
        public static readonly IReadOnlyList<ControlAction> Actions = new[]
        {
            new ControlAction("fire", "Fire"),
            new ControlAction("weapon_mod", "Weapon mod"),
            new ControlAction("switch_weapon_mod", "Switch weapon mod"),
            new ControlAction("jump", "Jump"),
            new ControlAction("dash", "Dash"),
            new ControlAction("melee", "Melee (and use)"),
            new ControlAction("chainsaw", "Chainsaw"),
            new ControlAction("flame_belch", "Flame Belch"),
            new ControlAction("equipment", "Equipment launcher"),
            new ControlAction("switch_equipment", "Switch equipment"),
            new ControlAction("quick_switch", "Quick switch (last weapon)"),
            new ControlAction("weapon_wheel", "Weapon wheel"),
            new ControlAction("next_weapon", "Next weapon"),
            new ControlAction("previous_weapon", "Previous weapon"),
            new ControlAction("weapon_slot_1", "Weapon slot 1"),
            new ControlAction("weapon_slot_2", "Weapon slot 2"),
            new ControlAction("weapon_slot_3", "Weapon slot 3"),
            new ControlAction("weapon_slot_4", "Weapon slot 4"),
            new ControlAction("weapon_slot_5", "Weapon slot 5"),
            new ControlAction("weapon_slot_6", "Weapon slot 6"),
            new ControlAction("weapon_slot_7", "Weapon slot 7"),
            new ControlAction("weapon_slot_8", "Weapon slot 8"),
            new ControlAction("crucible", "Crucible"),
            new ControlAction("pause", "Pause menu"),
            new ControlAction("dossier", "Dossier"),
            new ControlAction("mission_info", "Mission info"),
            new ControlAction("automap", "Automap"),
            new ControlAction("recenter", "Recenter"),
        };

        public static ControlAction FindAction(string name) => Actions.FirstOrDefault(a => a.Name == name);

        /// <summary>The label of an action name, or the name itself in quotes when it is not an action.</summary>
        public static string ActionLabel(string name)
        {
            if (name == null || name == Unbound) return "nothing";
            var a = FindAction(name);
            return a != null ? a.Label : "'" + name + "'";
        }

        public static string HandName(ControlHand hand) => hand == ControlHand.Left ? "left" : "right";

        public static bool TryParseHand(string text, out ControlHand hand)
        {
            hand = text == "left" ? ControlHand.Left : ControlHand.Right;
            return text == "left" || text == "right";
        }

        public static string RoleName(StickRole role) => RoleNames[(int)role];

        public static bool TryParseRole(string text, out StickRole role)
        {
            int i = Array.IndexOf(RoleNames, text);
            role = i < 0 ? StickRole.None : (StickRole)i;
            return i >= 0;
        }

        public static string PressLabel(PressKind press) => press == PressKind.Press ? "press" : press == PressKind.Tap ? "tap" : "hold";

        public static string GestureLabel(StickGesture gesture) =>
            gesture == StickGesture.Up ? "up" : gesture == StickGesture.DownTap ? "down, tap" : "down, hold";

        public static string RoleLabel(StickRole role) => role == StickRole.Move ? "Move" : role == StickRole.Turn ? "Turn" : "None";

        /// <summary>The <c>[map.*]</c> section of a Weapon hand setting (the layer's <c>handednessName</c>).</summary>
        public static string MapSection(Handedness hand) =>
            MapSectionPrefix + (hand == Handedness.Left ? "left_button_swap" : hand == Handedness.LeftMirrored ? "left_full_mirror" : "right");

        public static IReadOnlyList<string> MapSections { get; } = new[]
        {
            MapSection(Handedness.Right), MapSection(Handedness.Left), MapSection(Handedness.LeftMirrored),
        };

        /// <summary>
        /// A controller input's name, from the path it is bound to in the <c>[profile]</c> section: "A", "trigger", "trackpad
        /// click". Null for a path it does not know.
        /// </summary>
        public static string PhysicalName(string path)
        {
            if (string.IsNullOrEmpty(path) || !path.StartsWith("/input/", StringComparison.Ordinal)) return null;
            var parts = path.Substring("/input/".Length).Split('/');
            var what = parts[0];
            var how = parts.Length > 1 ? parts[1] : null;
            switch (what)
            {
                case "a": case "b": case "x": case "y": return what.ToUpperInvariant();
                case "trigger": return "trigger";
                case "squeeze": return "grip";
                case "menu": return "Menu button";
                case "system": return "System button";
                case "thumbstick": return how == null ? "stick" : how == "click" ? "stick click" : null;
                case "trackpad": return how == null ? "trackpad" : how == "click" ? "trackpad click" : how == "force" ? "trackpad press" : null;
                case "thumbrest": return "thumb rest";
                case "shoulder": return "shoulder button";
                default: return null;
            }
        }

        /// <summary>A button's name when the controller's profile does not say which physical input it is.</summary>
        public static string GenericInputName(ButtonInput input)
        {
            switch (input)
            {
                case ButtonInput.Trigger: return "trigger";
                case ButtonInput.Grip: return "grip";
                case ButtonInput.StickClick: return "stick click";
                case ButtonInput.Primary: return "primary button";
                case ButtonInput.Secondary: return "secondary button";
                default: return "Menu button";
            }
        }

        public static string Capitalized(ControlHand hand) => hand == ControlHand.Left ? "Left" : "Right";

        /// <summary>The built-in controller files and their names, in the order the controls README lists them.</summary>
        private static readonly (string File, string Name)[] Families =
        {
            ("oculus_touch.toml", "Meta Quest and Rift (Touch)"),
            ("valve_index.toml", "Valve Index"),
            ("hp_reverb_g2.toml", "HP Reverb G2"),
            ("windows_mixed_reality.toml", "Windows Mixed Reality"),
            ("htc_vive_cosmos.toml", "HTC Vive Cosmos"),
            ("htc_vive_wand.toml", "HTC Vive wands"),
            ("pico4.toml", "Pico 4"),
        };

        /// <summary>The name shown for a built-in controller file: the README's, else the file name without its extension.</summary>
        public static string FamilyName(string fileName)
        {
            int i = FamilyOrder(fileName);
            return i < Families.Length ? Families[i].Name : System.IO.Path.GetFileNameWithoutExtension(fileName);
        }

        /// <summary>Where a controller file is listed: the README's order, then any others (int.MaxValue).</summary>
        public static int FamilyOrder(string fileName)
        {
            for (int i = 0; i < Families.Length; i++)
                if (string.Equals(Families[i].File, fileName, StringComparison.OrdinalIgnoreCase)) return i;
            return int.MaxValue;
        }
    }
}
