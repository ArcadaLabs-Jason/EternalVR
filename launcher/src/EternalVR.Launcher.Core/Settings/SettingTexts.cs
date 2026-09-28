using System.Collections.Generic;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>The window's words for every setting: its label, its tooltip and, for a list, its choices (in enum order).</summary>
    public static class SettingTexts
    {
        public sealed class Text
        {
            public Text(string label, string tooltip, params string[] choices)
            {
                Label = label;
                Tooltip = tooltip;
                Choices = choices;
            }

            public string Label { get; }
            public string Tooltip { get; }
            public IReadOnlyList<string> Choices { get; }
        }

        private static readonly Dictionary<Setting, Text> Texts = new Dictionary<Setting, Text>
        {
            [Setting.Turning] = new Text("Turning",
                "How the right stick turns you. Smooth turns continuously; snap jumps by a fixed angle, which many people find "
                + "more comfortable; off leaves turning to your body.",
                "Smooth", "Snap", "Off"),
            [Setting.TurnSpeed] = new Text("Turn speed",
                "How fast smooth turning turns with the stick pushed all the way, in degrees per second."),
            [Setting.SnapAngle] = new Text("Snap angle",
                "How far one flick of the stick turns you with snap turning, in degrees."),
            [Setting.Vignette] = new Text("Vignette",
                "Darkens the edges of your view while the stick moves or turns you (and during a dash or a glory kill), "
                + "leaving the middle clear. Less motion at the edges of your eyes helps if moving with the stick makes you "
                + "feel sick. Strong narrows the view more than light. Moving your head never shows it.",
                "Off", "Light", "Strong"),
            [Setting.WalkInRoom] = new Text("Room-scale",
                "Walking around your room moves the Slayer with you. Off: you can still lean and peek about 60 cm from where "
                + "you recentered; past that the view fades until you step back or recenter."),
            [Setting.RecenterHold] = new Text("Recenter hold",
                "Hold both sticks pressed for 2 seconds to turn the game to where you face and reset your height. "
                + "A single stick click still melees at once. The headset's own recenter works too."),
            [Setting.SkipCutscenes] = new Text("Skip cutscenes",
                "Skips the game's cutscenes automatically. The first part of the very first cinematic cannot be skipped: "
                + "the game makes you watch it."),
            [Setting.PlayPosition] = new Text("Play position",
                "Whether you play sitting or standing. Detect decides from your height when the game starts.",
                "Detect", "Sitting", "Standing"),
            [Setting.EyeHeight] = new Text("Eye height",
                "The game's: your eyes at the Slayer's height, however tall you are. Your real height: your own eye height "
                + "above the floor (needs a headset that knows where the floor is).",
                "The game's (Slayer)", "Your real height"),
            [Setting.AimWith] = new Text("Aim with",
                "What aims your weapon: your weapon hand (motion controllers), your head, or the mouse as in the flat game.",
                "Weapon hand", "Head", "Mouse"),
            [Setting.WeaponHand] = new Text("Weapon hand",
                "The hand that holds the gun. Left swaps the buttons between the hands; the second left option also swaps the sticks.",
                "Right", "Left (buttons swapped)", "Left (buttons and sticks)"),
            [Setting.MoveToward] = new Text("Move toward",
                "What pushing the left stick forward moves you toward: where you look, or where your off hand points.",
                "Where you look", "Where your off hand points"),
            [Setting.XButton] = new Text("X button",
                "Which press of the X button opens the Dossier; the other one switches equipment. Hold means a quarter of "
                + "a second or more.",
                "Hold for Dossier (default)", "Tap for Dossier"),
            [Setting.AimSteadiness] = new Text("Aim steadiness",
                "Steadies the gun and the aim dot against the small shake of a held hand. Higher is steadier but the gun "
                + "follows your hand a little later; lower it if the gun feels like it trails behind.",
                AimSteadiness.Names),
            [Setting.AimDot] = new Text("Aim dot",
                "A dot where your weapon hand aims, in place of the game's crosshair."),
            [Setting.Vibration] = new Text("Vibration",
                "How strongly the controllers vibrate. They pulse when you fire, punch, point at and click menus, and when the "
                + "game rumbles. Off turns vibration off.",
                Vibration.Names),
            [Setting.ButtonLayout] = new Text("Button layout",
                "Change which button does what. Edit controls opens your controls folder, with a copy of the built-in controls "
                + "in its defaults folder: copy the file for your controllers into the controls folder, edit it and start the "
                + "game. Delete your copy to go back to the built-in controls."),
            [Setting.Resolution] = new Text("Resolution",
                "The detail each eye is rendered with. 1.00 is your headset's recommended size, kept within what a fast card "
                + "can render at the headset's refresh rate. Raise it for a sharper picture if your card has headroom; lower it "
                + "if the frame rate drops."),
            [Setting.AntiAliasing] = new Text("Anti-aliasing",
                "How edges are smoothed in each eye. TAA (recommended) is the game's own: smooth edges, with each eye "
                + "keeping its own history. Off turns anti-aliasing and the game's other temporal effects off: sharp, "
                + "with some shimmer on edges and shiny surfaces, and a little lighter on the graphics card. DLSS is "
                + "experimental and "
                + "needs an NVIDIA RTX card: it renders a smaller image and scales it up (Quality the least, Ultra "
                + "Performance the most), which helps only when the graphics card is what limits the frame rate; when the "
                + "processor is, it can be slower than TAA. Choose it here: the game's own DLSS setting in its video menu "
                + "is not used in VR, and it may show DLSS as off.",
                "TAA (recommended)", "DLSS Quality", "DLSS Balanced", "DLSS Performance", "DLSS Ultra Performance", "Off"),
            [Setting.VrMode] = new Text("VR mode",
                "Stereo renders one image per eye, for real depth. Mono renders one image for both eyes: flat, but faster.",
                "Stereo (one image per eye)", "Mono (one for both eyes)"),
            [Setting.WorldSize] = new Text("World size",
                "How big the world feels around you. 1.00 is the game's own scale; change it a little if rooms and demons "
                + "feel too big or too small."),
            [Setting.EyeDistance] = new Text("Eye distance",
                "The distance between your eyes the game renders with. Leave it to the headset unless the world's depth "
                + "feels wrong; then set your own in millimetres (most people are between 58 and 70)."),
            [Setting.DesktopWindow] = new Text("Desktop window",
                "What the game's small window on your desktop shows while you play: one eye's view, or nothing.",
                "The left eye", "The right eye", "Nothing (black)"),
            [Setting.DesktopMonitor] = new Text("Window monitor",
                "Where the desktop window goes. Automatic puts it on the headset app's virtual display if there is one (you may "
                + "not see it), otherwise on the primary monitor. Pick a monitor to watch or stream the game.",
                "Automatic", "Primary monitor"),
            [Setting.DesktopSize] = new Text("Window size",
                "The size of the desktop window. It is made smaller if the monitor is smaller. Fill the monitor covers the "
                + "whole monitor chosen under Window monitor, borderless, for others watching. Turn on Crop to 16:9 for an "
                + "undistorted picture.",
                MirrorSettings.SizeNames),
            [Setting.DesktopCrop] = new Text("Crop to 16:9",
                "Show only the middle 16:9 part of the eye view, filling the window with no black bars. Good for streaming or "
                + "recording. Off shows the whole (taller) eye view with bars at the sides."),
            [Setting.CutsceneView] = new Text("Cutscene view",
                "Where cutscenes that play are shown: on a flat screen in front of you (comfortable), or around you with the "
                + "game's own camera moving your view.",
                "Flat screen in front of you", "Around you (immersive)"),
            [Setting.CutsceneShape] = new Text("Cutscene screen",
                "The shape of the flat screen cutscenes play on: a 16:9 or 16:10 screen, or the whole eye image as the game "
                + "draws it.",
                "16:9", "16:10", "Full eye image"),
            [Setting.HudDistance] = new Text("HUD distance",
                "How far in front of you the HUD panel (health, armour, ammo) floats, in metres."),
            [Setting.HudSize] = new Text("HUD size",
                "How wide the HUD panel is, in metres. The menus take the same size."),
            [Setting.HudHeight] = new Text("HUD height",
                "Moves the HUD panel up or down from eye level, in metres (negative is lower)."),
            [Setting.HudPlace] = new Text("Health and ammo",
                "Where health, armour and ammo are shown: on the HUD panel with the rest, or on the inside of your off hand's "
                + "wrist, shown when you turn it toward you (experimental).",
                "On the HUD panel", "On your wrist"),
            [Setting.MotionControllers] = new Text("Motion controllers",
                "Play with your headset's controllers. Off: keyboard, mouse or a gamepad, as in the flat game."),
            [Setting.ShotsFrom] = new Text("Shots come from",
                "Where your shots start with the weapon hand aiming. Your weapon hand is the natural choice; your eyes stop "
                + "shots from passing through a thin wall when the gun is pushed into it.",
                "Your weapon hand", "Your eyes"),
            [Setting.AimDotSize] = new Text("Aim dot size",
                "How big the aim dot looks, in degrees of your view (1.0 is about the width of a finger at arm's length)."),
            [Setting.MenuLaser] = new Text("Menu laser",
                "A laser from your hand to the menu panel. Off keeps only the dot on the panel."),
            [Setting.GameFolder] = new Text("Game folder",
                "Where DOOM Eternal is installed. The launcher finds it through Steam; choose the folder yourself only if "
                + "that fails (the one with DOOMEternalx64vk.exe)."),
            [Setting.Runtime] = new Text("OpenXR runtime",
                "The program that connects the game to your headset. The system default is the one your headset's software "
                + "set; choose another only if you use more than one."),
            [Setting.ExtraArguments] = new Text("Extra game arguments",
                "Extra command-line text for the game, for testing. Multiplayer arguments are refused."),
        };

        public static Text For(Setting setting) => Texts.TryGetValue(setting, out var t) ? t : null;
    }
}
