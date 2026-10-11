using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Controls;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>A thumb rest bound like a button in the controls editor (the layer's touch_button.hpp): Touch only, no rest
    /// bound by default, the layer's rules, and the note when the thumb-rest wheel is on.</summary>
    public class ThumbRestButtonTests
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

        private static readonly BindingKey LeftRestTap = BindingKey.Button(ControlHand.Left, ButtonInput.ThumbRest, PressKind.Tap);

        [Fact]
        public void ThumbRestKeysReadAndWriteAsTheLayersDo()
        {
            foreach (var text in new[] { "left.thumbrest.tap", "right.thumbrest.press", "left.thumbrest.hold" })
            {
                Assert.True(BindingKey.TryParse(text, out var key));
                Assert.Equal(ButtonInput.ThumbRest, key.Input);
                Assert.Equal(text, key.ToString());
            }
            Assert.Equal("left.thumbrest.tap", LeftRestTap.ToString());
            foreach (var text in new[] { "left.thumb_rest.tap", "left.thumbrest", "left.thumbrest.touch" })
                Assert.False(BindingKey.TryParse(text, out _));
        }

        [Fact]
        public void ThumbRestRowsShowOnTouchOnly()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var touch = Open(controls, "oculus_touch.toml");
                var inputs = touch.Inputs(Handedness.Right);
                Assert.Contains((ControlHand.Left, ButtonInput.ThumbRest), inputs);
                Assert.Contains((ControlHand.Right, ButtonInput.ThumbRest), inputs);
                Assert.Equal("Left thumb rest", touch.InputName(ControlHand.Left, ButtonInput.ThumbRest));
                foreach (var family in controls.Families().Where(f => f.FileName != "oculus_touch.toml"))
                {
                    var other = ControlsEdit.Open(controls, family);
                    Assert.DoesNotContain(other.Inputs(Handedness.Right), i => i.Input == ButtonInput.ThumbRest);
                }
            }
        }

        [Fact]
        public void AnOldTouchFileWithoutTheThumbRestLinesStillShowsTheRows()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                // Without its thumb rest lines, and without the left grip's, which does not fall back.
                var lines = File.ReadAllLines(Path.Combine(controls.DefaultsDir, "oculus_touch.toml"))
                    .Where(l => !l.Contains("thumbrest") && !l.StartsWith("\"gameplay.left.grip\"", System.StringComparison.Ordinal));
                t.Write("controls/mine.toml", string.Join("\n", lines) + "\n");
                var edit = Open(controls, "oculus_touch.toml");
                Assert.Equal(t.Combine("controls", "mine.toml"), edit.PlayerFile);
                Assert.True(edit.HasInput(ControlHand.Left, ButtonInput.ThumbRest));
                Assert.Equal("Right thumb rest", edit.InputName(ControlHand.Right, ButtonInput.ThumbRest));
                Assert.False(edit.HasInput(ControlHand.Left, ButtonInput.Grip));
            }
        }

        [Fact]
        public void NoBuiltInMapBindsAThumbRest()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                foreach (var family in controls.Families())
                {
                    var edit = ControlsEdit.Open(controls, family);
                    foreach (Handedness hand in new[] { Handedness.Right, Handedness.Left, Handedness.LeftMirrored })
                    {
                        Assert.Empty(edit.BoundRests(hand));
                        Assert.Null(edit.RestWheelNote(ThumbRestMode.Edge, hand));
                    }
                }
            }
        }

        [Fact]
        public void ABoundThumbRestSavesInTheLayersOrderAndReadsBack()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var edit = Open(controls, "oculus_touch.toml");
                edit.SetAction(Handedness.Right, LeftRestTap, "quick_switch");
                Assert.Empty(edit.Check());
                Assert.Equal(ControlsSaveResult.Saved, edit.Save());
                var lines = File.ReadAllText(edit.PlayerFile).Split('\n').ToList();
                int rest = lines.IndexOf("\"left.thumbrest.tap\" = \"quick_switch\"");
                Assert.Equal("\"left.menu.tap\" = \"pause\"", lines[rest - 1]);
                Assert.Equal("quick_switch", Open(controls, "oculus_touch.toml").Get(Handedness.Right, LeftRestTap));
            }
        }

        [Fact]
        public void ThumbRestBindingsFollowTheLayersRules()
        {
            var map = new System.Collections.Generic.Dictionary<string, string>
            {
                ["left.thumbrest.tap"] = "quick_switch",
                ["left.thumbrest.hold"] = "weapon_wheel",
            };
            Assert.Empty(ControlMapRules.Check(map));
            map["left.thumbrest.press"] = "automap";
            var issues = ControlMapRules.Check(map);
            Assert.Equal(new[] { "left.thumbrest.hold", "left.thumbrest.tap" }, issues.Select(i => i.Key));
            Assert.All(issues, i => Assert.Equal(ControlConflict.PressWithTapOrHold, i.Conflict));
            Assert.All(issues, i => Assert.Equal("left.thumbrest.press", i.OtherKey));
            Assert.Contains("Left thumb rest", issues[0].Message);
        }

        [Fact]
        public void TheNoteSaysWhenTheRestLeftCannotWorkTheWheel()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                // Right-handed: the right stick turns, the left one moves.
                var edit = Open(controls, "oculus_touch.toml");
                edit.SetAction(Handedness.Right, LeftRestTap, "quick_switch");
                // Turn stick picks gives turning back with the left rest: bound, the wheel is off.
                var extreme = edit.RestWheelNote(ThumbRestMode.Extreme, Handedness.Right);
                Assert.Contains("left thumb rest is a button", extreme);
                Assert.Contains("the wheel is off", extreme);
                Assert.DoesNotContain("rest only", extreme);

                // The turn hand's rest bound instead: the wheel still works with the left one.
                var turnHand = Open(controls, "oculus_touch.toml");
                turnHand.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Right, ButtonInput.ThumbRest, PressKind.Tap), "quick_switch");
                Assert.Contains("uses the left rest only", turnHand.RestWheelNote(ThumbRestMode.Extreme, Handedness.Right));

                // Touch, then push with the left rest bound and the left stick without a role: the right rest has nothing
                // to pick with.
                edit.SetStickRole(Handedness.Right, ControlHand.Left, StickRole.None);
                var edge = edit.RestWheelNote(ThumbRestMode.Edge, Handedness.Right);
                Assert.Contains("no stick to pick with", edge);
                Assert.Contains("the wheel is off", edge);

                // A wheel that could not work with these controls anyway gets no note: Turn stick picks with no turn stick.
                var noTurn = Open(controls, "oculus_touch.toml");
                noTurn.SetAction(Handedness.Right, LeftRestTap, "quick_switch");
                noTurn.SetStickRole(Handedness.Right, ControlHand.Right, StickRole.None);
                Assert.Null(noTurn.RestWheelNote(ThumbRestMode.Extreme, Handedness.Right));
                Assert.Empty(edit.Check());
            }
        }

        [Fact]
        public void TheEditorNotesABoundRestWhileTheWheelIsOn()
        {
            using (var t = new TempDir())
            {
                var edit = Open(Prepared(t), "oculus_touch.toml");
                edit.SetAction(Handedness.Right, LeftRestTap, "quick_switch");
                Assert.Equal(new[] { ControlHand.Left }, edit.BoundRests(Handedness.Right));
                Assert.Null(edit.RestWheelNote(ThumbRestMode.Off, Handedness.Right));
                Assert.Null(edit.RestWheelNote(ThumbRestMode.Edge, Handedness.Left)); // another weapon hand's map
                var note = edit.RestWheelNote(ThumbRestMode.Edge, Handedness.Right);
                Assert.Contains("left thumb rest is a button", note);
                Assert.Contains("right rest only", note);

                edit.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Right, ButtonInput.ThumbRest, PressKind.Hold), "automap");
                Assert.Contains("both thumb rests", edit.RestWheelNote(ThumbRestMode.Full, Handedness.Right));
                // The note never blocks saving.
                Assert.Empty(edit.Check());

                // "none" binds nothing.
                edit.SetAction(Handedness.Right, LeftRestTap, null);
                edit.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Right, ButtonInput.ThumbRest, PressKind.Hold), null);
                Assert.Empty(edit.BoundRests(Handedness.Right));
            }
        }
    }
}
