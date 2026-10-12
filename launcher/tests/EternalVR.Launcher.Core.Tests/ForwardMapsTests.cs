using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Controls;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Controls files written by a newer version (T-127): a key this version does not know but a later one may write is
    /// left out by the game with the rest used, so the editor keeps the line as it is, shows it as not used, and still
    /// saves; anything else wrong still stops a save. The cases are the layer's own (forward_maps_tests.cpp).
    /// </summary>
    public class ForwardMapsTests
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

        /// <summary>
        /// One map line and whether it is a later version's, which the game leaves out and uses the rest (<c>Skipped</c>);
        /// <c>Alone</c>: the line is all the map has, so the game would not use the file either way. The layer's kMapCases.
        /// </summary>
        private static readonly (string Key, string Value, bool Skipped, bool Alone)[] MapCases =
        {
            ("left.futurebutton.tap", "weapon_slot_3", true, false),
            ("right.trigger.double_tap", "fire", true, false),
            ("right.stick.up_left", "chainsaw", true, false),
            ("left.trigger.hold.seconds", "0.4", true, false),
            ("right.futurepad.press", "future_action", true, false),
            ("left.trigger.squeeze", "fire", true, false),
            ("weapon_hands", "right", false, false),
            ("left.trigger", "fire", false, false),
            ("Left.trigger.press", "fire", false, false),
            ("left.trigger-touch.press", "fire", false, false),
            ("both.trigger.press", "fire", false, false),
            ("wheel.mode", "edge", false, false),
            ("right.grip.press", "future_action", false, false),
            ("right.stick.down_tap", "future_action", false, false),
            ("weapon_hand", "middle", false, false),
            ("left.stick.role", "dpad", false, false),
            ("left.futurebutton.tap", "weapon_slot_3", true, true),
        };

        /// <summary>The known bindings beside a case's line unless it is alone (the layer's kKnownLines).</summary>
        private static readonly (string Key, string Value)[] KnownLines =
            { ("right.stick.role", "turn"), ("right.trigger.press", "fire"), ("left.thumbrest.tap", "weapon_slot_2") };

        private static Dictionary<string, string> MapOf((string Key, string Value, bool Skipped, bool Alone) c)
        {
            var map = c.Alone ? new Dictionary<string, string>() : KnownLines.ToDictionary(l => l.Key, l => l.Value);
            map[c.Key] = c.Value;
            return map;
        }

        [Fact]
        public void TheCasesAreTheLayers()
        {
            var cpp = File.ReadAllText(RepoFile("tests", "features", "input", "forward_maps_tests.cpp"));
            var layer = Regex.Matches(cpp, "MapCase\\{\"([^\"]*)\", \"([^\"]*)\", (true|false)(?:, (true|false))?\\}").Cast<Match>()
                .Select(m => (m.Groups[1].Value, m.Groups[2].Value, m.Groups[3].Value == "true", m.Groups[4].Value == "true")).ToList();
            foreach (var l in KnownLines)
                Assert.Contains("\\\"" + l.Key + "\\\" = \\\"" + l.Value + "\\\"\\n", cpp);
            Assert.Equal(MapCases.Length, layer.Count);
            Assert.Equal(MapCases, layer);
        }

        [Fact]
        public void ControlMapRulesMarkOnlyAnUnknownKeyOfTheLaterShapeAsSkipped()
        {
            foreach (var c in MapCases)
            {
                var issues = ControlMapRules.Check(MapOf(c), "map.right");
                bool emptied = c.Skipped && c.Alone;
                Assert.True(issues.Count == (emptied ? 2 : 1), c.Key + (c.Alone ? " alone" : ""));
                var issue = issues[0];
                Assert.True(c.Skipped == issue.Skipped, c.Key);
                Assert.Equal(c.Key, issue.Key);
                if (c.Skipped) Assert.Equal(ControlIssueKind.UnknownKey, issue.Kind);
                if (!emptied) continue;
                // Nothing known is left: an issue of its own, which stops a save as the layer refuses the file.
                Assert.False(issues[1].Skipped);
                Assert.Equal(ControlIssueKind.UnknownKey, issues[1].Kind);
                Assert.Null(issues[1].Key);
                Assert.Equal("map.right", issues[1].Section);
            }
        }

        [Fact]
        public void LaterKeysAreAHandAndTwoOrMoreLowercaseNames()
        {
            foreach (var key in new[] { "left.trigger.press", "left.futurebutton.tap", "right.stick.up_left", "left.trigger.hold.seconds", "right.face5.tap", "left.a.b" })
                Assert.True(BindingKey.IsLaterKey(key), key);
            foreach (var key in new[] { null, "", "weapon_hand", "weapon_hands", "left", "left.trigger", "Left.trigger.press", "left.Trigger.press", "both.trigger.press",
                         "left..press", "left.trigger.", "left.trigger-touch.press", "left.trigger press.tap", "wheel.mode.edge", "trigger.left.press" })
                Assert.False(BindingKey.IsLaterKey(key), key ?? "null");
        }

        private const string Header = "[profile]\n\"path\" = \"/interaction_profiles/oculus/touch_controller\"\n";

        [Fact]
        public void ALaterProfileInputAndLabelNameAreSkippedAndOtherShapesAreNot()
        {
            var later = ControllerMapFile.Parse(Header + "\"gameplay.left.trigger_force\" = \"/input/trigger/force\"\n[labels]\n\"left.trigger_force\" = \"Force\"\n");
            Assert.Equal(2, later.FileIssues.Count);
            Assert.All(later.FileIssues, i => Assert.True(i.Skipped));
            Assert.Equal(3, later.FileIssues[0].Line);
            Assert.Equal("gameplay.left.trigger_force", later.FileIssues[0].Key);
            Assert.Equal("left.trigger_force", later.FileIssues[1].Key);

            foreach (var line in new[] { "\"gameplay.middle.trigger\" = \"/input/trigger/value\"", "\"gameplay.left.Trigger_force\" = \"/input/trigger/force\"",
                         "\"futureset.left.trigger\" = \"/input/trigger/value\"", "\"gameplay.left.primary\" = \"x/click\"" })
            {
                var issue = Assert.Single(ControllerMapFile.Parse(Header + line + "\n").FileIssues);
                Assert.False(issue.Skipped, line);
            }
            foreach (var line in new[] { "\"middle.primary\" = \"X\"", "\"left.primary.x\" = \"X\"", "\"left.Trigger_force\" = \"F\"", "\"left.primary\" = \" \"" })
            {
                var issue = Assert.Single(ControllerMapFile.Parse(Header + "[labels]\n" + line + "\n").FileIssues);
                Assert.False(issue.Skipped, line);
            }
        }

        /// <summary>The built-in Touch file with a later version's lines in it: a map key, a <c>[profile]</c> input and a label.</summary>
        private static string WithLaterLines(string builtIn, string newline)
        {
            var text = builtIn.Replace("\r\n", "\n");
            text = ReplaceFirst(text, "\"gameplay.right.thumbrest\" = \"/input/thumbrest/touch\"\n",
                "\"gameplay.right.thumbrest\" = \"/input/thumbrest/touch\"\n\"gameplay.left.trigger_force\" = \"/input/trigger/force\"   # newer\n");
            text = ReplaceFirst(text, "\"right.stick.down_hold\" = \"weapon_wheel\"\n",
                "\"right.stick.down_hold\" = \"weapon_wheel\"\n'left.futurebutton.tap' = 'weapon_slot_3'\n\"right.stick.up_left\" = \"chainsaw\"  # a gesture of a later version\n");
            text += "\n[labels]\n\"left.trigger_force\" = \"Force\"\n";
            return text.Replace("\n", newline);
        }

        [Theory]
        [InlineData("\n")]
        [InlineData("\r\n")]
        public void AFileWithLaterLinesOpensSavesAndKeepsThoseLinesByteForByte(string newline)
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var original = WithLaterLines(File.ReadAllText(Path.Combine(controls.DefaultsDir, "oculus_touch.toml")), newline);
                Assert.Contains("'left.futurebutton.tap' = 'weapon_slot_3'", original);
                var path = t.Write("controls/mine.toml", original);
                var edit = Open(controls, "oculus_touch.toml");
                Assert.Equal(path, edit.PlayerFile);
                Assert.Empty(edit.Problems());
                var notUsed = edit.NotUsed();
                Assert.Equal(new[] { "gameplay.left.trigger_force", "left.trigger_force", "left.futurebutton.tap", "right.stick.up_left" }, notUsed.Select(i => i.Key));
                Assert.All(notUsed, i => Assert.Equal(ControlIssueKind.UnknownKey, i.Kind));
                Assert.Equal("map.right", notUsed[2].Section);

                // Unchanged: written back as it was.
                Assert.False(edit.IsModified);
                Assert.Equal(ControlsSaveResult.Saved, edit.Save());
                Assert.Equal(original, Encoding.UTF8.GetString(File.ReadAllBytes(path)));

                // One binding changed: only its line changes, the later version's lines stay as they were.
                var rightTrigger = BindingKey.Button(ControlHand.Right, ButtonInput.Trigger, PressKind.Press);
                edit.SetAction(Handedness.Right, rightTrigger, "dash");
                edit.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Left, ButtonInput.ThumbRest, PressKind.Tap), "weapon_slot_2");
                Assert.Empty(edit.Problems());
                Assert.Equal(ControlsSaveResult.Saved, edit.Save());
                var saved = Encoding.UTF8.GetString(File.ReadAllBytes(path));
                var expected = ReplaceFirst(original, "\"right.trigger.press\" = \"fire\"", "\"right.trigger.press\" = \"dash\"");
                expected = ReplaceFirst(expected, "\"left.menu.tap\" = \"pause\"" + newline, "\"left.menu.tap\" = \"pause\"" + newline + "\"left.thumbrest.tap\" = \"weapon_slot_2\"" + newline);
                Assert.NotEqual(original, expected);
                Assert.Equal(expected, saved);
                foreach (var later in new[] { "\"gameplay.left.trigger_force\" = \"/input/trigger/force\"   # newer", "'left.futurebutton.tap' = 'weapon_slot_3'",
                             "\"right.stick.up_left\" = \"chainsaw\"  # a gesture of a later version", "\"left.trigger_force\" = \"Force\"" })
                    Assert.Single(saved.Split(new[] { newline }, StringSplitOptions.None), l => l == later);

                // Read back: the same lines still not used, nothing else.
                var reopened = Open(controls, "oculus_touch.toml");
                Assert.Empty(reopened.Problems());
                Assert.Equal(4, reopened.NotUsed().Count);
                Assert.Equal("dash", reopened.Get(Handedness.Right, rightTrigger));
            }
        }

        [Fact]
        public void ALaterLineBesideARealProblemStillStopsTheSave()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var original = WithLaterLines(File.ReadAllText(Path.Combine(controls.DefaultsDir, "oculus_touch.toml")), "\n");
                var path = t.Write("controls/mine.toml", ReplaceFirst(original, "\"right.grip.press\" = \"weapon_mod\"", "\"right.grip.press\" = \"future_action\""));
                var edit = Open(controls, "oculus_touch.toml");
                var problem = Assert.Single(edit.Problems());
                Assert.Equal("right.grip.press", problem.Key);
                Assert.Equal(ControlIssueKind.UnknownValue, problem.Kind);
                Assert.Equal(4, edit.NotUsed().Count);
                var before = File.ReadAllText(path);
                Assert.Throws<InvalidOperationException>(() => edit.Save());
                Assert.Equal(before, File.ReadAllText(path));
            }
        }

        [Fact]
        public void AMapOfOnlyLaterLinesStopsTheSaveAndOneKnownBindingIsEnough()
        {
            var later = new Dictionary<string, string> { ["left.futurebutton.tap"] = "weapon_slot_3", ["right.trigger.double_tap"] = "fire", ["right.stick.up_left"] = "chainsaw" };
            var issues = ControlMapRules.Check(later, "map.right");
            Assert.Equal(4, issues.Count);
            Assert.Contains("bind at least one control", Assert.Single(issues, i => !i.Skipped).Message);
            later["right.grip.press"] = ControlNames.Unbound;
            Assert.All(ControlMapRules.Check(later, "map.right"), i => Assert.True(i.Skipped));
            Assert.Empty(ControlMapRules.Check(new Dictionary<string, string>(), "map.right"));

            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var path = t.Write("controls/mine.toml", Header + "[map.right]\n\"left.futurebutton.tap\" = \"weapon_slot_3\"\n");
                var edit = Open(controls, "oculus_touch.toml");
                Assert.Equal(path, edit.PlayerFile);
                Assert.Equal("map.right", Assert.Single(edit.Problems()).Section);
                Assert.Single(edit.NotUsed());
                var before = File.ReadAllText(path);
                Assert.Throws<InvalidOperationException>(() => edit.Save());
                Assert.Equal(before, File.ReadAllText(path));
            }
        }

        [Fact]
        public void TheBuiltInFilesHaveNothingSkipped()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                foreach (var family in controls.Families())
                    Assert.Empty(ControlsEdit.Open(controls, family).Check());
            }
        }

        private static string ReplaceFirst(string text, string what, string with)
        {
            int at = text.IndexOf(what, StringComparison.Ordinal);
            Assert.True(at >= 0, what);
            return text.Substring(0, at) + with + text.Substring(at + what.Length);
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
