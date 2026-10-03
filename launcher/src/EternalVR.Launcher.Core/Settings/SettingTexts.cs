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
            [Setting.Headset] = new Text("Headset",
                "Your headset and how the game reaches it (the OpenXR runtime). Read when the launcher opens, if its runtime "
                + "is already running, and at every Launch VR; on SteamVR the model is the one SteamVR last saw."),
            [Setting.NativePanel] = new Text("Native panel",
                "Your headset's own screen per eye, from the launcher's list of headsets: no runtime tells it."),
            [Setting.HeadsetAsks] = new Text("Asks for",
                "The size the runtime asks the game to render each eye at. It follows the runtime's quality setting, not the panel."),
            [Setting.Refresh] = new Text("Refresh",
                "Your headset's refresh rate in your last session, from the mod's log: steady, held to a half or a third of it "
                + "for part of play (the runtime's smoothing or throttling), or changing."),
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
            [Setting.GloryKills] = new Text("Glory kills",
                "How a glory kill is shown. Follow the camera: your view goes with the game's camera through the kill "
                + "and turns with your head from there. Steady view: you still see the kill up close, but the view never "
                + "turns on its own, only when you turn your head, and you face the same way after it. Flat screen: the "
                + "kill plays on a flat screen in front of you, like a cutscene. Fade out: the view goes dark for the kill "
                + "and comes back when it ends.",
                "Follow the camera (intense)", "Steady view", "Flat screen", "Fade out"),
            [Setting.WalkInRoom] = new Text("Room-scale",
                "Walking around your room moves the Slayer with you. Off: you can still lean and peek about 60 cm from where "
                + "you recentered; past that the view fades until you step back or recenter."),
            [Setting.HeadFade] = new Text("Fade in walls",
                "The view fades to black when your head goes into a wall or too far from your body, so you cannot see "
                + "through the level. If you are stuck in the dark for a moment, the game moves you back onto your body."),
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
            [Setting.ThrowGesture] = new Text("Throw grenades",
                "Throw a grenade with your off hand, as you would throw a ball: bring the hand up beside your ear, then "
                + "swing it forward. It fires the equipment launcher, like its button; the grenade flies where you aim. "
                + "Off by default. Experimental: not yet tried in a headset."),
            [Setting.HandsJump] = new Text("Jump with both hands",
                "Throw both hands up above your head, fast, to jump, like in DOOM VFR. The jump button still works. "
                + "Sitting, the hands have to go higher. Off by default: two-handed moves can set it off by accident."),
            [Setting.SwingGesture] = new Text("Overhead swing",
                "Swing the Crucible (the Sentinel Hammer in The Ancient Gods Part Two) with your weapon hand: raise it above "
                + "your head, then bring it down hard. It does what the Crucible button does. Both hands up does nothing. "
                + "Off by default. Experimental: not yet tried in a headset."),
            [Setting.AimWith] = new Text("Aim with",
                "What aims your weapon: your weapon hand (motion controllers), your head, or the mouse as in the flat game.",
                "Weapon hand", "Head", "Mouse"),
            [Setting.RevenantAimWith] = new Text("Revenant aim with",
                "What aims the Revenant's cannons. Only applies while you pilot the Revenant in Cultist Base. Same as Aim with "
                + "follows that setting. Weapon hand: the cannons and the demon's hands follow your controller. Head: they follow "
                + "where you look, which keeps them out of the way when you walk around or turn in your room.",
                "Same as Aim with", "Weapon hand", "Head"),
            [Setting.WeaponHand] = new Text("Weapon hand",
                "The hand that holds the gun. Left swaps the buttons between the hands; the second left option also swaps the sticks.",
                "Right", "Left (buttons swapped)", "Left (buttons and sticks)"),
            [Setting.MoveToward] = new Text("Move toward",
                "What pushing the move stick forward moves you toward: where you look, where your left hand points, or "
                + "where your right hand points, with either Weapon hand.",
                "Where you look", "Where your left hand points", "Where your right hand points"),
            [Setting.XButton] = new Text("X button",
                "Which press of the X button opens the Dossier; the other one switches equipment. Hold means a quarter of "
                + "a second or more.",
                "Hold for Dossier (default)", "Tap for Dossier"),
            [Setting.DossierMapSticks] = new Text("Dossier map sticks",
                "Which stick moves the Dossier's map. On the map one stick pans it and the other zooms (up and down) and "
                + "turns it (left and right). By default your weapon hand's stick pans; choose the other hand to swap them.",
                "Weapon hand pans (default)", "Other hand pans"),
            [Setting.WeaponWheel] = new Text("Weapon wheel",
                "How you pick a weapon on the weapon wheel. Stick: hold the wheel open and push the stick toward a weapon. "
                + "Point with your hand: hold the wheel open with its stick or button, then turn your weapon hand toward "
                + "a weapon (a small turn of the wrist is enough) and let go to pick it.",
                "Stick (default)", "Point with your hand"),
            [Setting.AimSteadiness] = new Text("Aim steadiness",
                "Steadies the gun and the aim dot against the small shake of a held hand. Higher is steadier but the gun "
                + "follows your hand a little later.",
                AimSteadiness.Names),
            [Setting.AimDot] = new Text("Aim dot",
                "A dot where your weapon hand aims, in place of the game's crosshair."),
            [Setting.Vibration] = new Text("Vibration",
                "How strongly the controllers vibrate. They pulse when you fire, punch, point at and click menus, and when the "
                + "game rumbles. Off turns vibration off.",
                Vibration.Names),
            [Setting.Bhaptics] = new Text("bHaptics (experimental)",
                "For bHaptics vests and arm sleeves. Start the bHaptics Player on this PC with your suit connected, then the "
                + "game: you feel your shots on the weapon arm, hits on the vest from the side they came from, a heartbeat at "
                + "low health, glory kills, landing from a fall, jump pads, pickups, portals and death. Nothing happens without "
                + "the Player running. Experimental: a player has felt the shots, the landing and glory kills on a real suit; "
                + "the other effects have not been felt yet."),
            [Setting.ButtonLayout] = new Text("Button layout",
                "Change which button does what. Edit controls lets you pick an action for each button of your controllers, "
                + "for each weapon hand, and saves them for the VR settings profile in use: each profile keeps its own controls. "
                + "Open folder opens that profile's controls folder, to edit the files by hand or delete yours to go back to the "
                + "built-in controls."),
            [Setting.Resolution] = new Text("Resolution",
                "How many pixels each eye renders: a size times the number beside it (0.50 to 2.00). Auto is what the runtime "
                + "asks for, fitted within about 4.6 million pixels per eye; Virtual Desktop native (SteamVR native...) is exactly "
                + "what the runtime asks for; Quest 3 native (Index native...) is your headset's own panel.",
                "Auto (fits about 4.6 MP per eye)", "Runtime native", "Headset native"),
            [Setting.EachEye] = new Text("Each eye",
                "The size each eye renders at, per side against what the runtime asks for and the native panel; with DLSS, about "
                + "the size DLSS draws. After a session held to the window's size (AMD Radeon RX 5000 and 6000), that size first."),
            [Setting.AntiAliasing] = new Text("Anti-aliasing",
                "How edges are smoothed in each eye. TAA (recommended) is the game's own: smooth edges, with each eye "
                + "keeping its own history. DLSS needs an NVIDIA RTX card: it renders a smaller image and scales it up, which "
                + "helps only when the graphics card is what limits the frame rate; when the processor is, it can be slower "
                + "than TAA. Its quality, version and preset are in the DLSS group. Off turns anti-aliasing and the game's "
                + "other temporal effects off: sharp, with some shimmer on edges and shiny surfaces, and a little lighter on "
                + "the graphics card. The game's own DLSS setting in its video menu is not used in VR with DLSS or Off (it "
                + "shows what runs, and your flat game keeps its setting); with TAA it is used: if it is on, DLSS runs in VR.",
                "TAA (recommended)", "DLSS (RTX cards)", "Off"),
            [Setting.Sharpening] = new Text("Sharpening",
                "The game's sharpening filter, applied to each eye's finished picture, with any anti-aliasing. The game's "
                + "setting is the one from its own video menu (Advanced, Sharpening); the others hold it at a fixed strength "
                + "in VR and leave your flat game's setting as it was. Low to High make distant detail crisper, at the cost "
                + "of some shimmer on fine edges.",
                "The game's setting", "Off", "Low", "Medium", "High"),
            [Setting.Foveation] = new Text("Foveated rendering (experimental)",
                "Shades the edges of each eye at a lower rate, where the lenses blur anyway, so the graphics card does less "
                + "work (about 16% more frames per second with Balanced at a Quest 3's size on the test rig). "
                + "It changes the frame rate only when the graphics card is what limits it. Subtle keeps the most of each eye "
                + "at full detail, Maximum the least. NVIDIA RTX only; other cards ignore it. Off by default. Experimental.",
                "Off", "Subtle", "Balanced", "Aggressive", "Maximum"),
            [Setting.FramePacing] = new Text("Frame pacing",
                "How the game's frames are timed against your headset's. As fast as the game runs: the game draws as many "
                + "frames as it can and the headset shows the newest one at each of its frames; looking around is smooth, "
                + "but when the game draws more frames than the headset shows, moving things (the world, your gun, the "
                + "Slayer's movement) advance by uneven steps. Matched to the headset: the game draws exactly one frame for "
                + "each headset frame, started at the same moment of each, as native VR games do, so motion advances evenly. "
                + "It helps when the game runs faster than your headset's refresh rate; below it nothing changes. "
                + "If your headset's own smoothing takes over (Virtual Desktop's SSW, Meta's ASW, SteamVR's Motion Smoothing or "
                + "throttling), your headset asks for half the frames and the game is matched to that half. "
                + "Matched to the headset by default.",
                "As fast as the game runs", "Matched to the headset (default)"),
            [Setting.TextureStreaming] = new Text("Texture streaming",
                "How the game loads texture detail as you play. Only what you see loads the detail the current view needs "
                + "instead of also caching extra detail ahead of time, which saves the processor a lot of work in VR (about 8% "
                + "more frames per second on the test rig). Mostly lossless: textures may sharpen a moment later when you turn "
                + "fast or enter a new area. On by default. The game's own setting is put back after you play."),
            [Setting.ParallelEyes] = new Text("Parallel Eye Rendering (experimental)",
                "Renders both eyes in one game frame, their work running at the same time, instead of one eye after the "
                + "other. Only on the Steam version this release supports; other versions use the standard renderer. The "
                + "game's async compute is off while it is on. Off by default. Experimental."),
            [Setting.CpuSaver] = new Text("CPU Saver (experimental)",
                "Turns down a few of the game's detail settings that cost processor time for each eye's picture, one checkbox "
                + "each; point at one to see what it changes, what it gained on the test rig and what it costs in the picture. "
                + "In VR the game draws every scene twice, once per eye. All of them together with Texture streaming gave about "
                + "24% more frames per second on the test rig. All off by default. The game's own settings are put back after "
                + "you play. Experimental: the list is still being measured."),
            [Setting.VrMode] = new Text("VR mode",
                "Stereo renders one image per eye, for real depth. Mono renders one image for both eyes: flat, but faster.",
                "Stereo (one image per eye)", "Mono (one for both eyes)"),
            [Setting.AlternateEyes] = new Text("Alternate eyes",
                "Draws one eye per game frame instead of both, taking turns. The processor does about half the work per "
                + "frame, so the game runs faster on a slower processor, but each eye updates at half the rate. Fast "
                + "motion can look doubled or smeared, and some people find it uncomfortable. Auto does this only when "
                + "your processor cannot keep up with your headset, and draws both eyes again once it can.",
                "Off", "Auto (when needed)", "On (for slower processors)"),
            [Setting.WorldSize] = new Text("World size",
                "How big the world feels around you. 1.00 is the game's own scale."),
            [Setting.EyeDistance] = new Text("Eye distance",
                "The distance between your eyes the game renders with: the headset's, or your own in millimetres (most "
                + "people are between 58 and 70)."),
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
                "Where health, armour and ammo are shown: on the HUD panel with the rest; on the inside of your off hand's "
                + "wrist, shown when you turn it toward you; or on your weapon, where the ammo sits just above the back of the "
                + "gun and health and armour stay on the panel. The wrist and the weapon are experimental.",
                "On the HUD panel", "On your wrist", "On your weapon"),
            [Setting.DlssQuality] = new Text("Quality",
                "How large the image DLSS scales up from: DLAA renders each eye at its full size, Quality at two thirds, "
                + "Balanced at 58%, Performance at half and Ultra Performance at a third. The smaller, the faster and the softer. "
                + "DLAA needs a newer DLSS than the game's (Version); with the game's it runs as Quality.",
                DlssDll.QualityChoiceNames()),
            [Setting.DlssVersion] = new Text("Version",
                "The DLSS that runs. NVIDIA's newest (recommended) looks sharper and smears less in motion than the game's "
                + "own 2.3: Download fetches it once from NVIDIA's GitHub, after you accept NVIDIA's license, and keeps it in "
                + "the launcher's data folder. Load file... uses an nvngx_dlss.dll of yours, where it is. Nothing is copied "
                + "into the game folder. If the game cannot use the newer file, it keeps its own.",
                "Newest from NVIDIA (recommended)", "The game's (2.3)", "Load file..."),
            [Setting.DlssPreset] = new Text("Preset",
                "How the newer DLSS renders. K is the transformer model at every quality, the sharpest picture; J, M and L "
                + "are its variants; F is the older model. Automatic lets NVIDIA pick for each quality.",
                DlssDll.PresetNames),
            [Setting.DlssInHeadset] = new Text("In the headset",
                "The DLSS that will run in VR with these settings, both eyes the same. The mod's log and Export report "
                + "name the version and preset the game really used."),
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
                "Where DOOM Eternal is installed. The launcher finds it through Steam or Game Pass; choose the folder yourself only if "
                + "that fails (the one with DOOMEternalx64vk.exe; for Game Pass, its Content folder)."),
            [Setting.Runtime] = new Text("OpenXR runtime",
                "The program that connects the game to your headset. The system default is the one your headset's software "
                + "set; choose another only if you use more than one."),
            [Setting.ExtraArguments] = new Text("Extra game arguments",
                "Extra command-line text for the game, for testing. Multiplayer arguments are refused."),
        };

        public static Text For(Setting setting) => Texts.TryGetValue(setting, out var t) ? t : null;
    }
}
