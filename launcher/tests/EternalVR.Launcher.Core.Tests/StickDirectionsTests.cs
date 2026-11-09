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
    /// The turn stick's directions (T-229): down, the sides and the diagonals as bindable gestures, read, checked and saved as
    /// the layer reads them, and the editor's note when they split the stick.
    /// </summary>
    public class StickDirectionsTests
    {
        private static string ShippedMaps => Path.Combine(TestData.Dir, "controllers");

        private static ControlsEdit OpenTouch(TempDir t)
        {
            var controls = new ControlsFolder(t.Combine("controls"));
            controls.Prepare(ShippedMaps);
            return ControlsEdit.Open(controls, controls.Families().Single(f => f.FileName == "oculus_touch.toml"));
        }

        private static Dictionary<string, string> Map(params string[] pairs)
        {
            var map = new Dictionary<string, string>(StringComparer.Ordinal);
            for (int i = 0; i < pairs.Length; i += 2) map[pairs[i]] = pairs[i + 1];
            return map;
        }

        private static BindingKey Right(StickGesture gesture) => BindingKey.StickGestureOf(ControlHand.Right, gesture);

        /// <summary>The weapon slots where the game's wheel shows them (docs/release/CONTROLS.md's example).</summary>
        private static readonly (StickGesture Gesture, string Action)[] EightSlots =
        {
            (StickGesture.Up, "weapon_slot_1"), (StickGesture.UpRight, "weapon_slot_5"), (StickGesture.Right, "weapon_slot_2"),
            (StickGesture.DownRight, "weapon_slot_7"), (StickGesture.Down, "weapon_slot_3"), (StickGesture.DownLeft, "weapon_slot_6"),
            (StickGesture.Left, "weapon_slot_4"), (StickGesture.UpLeft, "weapon_slot_8"),
        };

        [Fact]
        public void EveryGestureReadsAndWritesAsTheLayersDo()
        {
            foreach (StickGesture gesture in Enum.GetValues(typeof(StickGesture)))
                foreach (var hand in new[] { ControlHand.Left, ControlHand.Right })
                {
                    var key = BindingKey.StickGestureOf(hand, gesture);
                    Assert.True(BindingKey.TryParse(key.ToString(), out var parsed));
                    Assert.Equal(key, parsed);
                    Assert.Equal(gesture, parsed.Gesture);
                }
            Assert.Equal("right.stick.up_left", Right(StickGesture.UpLeft).ToString());
            Assert.Equal("right.stick.down", Right(StickGesture.Down).ToString());
            // Ranked after the hand's buttons in the gestures' order, and before the other hand, as the layer writes them.
            var ranks = Enum.GetValues(typeof(StickGesture)).Cast<StickGesture>().Select(g => BindingKey.StickGestureOf(ControlHand.Left, g).Rank).ToList();
            Assert.Equal(ranks.OrderBy(r => r), ranks);
            Assert.True(ranks.Last() < BindingKey.Role(ControlHand.Right).Rank);
            Assert.True(ranks.First() > BindingKey.Button(ControlHand.Left, ButtonInput.TriggerTouch, PressKind.Hold).Rank);
        }

        /// <summary>The launcher's gesture names are the layer's, in its order (binding_keys.cpp kGestureNames).</summary>
        [Fact]
        public void TheGestureNamesAreTheLayersInItsOrder()
        {
            var keys = File.ReadAllText(RepoFile("src", "features", "input", "binding_keys.cpp"));
            var names = Regex.Matches(keys, "\\{StickGesture::\\w+, \"([a-z_]+)\"\\}").Cast<Match>().Select(m => m.Groups[1].Value);
            Assert.Equal(names, Enum.GetValues(typeof(StickGesture)).Cast<StickGesture>().Select(g => Right(g).ToString().Substring("right.stick.".Length)));
            var profile = File.ReadAllText(RepoFile("src", "features", "input", "binding_profile.hpp"));
            var order = Regex.Match(profile, "enum class StickGesture[^{]*\\{([^}]*)\\}").Groups[1].Value;
            var layer = Regex.Matches(order, "^\\s*([A-Za-z]+),", RegexOptions.Multiline).Cast<Match>().Select(m => m.Groups[1].Value);
            Assert.Equal(layer, Enum.GetNames(typeof(StickGesture)));
        }

        [Fact]
        public void GesturesHaveReadableNames()
        {
            Assert.Equal("up left", ControlNames.GestureLabel(StickGesture.UpLeft));
            Assert.Equal("down right", ControlNames.GestureLabel(StickGesture.DownRight));
            Assert.Equal("down", ControlNames.GestureLabel(StickGesture.Down));
            Assert.Equal("down, tap", ControlNames.GestureLabel(StickGesture.DownTap));
            Assert.Equal(Enum.GetValues(typeof(StickGesture)).Length,
                Enum.GetValues(typeof(StickGesture)).Cast<StickGesture>().Select(ControlNames.GestureLabel).Distinct().Count());
        }

        [Fact]
        public void TheEightDirectionsAreAValidMap()
        {
            var map = Map("right.stick.role", "turn", "left.stick.role", "move");
            foreach (var (gesture, action) in EightSlots) map[Right(gesture).ToString()] = action;
            Assert.Empty(ControlMapRules.Check(map));
        }

        [Fact]
        public void DownBesideADownTapOrHoldIsAConflictAndDownIsKept()
        {
            var issues = ControlMapRules.Check(Map("right.stick.role", "turn", "right.stick.down", "weapon_slot_3",
                "right.stick.down_tap", "quick_switch", "right.stick.down_hold", "weapon_wheel"));
            Assert.Equal(new[] { "right.stick.down_hold", "right.stick.down_tap" }, issues.Select(i => i.Key));
            foreach (var issue in issues)
            {
                Assert.Equal(ControlConflict.PressWithTapOrHold, issue.Conflict);
                Assert.Equal("right.stick.down", issue.OtherKey);
                Assert.Equal("weapon_slot_3", issue.OtherValue);
                Assert.Contains("cannot share the stick's down", issue.Message);
            }
            Assert.StartsWith("Right stick: Weapon slot 3 (down) and Quick switch (last weapon) (down, tap) cannot share", issues[1].Message);
        }

        [Fact]
        public void ADirectionOffTheTurnStickIsAConflict()
        {
            var issue = Assert.Single(ControlMapRules.Check(Map("left.stick.role", "move", "right.stick.role", "turn", "left.stick.up_left", "weapon_slot_8")));
            Assert.Equal(ControlConflict.GestureOffTurnStick, issue.Conflict);
            Assert.Equal("left.stick.up_left", issue.Key);
            Assert.Contains("Left stick, up left", issue.Message);
        }

        [Fact]
        public void TheEditorBindsTheDirectionsAndSavesThem()
        {
            using (var t = new TempDir())
            {
                var edit = OpenTouch(t);
                foreach (var (gesture, action) in EightSlots) edit.SetAction(Handedness.Right, Right(gesture), action);
                // Down beside the built-in down tap and hold: the editor shows the clash and does not save.
                Assert.Equal(2, edit.Problems().Count(i => i.Conflict == ControlConflict.PressWithTapOrHold));
                Assert.Throws<InvalidOperationException>(() => edit.Save());
                edit.SetAction(Handedness.Right, Right(StickGesture.DownTap), null);
                edit.SetAction(Handedness.Right, Right(StickGesture.DownHold), ControlNames.Unbound);
                Assert.Empty(edit.Check());
                Assert.Equal(ControlsSaveResult.Saved, edit.Save());

                var text = File.ReadAllText(edit.PlayerFile);
                Assert.Contains("\"right.stick.up_left\" = \"weapon_slot_8\"", text);
                Assert.Contains("\"right.stick.down\" = \"weapon_slot_3\"", text);
                var reopened = ControlsEdit.Open(new ControlsFolder(t.Combine("controls")),
                    new ControlsFolder(t.Combine("controls")).Families().Single(f => f.FileName == "oculus_touch.toml"));
                foreach (var (gesture, action) in EightSlots) Assert.Equal(action, reopened.Get(Handedness.Right, Right(gesture)));
                // The right-handed map only: the left-handed maps keep their own down tap and hold.
                Assert.Null(reopened.Get(Handedness.Right, Right(StickGesture.DownTap)));
                Assert.Equal("quick_switch", reopened.Get(Handedness.Left, Right(StickGesture.DownTap)));
                Assert.Empty(reopened.Check());
            }
        }

        [Fact]
        public void TheNoteSaysWhereTheSplitStickStillTurns()
        {
            using (var t = new TempDir())
            {
                var edit = OpenTouch(t);
                Assert.Null(edit.StickDirectionsNote(Handedness.Right));
                // Down alone keeps the built-in claim cones: no split.
                edit.SetAction(Handedness.Right, Right(StickGesture.Down), "weapon_slot_3");
                Assert.Null(edit.StickDirectionsNote(Handedness.Right));

                edit.SetAction(Handedness.Right, Right(StickGesture.Left), "weapon_slot_4");
                Assert.Equal("The right stick is split by direction: each bound direction does its action while you push the stick that "
                    + "way, and the stick turns you right only: its left is bound.", edit.StickDirectionsNote(Handedness.Right));
                edit.SetAction(Handedness.Right, Right(StickGesture.Right), "weapon_slot_2");
                Assert.EndsWith("no longer turns you: its left and right are both bound.", edit.StickDirectionsNote(Handedness.Right));
                edit.SetAction(Handedness.Right, Right(StickGesture.Left), null);
                Assert.EndsWith("turns you left only: its right is bound.", edit.StickDirectionsNote(Handedness.Right));
                edit.SetAction(Handedness.Right, Right(StickGesture.Right), null);
                edit.SetAction(Handedness.Right, Right(StickGesture.UpRight), "weapon_slot_5");
                Assert.EndsWith("turns you only when pushed close to straight left or right.", edit.StickDirectionsNote(Handedness.Right));
                edit.SetAction(Handedness.Right, Right(StickGesture.UpRight), null);
                Assert.Null(edit.StickDirectionsNote(Handedness.Right));
                // Each map has its own: the left-handed ones are untouched.
                edit.SetAction(Handedness.Right, Right(StickGesture.Left), "weapon_slot_4");
                Assert.Null(edit.StickDirectionsNote(Handedness.LeftMirrored));
            }
        }

        [Fact]
        public void TheDirectionsMoveWithTheTurnStickAsTheyAre()
        {
            using (var t = new TempDir())
            {
                var edit = OpenTouch(t);
                edit.SetAction(Handedness.Right, Right(StickGesture.Left), "weapon_slot_4");
                edit.SetAction(Handedness.Right, Right(StickGesture.UpRight), "weapon_slot_5");
                edit.SetStickRole(Handedness.Right, ControlHand.Left, StickRole.Turn);
                Assert.Equal(ControlHand.Left, edit.TurnStick(Handedness.Right));
                // Left stays left (no mirroring): the game's wheel shows the rocket launcher on the left whichever stick points.
                Assert.Equal("weapon_slot_4", edit.Get(Handedness.Right, BindingKey.StickGestureOf(ControlHand.Left, StickGesture.Left)));
                Assert.Equal("weapon_slot_5", edit.Get(Handedness.Right, BindingKey.StickGestureOf(ControlHand.Left, StickGesture.UpRight)));
                Assert.Null(edit.Get(Handedness.Right, Right(StickGesture.Left)));
                Assert.Empty(edit.Check());
                Assert.StartsWith("The left stick is split by direction", edit.StickDirectionsNote(Handedness.Right));
            }
        }

        private static string RepoFile(params string[] parts)
        {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                var path = Path.Combine(new[] { dir.FullName }.Concat(parts).ToArray());
                if (File.Exists(path)) return path;
            }
            throw new FileNotFoundException("not found above the test folder: " + string.Join("/", parts));
        }
    }
}
