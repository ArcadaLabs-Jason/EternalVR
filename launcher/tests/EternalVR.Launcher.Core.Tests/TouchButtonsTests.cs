using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Controls;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Touches on controls bound like buttons in the controls editor (the layer's touch_inputs.hpp): rows only where the
    /// controller has the sensor, the keys as the layer reads them, no touch bound by default, and the note when a face
    /// button's touch is the thumb-rest wheel's rest.
    /// </summary>
    public class TouchButtonsTests
    {
        private static string ShippedMaps => Path.Combine(TestData.Dir, "controllers");

        private static ControlsFolder Prepared(TempDir t)
        {
            var controls = new ControlsFolder(t.Combine("controls"));
            controls.Prepare(ShippedMaps);
            return controls;
        }

        private static ControlsEdit Open(ControlsFolder controls, string fileName) =>
            ControlsEdit.Open(controls, controls.Families().Single(f => f.FileName == fileName));

        private static readonly ButtonInput[] Touches =
            { ButtonInput.PrimaryTouch, ButtonInput.SecondaryTouch, ButtonInput.StickTouch, ButtonInput.TrackpadTouch, ButtonInput.TriggerTouch };

        [Fact]
        public void TouchKeysReadAndWriteAsTheLayersDo()
        {
            foreach (var text in new[] { "right.primary_touch.tap", "left.secondary_touch.hold", "left.stick_touch.press", "right.trackpad_touch.tap", "right.trigger_touch.hold" })
            {
                Assert.True(BindingKey.TryParse(text, out var key));
                Assert.True(TouchSensors.IsControlTouch(key.Input));
                Assert.Equal(text, key.ToString());
            }
            foreach (var text in new[] { "left.thumbstick_touch.tap", "right.a_touch.tap", "right.trigger_touch", "right.primary.touch" })
                Assert.False(BindingKey.TryParse(text, out _));
            Assert.True(TouchSensors.IsTouch(ButtonInput.ThumbRest));
            Assert.False(TouchSensors.IsControlTouch(ButtonInput.ThumbRest));
            Assert.False(TouchSensors.IsTouch(ButtonInput.Primary));
        }

        [Fact]
        public void TheTouchNamesMatchTheLayersSource()
        {
            var keys = File.ReadAllText(RepoFile("src", "features", "input", "binding_keys.cpp"));
            foreach (var word in new[] { "primary_touch", "secondary_touch", "stick_touch", "trackpad_touch", "trigger_touch" })
                Assert.Contains("\"" + word + "\"", keys);
            var actions = File.ReadAllText(RepoFile("src", "features", "input", "xr_action_set.cpp"));
            foreach (var input in Touches)
                Assert.Contains("\"" + ControlNames.InputActions[(int)input] + "\"", actions);
        }

        /// <summary>Per controller, the touch rows the editor shows (the same per family as the layer's touch_inputs_tests.cpp).</summary>
        [Fact]
        public void TouchRowsShowOnlyWhereTheControllerHasTheSensor()
        {
            var expected = new Dictionary<string, (ButtonInput[] Left, ButtonInput[] Right)>
            {
                ["oculus_touch.toml"] = (new[] { ButtonInput.PrimaryTouch, ButtonInput.SecondaryTouch, ButtonInput.StickTouch, ButtonInput.TriggerTouch },
                                         new[] { ButtonInput.PrimaryTouch, ButtonInput.SecondaryTouch, ButtonInput.StickTouch, ButtonInput.TriggerTouch }),
                ["valve_index.toml"] = (Touches, Touches),
                ["hp_reverb_g2.toml"] = (new ButtonInput[0], new ButtonInput[0]),
                ["windows_mixed_reality.toml"] = (new[] { ButtonInput.TrackpadTouch }, new[] { ButtonInput.TrackpadTouch }),
                ["htc_vive_cosmos.toml"] = (new[] { ButtonInput.StickTouch }, new[] { ButtonInput.StickTouch }),
                ["htc_vive_wand.toml"] = (new[] { ButtonInput.TrackpadTouch }, new[] { ButtonInput.TrackpadTouch }),
                ["pico4.toml"] = (new[] { ButtonInput.PrimaryTouch, ButtonInput.SecondaryTouch, ButtonInput.StickTouch, ButtonInput.TriggerTouch },
                                  new[] { ButtonInput.PrimaryTouch, ButtonInput.SecondaryTouch, ButtonInput.StickTouch, ButtonInput.TriggerTouch }),
                // The Frame's left face buttons are a D-pad: no A/B/X/Y touch there.
                ["steam_frame.toml"] = (new[] { ButtonInput.StickTouch, ButtonInput.TriggerTouch },
                                        new[] { ButtonInput.PrimaryTouch, ButtonInput.SecondaryTouch, ButtonInput.StickTouch, ButtonInput.TriggerTouch }),
            };
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                Assert.Equal(expected.Keys.OrderBy(k => k), controls.Families().Select(f => f.FileName).OrderBy(k => k));
                foreach (var family in controls.Families())
                {
                    var edit = ControlsEdit.Open(controls, family);
                    var rows = edit.Inputs(Handedness.Right);
                    var want = expected[family.FileName];
                    Assert.Equal(want.Left, rows.Where(r => r.Hand == ControlHand.Left && TouchSensors.IsControlTouch(r.Input)).Select(r => r.Input));
                    Assert.Equal(want.Right, rows.Where(r => r.Hand == ControlHand.Right && TouchSensors.IsControlTouch(r.Input)).Select(r => r.Input));
                }
            }
        }

        [Fact]
        public void TouchRowsAreNamedForTheControlTouched()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var touch = Open(controls, "oculus_touch.toml");
                Assert.Equal("Right A touch", touch.InputName(ControlHand.Right, ButtonInput.PrimaryTouch));
                Assert.Equal("Left Y touch", touch.InputName(ControlHand.Left, ButtonInput.SecondaryTouch));
                Assert.Equal("Left stick touch", touch.InputName(ControlHand.Left, ButtonInput.StickTouch));
                Assert.Equal("Right trigger touch", touch.InputName(ControlHand.Right, ButtonInput.TriggerTouch));
                Assert.Equal("Right trackpad touch", Open(controls, "valve_index.toml").InputName(ControlHand.Right, ButtonInput.TrackpadTouch));
                Assert.Equal("Left thumb rest", touch.InputName(ControlHand.Left, ButtonInput.ThumbRest));
                // Not on these controllers: the generic name.
                Assert.Equal("Right trackpad touch", touch.InputName(ControlHand.Right, ButtonInput.TrackpadTouch));
                Assert.False(touch.HasInput(ControlHand.Right, ButtonInput.TrackpadTouch));
            }
        }

        [Fact]
        public void NoBuiltInMapBindsATouch()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                foreach (var family in controls.Families())
                {
                    var edit = ControlsEdit.Open(controls, family);
                    foreach (Handedness hand in new[] { Handedness.Right, Handedness.Left, Handedness.LeftMirrored })
                        foreach (var h in new[] { ControlHand.Left, ControlHand.Right })
                            foreach (var input in Touches)
                                Assert.False(edit.Binds(hand, h, input), family.FileName + " " + hand + " " + h + " " + input);
                }
            }
        }

        [Fact]
        public void ABoundTouchSavesInTheLayersOrderAndReadsBack()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var edit = Open(controls, "oculus_touch.toml");
                var key = BindingKey.Button(ControlHand.Right, ButtonInput.PrimaryTouch, PressKind.Tap);
                edit.SetAction(Handedness.Right, key, "weapon_slot_2");
                Assert.Empty(edit.Check());
                Assert.Equal(ControlsSaveResult.Saved, edit.Save());
                var lines = File.ReadAllText(edit.PlayerFile).Split('\n').ToList();
                int touch = lines.IndexOf("\"right.primary_touch.tap\" = \"weapon_slot_2\"");
                Assert.True(touch > lines.IndexOf("\"right.secondary.press\" = \"dash\""));
                // The turn stick's gestures stay after the hand's buttons, as the layer writes them.
                Assert.True(touch < lines.IndexOf("\"right.stick.up\" = \"chainsaw\""));
                Assert.Equal("weapon_slot_2", Open(controls, "oculus_touch.toml").Get(Handedness.Right, key));
                // The [profile] section is left as it is: the layer adds the touch's binding for the session.
                Assert.DoesNotContain(lines, l => l.Contains("primary_touch\" = \"/input"));
            }
        }

        [Fact]
        public void TouchBindingsFollowTheLayersRules()
        {
            var map = new Dictionary<string, string>
            {
                ["right.trigger_touch.tap"] = "quick_switch",
                ["right.trigger_touch.hold"] = "automap",
            };
            Assert.Empty(ControlMapRules.Check(map));
            map["right.trigger_touch.press"] = "dash";
            var issues = ControlMapRules.Check(map);
            Assert.Equal(new[] { "right.trigger_touch.hold", "right.trigger_touch.tap" }, issues.Select(i => i.Key));
            Assert.All(issues, i => Assert.Equal(ControlConflict.PressWithTapOrHold, i.Conflict));
        }

        [Fact]
        public void AFaceButtonTouchThatIsTheWheelsRestGetsTheNote()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var index = Open(controls, "valve_index.toml");
                index.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Right, ButtonInput.PrimaryTouch, PressKind.Tap), "quick_switch");
                // Face buttons as thumb rest on: the right hand's rest is A/B touched, now a button.
                Assert.Equal(new[] { ControlHand.Right }, index.BoundRests(Handedness.Right, true));
                var note = index.RestWheelNote(ThumbRestMode.Edge, Handedness.Right, true);
                Assert.Contains("right face-button touch is a button", note);
                Assert.Contains("left rest only", note);
                // Face buttons as thumb rest off: no rest on Index, no note.
                Assert.Empty(index.BoundRests(Handedness.Right));
                Assert.Null(index.RestWheelNote(ThumbRestMode.Edge, Handedness.Right));
                Assert.Null(index.RestWheelNote(ThumbRestMode.Edge, Handedness.Right, false));
                // Both hands' face touch bound: the wheel is off.
                index.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Left, ButtonInput.SecondaryTouch, PressKind.Hold), "automap");
                Assert.Contains("both rests are buttons", index.RestWheelNote(ThumbRestMode.Full, Handedness.Right, true));
                Assert.Empty(index.Check());

                // On Touch the thumb rest is the rest: a face button's touch bound is no rest of the wheel's.
                var touch = Open(controls, "oculus_touch.toml");
                touch.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Right, ButtonInput.PrimaryTouch, PressKind.Tap), "quick_switch");
                Assert.Empty(touch.BoundRests(Handedness.Right, true));
                Assert.Null(touch.RestWheelNote(ThumbRestMode.Edge, Handedness.Right, true));

                // The Steam Frame's bumper holds the wheel: its face touches are never the rest.
                var frame = Open(controls, "steam_frame.toml");
                frame.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Right, ButtonInput.PrimaryTouch, PressKind.Tap), "quick_switch");
                Assert.Empty(frame.BoundRests(Handedness.Right, true));
            }
        }

        /// <summary>
        /// Every gameplay action of the layer's (xr_action_set.cpp) is a <c>[profile]</c> key the editor accepts, as the layer
        /// does: a file that names one is not refused on save.
        /// </summary>
        [Fact]
        public void EveryGameplayActionOfTheLayersIsAProfileKey()
        {
            var src = File.ReadAllText(RepoFile("src", "features", "input", "xr_action_set.cpp"));
            var names = Regex.Matches(src, @"XrActionId::\w+, XrActionSetId::Gameplay, ""([a-z0-9_]+)""")
                .Cast<Match>().Select(m => m.Groups[1].Value).ToList();
            Assert.Contains("trigger_touch", names);
            Assert.Equal(names.OrderBy(n => n, StringComparer.Ordinal), ControllerMapFile.GameplayActions.OrderBy(n => n, StringComparer.Ordinal));
            var text = "[profile]\n\"path\" = \"/interaction_profiles/oculus/touch_controller\"\n"
                + string.Concat(names.Select(n => "\"gameplay.right." + n + "\" = \"/input/" + n + "/touch\"\n"))
                + "\n[map.right]\n\"right.trigger.press\" = \"fire\"\n";
            Assert.Empty(ControllerMapFile.Parse(text).FileIssues);
        }

        /// <summary>
        /// The touch sensors listed here against the layer's interaction_profiles.cpp: the same profiles, and on each hand the
        /// same identifiers with a <c>/touch</c> input.
        /// </summary>
        [Fact]
        public void TheTouchSensorsMatchTheLayersProfiles()
        {
            var src = File.ReadAllText(RepoFile("src", "features", "input", "interaction_profiles.cpp"));
            var arrays = Regex.Matches(src, @"constexpr std::array<std::string_view, \d+> (k\w+)\{(.*?)\};", RegexOptions.Singleline)
                .Cast<Match>()
                .ToDictionary(m => m.Groups[1].Value,
                              m => Regex.Matches(m.Groups[2].Value, @"""/input/([a-z_]+)/touch""").Cast<Match>().Select(t => t.Groups[1].Value).ToList());
            var constants = Regex.Matches(src, @"constexpr std::string_view (k\w+) = ""([^""]+)"";")
                .Cast<Match>().ToDictionary(m => m.Groups[1].Value, m => m.Groups[2].Value);
            var table = src.Substring(src.IndexOf("kProfiles{{", StringComparison.Ordinal));
            var profiles = Regex.Matches(table, @"\{(""[^""]+""|k\w+), (k\w+), (k\w+),")
                .Cast<Match>()
                .Select(m => (Path: m.Groups[1].Value.StartsWith("\"", StringComparison.Ordinal) ? m.Groups[1].Value.Trim('"') : constants[m.Groups[1].Value],
                              Left: arrays[m.Groups[2].Value], Right: arrays[m.Groups[3].Value]))
                .ToList();
            Assert.Equal(8, profiles.Count);
            Assert.Equal(profiles.Select(p => p.Path).OrderBy(p => p, StringComparer.Ordinal), TouchSensors.ProfilePaths.OrderBy(p => p, StringComparer.Ordinal));
            var every = arrays.Values.SelectMany(a => a).Distinct().ToList();
            Assert.Contains("thumbrest", every);
            foreach (var p in profiles)
                foreach (var (hand, touches) in new[] { (ControlHand.Left, p.Left), (ControlHand.Right, p.Right) })
                    foreach (var identifier in every)
                        Assert.True(touches.Contains(identifier) == TouchSensors.Has(p.Path, hand, identifier), p.Path + " " + hand + " " + identifier);
        }

        private static string RepoFile(params string[] parts)
        {
            for (var dir = new DirectoryInfo(System.AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                var path = Path.Combine(new[] { dir.FullName }.Concat(parts).ToArray());
                if (File.Exists(path)) return path;
            }
            throw new FileNotFoundException("not found above the test folder: " + string.Join("/", parts));
        }
    }
}
