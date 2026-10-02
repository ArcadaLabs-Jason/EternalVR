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
    /// <summary>The controls editor's model: reading, checking and saving controller files the way the layer reads them.</summary>
    public class ControlsEditorTests
    {
        private static string ShippedMaps => Path.Combine(TestData.Dir, "controllers");

        public static IEnumerable<object[]> BuiltInMaps() =>
            Directory.GetFiles(ShippedMaps, "*.toml").Select(f => new object[] { Path.GetFileName(f) });

        private static ControlsFolder Prepared(TempDir t)
        {
            var controls = new ControlsFolder(t.Combine("controls"));
            controls.Prepare(ShippedMaps);
            return controls;
        }

        private static ControlsEdit Open(ControlsFolder controls, string fileName) =>
            ControlsEdit.Open(controls, controls.Families().Single(f => f.FileName == fileName));

        private static Dictionary<string, string> Map(params string[] pairs)
        {
            var map = new Dictionary<string, string>(StringComparer.Ordinal);
            for (int i = 0; i < pairs.Length; i += 2) map[pairs[i]] = pairs[i + 1];
            return map;
        }

        private static readonly BindingKey RightTrigger = BindingKey.Button(ControlHand.Right, ButtonInput.Trigger, PressKind.Press);
        private static readonly BindingKey LeftTrigger = BindingKey.Button(ControlHand.Left, ButtonInput.Trigger, PressKind.Press);

        // ---- Every built-in map ----

        [Fact]
        public void EveryBuiltInMapIsListed()
        {
            using (var t = new TempDir())
            {
                var families = Prepared(t).Families();
                Assert.Equal(new[] { "oculus_touch.toml", "valve_index.toml", "hp_reverb_g2.toml", "windows_mixed_reality.toml", "htc_vive_cosmos.toml", "htc_vive_wand.toml", "pico4.toml", "steam_frame.toml" },
                    families.Select(f => f.FileName));
                Assert.Equal("Meta Quest and Rift (Touch)", families[0].Name);
                Assert.Equal("/interaction_profiles/oculus/touch_controller", families[0].ProfilePath);
                Assert.Equal(families.Count, families.Select(f => f.ProfilePath).Distinct().Count());
            }
        }

        [Theory]
        [MemberData(nameof(BuiltInMaps))]
        public void ABuiltInMapReadsBackByteForByteAndPassesTheLayersChecks(string name)
        {
            var text = File.ReadAllText(Path.Combine(ShippedMaps, name));
            var file = ControllerMapFile.Parse(text);
            Assert.Equal(text, file.ToText());
            Assert.Empty(file.FileIssues);
            Assert.Equal(new[] { "profile", "map.right", "map.left_button_swap", "map.left_full_mirror" }, file.Sections);
            Assert.StartsWith("/interaction_profiles/", file.ProfilePath);
            foreach (var section in ControlNames.MapSections)
            {
                Assert.Empty(ControlMapRules.Check(file.Entries(section), section));
                Assert.NotEmpty(file.Entries(section));
            }
            using (var t = new TempDir())
                Assert.Empty(Open(Prepared(t), name).Check());
        }

        [Theory]
        [MemberData(nameof(BuiltInMaps))]
        public void EditingSavingAndReloadingABuiltInMapChangesOnlyTheEditedLines(string name)
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var defaultsBytes = File.ReadAllBytes(Path.Combine(controls.DefaultsDir, name));
                var edit = Open(controls, name);
                Assert.Null(edit.PlayerFile);
                Assert.False(edit.IsModified);
                edit.SetAction(Handedness.Right, RightTrigger, "jump");
                edit.SetAction(Handedness.Left, LeftTrigger, "dash");
                edit.SetAction(Handedness.LeftMirrored, LeftTrigger, "automap");
                Assert.True(edit.IsModified);
                Assert.Empty(edit.Check());

                Assert.Equal(ControlsSaveResult.Saved, edit.Save());
                var saved = Path.Combine(controls.Dir, name);
                Assert.Equal(saved, edit.PlayerFile);
                Assert.False(edit.IsModified);
                Assert.True(controls.HasPlayerMaps);
                // The defaults are never written.
                Assert.Equal(defaultsBytes, File.ReadAllBytes(Path.Combine(controls.DefaultsDir, name)));

                var before = File.ReadAllText(Path.Combine(controls.DefaultsDir, name)).Split('\n');
                var after = File.ReadAllText(saved).Split('\n');
                Assert.Equal(before.Length, after.Length);
                var changed = Enumerable.Range(0, before.Length).Where(i => before[i] != after[i]).Select(i => after[i]).ToList();
                Assert.Equal(new[] { "\"right.trigger.press\" = \"jump\"", "\"left.trigger.press\" = \"dash\"", "\"left.trigger.press\" = \"automap\"" }, changed);

                var reopened = Open(controls, name);
                Assert.Equal(saved, reopened.PlayerFile);
                Assert.Equal("jump", reopened.Get(Handedness.Right, RightTrigger));
                Assert.Equal("dash", reopened.Get(Handedness.Left, LeftTrigger));
                Assert.Equal("automap", reopened.Get(Handedness.LeftMirrored, LeftTrigger));
                Assert.Empty(reopened.Check());
                Assert.Equal(File.ReadAllText(saved), reopened.File.ToText());
            }
        }

        [Theory]
        [MemberData(nameof(BuiltInMaps))]
        public void ResettingToTheDefaultsAndSavingRemovesThePlayersFile(string name)
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var edit = Open(controls, name);
                edit.SetAction(Handedness.Right, RightTrigger, "melee");
                edit.Save();
                Assert.True(controls.HasPlayerMaps);

                var again = Open(controls, name);
                again.ResetToDefaults();
                Assert.True(again.IsModified);
                Assert.Equal(File.ReadAllText(Path.Combine(controls.DefaultsDir, name)), again.File.ToText());
                Assert.Equal(ControlsSaveResult.Removed, again.Save());
                Assert.Null(again.PlayerFile);
                Assert.False(controls.HasPlayerMaps);
                Assert.True(File.Exists(Path.Combine(controls.DefaultsDir, name)));
            }
        }

        [Fact]
        public void SavingTheBuiltInControlsUnchangedWritesNothing()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var edit = Open(controls, "pico4.toml");
                Assert.Equal(ControlsSaveResult.BuiltIn, edit.Save());
                Assert.False(controls.HasPlayerMaps);
                // Back to the built-in binding by hand: the same.
                edit.SetAction(Handedness.Right, RightTrigger, "jump");
                edit.SetAction(Handedness.Right, RightTrigger, "fire");
                Assert.Equal(ControlsSaveResult.BuiltIn, edit.Save());
                Assert.False(controls.HasPlayerMaps);
            }
        }

        [Fact]
        public void ControlsWithProblemsAreNotSaved()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var edit = Open(controls, "oculus_touch.toml");
                // A tap on A, which already jumps on press.
                edit.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Right, ButtonInput.Primary, PressKind.Tap), "dash");
                var issue = Assert.Single(edit.Check());
                Assert.Equal(ControlConflict.PressWithTapOrHold, issue.Conflict);
                Assert.Equal("map.right", issue.Section);
                Assert.Equal("right.primary.tap", issue.Key);
                Assert.Equal("right.primary.press", issue.OtherKey);
                // Names the button on this controller and both actions.
                Assert.Contains("Right A", issue.Message);
                Assert.Contains("Jump (press)", issue.Message);
                Assert.Contains("Dash (tap)", issue.Message);
                Assert.Throws<InvalidOperationException>(() => edit.Save());
                Assert.False(controls.HasPlayerMaps);
            }
        }

        // ---- The layer's rules ----

        [Fact]
        public void ATapAndAHoldMayShareAButtonButNotWithAPress()
        {
            Assert.Empty(ControlMapRules.Check(Map("left.primary.tap", "switch_equipment", "left.primary.hold", "dossier")));
            var issues = ControlMapRules.Check(Map("left.primary.press", "jump", "left.primary.tap", "switch_equipment", "left.primary.hold", "dossier"));
            Assert.Equal(new[] { "left.primary.hold", "left.primary.tap" }, issues.Select(i => i.Key));
            Assert.All(issues, i => Assert.Equal(ControlConflict.PressWithTapOrHold, i.Conflict));
            Assert.All(issues, i => Assert.Equal("left.primary.press", i.OtherKey));
            // Another hand's or another button's press is no conflict.
            Assert.Empty(ControlMapRules.Check(Map("right.primary.press", "jump", "left.primary.tap", "dash", "left.grip.press", "flame_belch")));
            // "none" frees a button and clashes with nothing.
            Assert.Empty(ControlMapRules.Check(Map("left.primary.press", "none", "left.primary.tap", "dash")));
        }

        [Fact]
        public void OneActionOnSeveralButtonsIsAllowed()
        {
            Assert.Empty(ControlMapRules.Check(Map("left.trigger.press", "fire", "right.trigger.press", "fire", "right.grip.hold", "fire")));
        }

        [Fact]
        public void UnknownKeysActionsHandsAndRolesAreReported()
        {
            var issues = ControlMapRules.Check(Map(
                "right.trigger.pres", "fire",
                "right.thumb.press", "fire",
                "right.grip.press", "fier",
                "weapon_hand", "middle",
                "left.stick.role", "walk",
                "right.stick.up", "chainsaws"));
            Assert.Equal(ControlIssueKind.UnknownKey, issues.Single(i => i.Key == "right.trigger.pres").Kind);
            Assert.Equal(ControlIssueKind.UnknownKey, issues.Single(i => i.Key == "right.thumb.press").Kind);
            Assert.Equal(ControlIssueKind.UnknownValue, issues.Single(i => i.Key == "right.grip.press").Kind);
            Assert.Contains("'fier' is not an action", issues.Single(i => i.Key == "right.grip.press").Message);
            Assert.Equal(ControlIssueKind.UnknownValue, issues.Single(i => i.Key == "weapon_hand").Kind);
            Assert.Equal(ControlIssueKind.UnknownValue, issues.Single(i => i.Key == "left.stick.role").Kind);
            Assert.Equal(ControlIssueKind.UnknownValue, issues.Single(i => i.Key == "right.stick.up").Kind);
            Assert.Equal(6, issues.Count);
        }

        [Fact]
        public void BothSticksWithOneRoleConflictAndTheLeftKeepsIt()
        {
            var issue = Assert.Single(ControlMapRules.Check(Map("left.stick.role", "turn", "right.stick.role", "turn", "left.stick.up", "chainsaw")));
            Assert.Equal(ControlConflict.SharedStickRole, issue.Conflict);
            Assert.Equal("right.stick.role", issue.Key);
            Assert.Equal("left.stick.role", issue.OtherKey);
            Assert.Empty(ControlMapRules.Check(Map("left.stick.role", "none", "right.stick.role", "none")));
        }

        [Fact]
        public void GesturesWorkOnlyOnTheTurnStick()
        {
            var issue = Assert.Single(ControlMapRules.Check(Map("left.stick.role", "turn", "right.stick.role", "move", "right.stick.down_tap", "quick_switch")));
            Assert.Equal(ControlConflict.GestureOffTurnStick, issue.Conflict);
            Assert.Equal("right.stick.down_tap", issue.Key);
            Assert.Equal("right.stick.role", issue.OtherKey);
            Assert.Equal("move", issue.OtherValue);
            // No role at all: the key the layer names is the missing role line.
            issue = Assert.Single(ControlMapRules.Check(Map("right.stick.up", "chainsaw")));
            Assert.Equal("right.stick.role", issue.OtherKey);
            Assert.Equal("none", issue.OtherValue);
        }

        [Fact]
        public void EveryActionIsAccepted()
        {
            foreach (var a in ControlNames.Actions)
                Assert.Empty(ControlMapRules.Check(Map("right.trigger.press", a.Name)));
            Assert.Equal(ControlNames.Actions.Count, ControlNames.Actions.Select(a => a.Label).Distinct().Count());
        }

        [Fact]
        public void KeysReadAndWriteAsTheLayersDo()
        {
            foreach (var text in new[] { "weapon_hand", "left.stick.role", "right.stick.down_hold", "left.stick_click.tap", "right.menu.hold" })
            {
                Assert.True(BindingKey.TryParse(text, out var key));
                Assert.Equal(text, key.ToString());
            }
            foreach (var text in new[] { "", "left", "left.trigger", "left.trigger.press.extra", "Left.trigger.press", "both.trigger.press", "left.stick.down", "left.stick.press" })
                Assert.False(BindingKey.TryParse(text, out _));
        }

        /// <summary>The launcher's names against the layer's source: every action and every key word, no more and no fewer.</summary>
        [Fact]
        public void TheNamesMatchTheLayersSource()
        {
            var src = RepoFile("src", "game", "eternal", "game_action.cpp");
            var names = Regex.Matches(File.ReadAllText(src), "return \"([a-z0-9_]+)\";").Cast<Match>().Select(m => m.Groups[1].Value).Where(n => n != "unknown");
            Assert.Equal(names, ControlNames.Actions.Select(a => a.Name));

            var keys = File.ReadAllText(RepoFile("src", "features", "input", "binding_keys.cpp"));
            foreach (var word in new[] { "trigger", "grip", "stick_click", "primary", "secondary", "menu", "press", "tap", "hold", "up", "down_tap", "down_hold", "none", "move", "turn" })
                Assert.Contains("\"" + word + "\"", keys);
            var sections = File.ReadAllText(RepoFile("src", "features", "input", "controller_bindings.cpp"));
            foreach (var section in ControlNames.MapSections)
                Assert.Contains("\"" + section.Substring(ControlNames.MapSectionPrefix.Length) + "\"", sections);
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

        // ---- The file ----

        [Fact]
        public void EditsKeepCommentsLineEndingsAndTheByteOrderMark()
        {
            var text = "﻿# My controls\r\n[profile]\r\n\"path\" = \"/interaction_profiles/oculus/touch_controller\"\r\n\r\n"
                + "[map.right]\r\n  \"right.trigger.press\" = \"fire\"   # shoot\r\nright.grip.press = 'weapon_mod'\r\n";
            var file = ControllerMapFile.Parse(text);
            Assert.Equal(text, file.ToText());
            Assert.Equal("weapon_mod", file.Get("map.right", "right.grip.press"));
            file.Set("map.right", "right.trigger.press", "jump");
            file.Set("map.right", "right.grip.press", "dash");
            Assert.Equal("﻿# My controls\r\n[profile]\r\n\"path\" = \"/interaction_profiles/oculus/touch_controller\"\r\n\r\n"
                + "[map.right]\r\n  \"right.trigger.press\" = \"jump\"   # shoot\r\nright.grip.press = \"dash\"\r\n", file.ToText());
        }

        [Fact]
        public void ALabelsSectionIsReadAndItsBadKeysAndEmptyNamesAreReported()
        {
            var header = "[profile]\n\"path\" = \"/interaction_profiles/oculus/touch_controller\"\n[labels]\n";
            var good = ControllerMapFile.Parse(header + "\"left.primary\" = \"X\"\n\"right.stick\" = \"Right Thumbstick\"\n");
            Assert.Empty(good.FileIssues);
            Assert.Equal("Right Thumbstick", good.Get(ControlNames.LabelsSection, "right.stick"));
            var bad = ControllerMapFile.Parse(header + "\"left.thumb\" = \"Thumb\"\n\"right.primary\" = \" \"\n");
            Assert.Equal(2, bad.FileIssues.Count);
            Assert.Contains(bad.FileIssues, i => i.Kind == ControlIssueKind.UnknownKey && i.Key == "left.thumb" && i.Line == 4);
            Assert.Contains(bad.FileIssues, i => i.Kind == ControlIssueKind.UnknownValue && i.Key == "right.primary");
        }

        [Fact]
        public void ANewBindingGoesInTheLayersOrderAndARemovedOneLeavesTheButtonFree()
        {
            var file = ControllerMapFile.Parse(File.ReadAllText(Path.Combine(ShippedMaps, "oculus_touch.toml")));
            file.Set("map.right", "right.menu.press", "automap");
            file.Set("map.right", "left.trigger.tap", "recenter");
            var lines = file.ToText().Split('\n').ToList();
            int menu = lines.IndexOf("\"right.menu.press\" = \"automap\"");
            Assert.Equal("\"right.secondary.press\" = \"dash\"", lines[menu - 1]);
            Assert.Equal("\"right.stick.up\" = \"chainsaw\"", lines[menu + 1]);
            int tap = lines.IndexOf("\"left.trigger.tap\" = \"recenter\"");
            Assert.Equal("\"left.trigger.press\" = \"equipment\"", lines[tap - 1]);
            // Only in its own section.
            Assert.Null(file.Get("map.left_button_swap", "right.menu.press"));

            file.Remove("map.right", "right.menu.press");
            Assert.Null(file.Get("map.right", "right.menu.press"));
            Assert.DoesNotContain("\"right.menu.press\" = \"automap\"", file.ToText());
        }

        [Fact]
        public void AMissingMapIsAddedAtTheEnd()
        {
            var file = ControllerMapFile.Parse("[profile]\n\"path\" = \"/p\"\n\n[map.right]\n\"right.trigger.press\" = \"fire\"\n");
            file.Set("map.left_full_mirror", "left.trigger.press", "fire");
            Assert.Equal("[profile]\n\"path\" = \"/p\"\n\n[map.right]\n\"right.trigger.press\" = \"fire\"\n\n[map.left_full_mirror]\n\"left.trigger.press\" = \"fire\"\n", file.ToText());
            Assert.Empty(file.FileIssues);
        }

        [Fact]
        public void SettingARepeatedKeyRemovesTheCopiesTheLayerIgnores()
        {
            var file = ControllerMapFile.Parse("[profile]\n\"path\" = \"/p\"\n[map.right]\n\"right.trigger.press\" = \"fire\"\n\"right.trigger.press\" = \"jump\"\n");
            var dup = Assert.Single(file.FileIssues);
            Assert.Equal(ControlIssueKind.DuplicateKey, dup.Kind);
            Assert.Equal(5, dup.Line);
            Assert.Equal("fire", file.Get("map.right", "right.trigger.press")); // the first wins
            file.Set("map.right", "right.trigger.press", "dash");
            Assert.Equal("[profile]\n\"path\" = \"/p\"\n[map.right]\n\"right.trigger.press\" = \"dash\"\n", file.ToText());
            Assert.Empty(file.FileIssues);
        }

        [Fact]
        public void FileProblemsAreFoundAsTheLayerFindsThem()
        {
            var file = ControllerMapFile.Parse(
                "\"orphan\" = \"x\"\n"
                + "[map.right]\n"
                + "\"right.trigger.press\" = fire\n"
                + "\"right.grip.press\" = \"weapon_mod\" extra\n"
                + "\"right.menu.press\" = \"pa\\use\"\n"
                + "[map.left]\n"
                + "[map.right\n"
                + "[map.right]\n");
            var issues = file.FileIssues;
            Assert.Contains(issues, i => i.Line == 1 && i.Message.Contains("under a [profile]"));
            Assert.Contains(issues, i => i.Line == 3 && i.Message.Contains("expected a quoted string"));
            Assert.Contains(issues, i => i.Line == 4 && i.Message.Contains("unexpected text"));
            Assert.Contains(issues, i => i.Line == 5 && i.Message.Contains("escape"));
            Assert.Contains(issues, i => i.Line == 7 && i.Message.Contains("malformed section header"));
            Assert.Contains(issues, i => i.Line == 8 && i.Kind == ControlIssueKind.DuplicateKey);
            Assert.Contains(issues, i => i.Key == "map.left" && i.Kind == ControlIssueKind.UnknownKey);
            Assert.Contains(issues, i => i.Message == "the file has no [profile] section");
            Assert.Equal(8, issues.Count);

            var noPath = ControllerMapFile.Parse("[profile]\n\"gameplay.left.trigger\" = \"/input/trigger/value\"\n\"gameplay.left.wing\" = \"/input/x\"\n\"menu.right.back\" = \"b\"\n");
            Assert.Equal(3, noPath.FileIssues.Count);
            Assert.Contains(noPath.FileIssues, i => i.Key == "path");
            Assert.Contains(noPath.FileIssues, i => i.Key == "gameplay.left.wing");
            Assert.Contains(noPath.FileIssues, i => i.Key == "menu.right.back" && i.Kind == ControlIssueKind.UnknownValue);
        }

        // ---- Sticks ----

        [Fact]
        public void TurningTheLeftStickIntoTheTurnStickSwapsTheRolesAndMovesTheGestures()
        {
            using (var t = new TempDir())
            {
                var edit = Open(Prepared(t), "oculus_touch.toml");
                Assert.Equal(ControlHand.Right, edit.TurnStick(Handedness.Right));
                edit.SetStickRole(Handedness.Right, ControlHand.Left, StickRole.Turn);
                Assert.Equal(StickRole.Turn, edit.Role(Handedness.Right, ControlHand.Left));
                Assert.Equal(StickRole.Move, edit.Role(Handedness.Right, ControlHand.Right));
                Assert.Equal("chainsaw", edit.Get(Handedness.Right, BindingKey.StickGestureOf(ControlHand.Left, StickGesture.Up)));
                Assert.Equal("weapon_wheel", edit.Get(Handedness.Right, BindingKey.StickGestureOf(ControlHand.Left, StickGesture.DownHold)));
                Assert.Null(edit.Get(Handedness.Right, BindingKey.StickGestureOf(ControlHand.Right, StickGesture.Up)));
                Assert.Empty(edit.Check());

                // No turn stick at all: the gestures stay, and the layer would refuse them.
                edit.SetStickRole(Handedness.Right, ControlHand.Left, StickRole.None);
                Assert.Null(edit.TurnStick(Handedness.Right));
                Assert.Equal(StickRole.Move, edit.Role(Handedness.Right, ControlHand.Right));
                Assert.Equal(3, edit.Check().Count(i => i.Conflict == ControlConflict.GestureOffTurnStick));
                edit.SetStickRole(Handedness.Right, ControlHand.Left, StickRole.Turn);
                Assert.Empty(edit.Check());
            }
        }

        // ---- Buttons and their names ----

        [Fact]
        public void TheButtonsShownAreTheControllersOwn()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var touch = Open(controls, "oculus_touch.toml");
                var inputs = touch.Inputs(Handedness.Right);
                Assert.Equal(11, inputs.Count); // the right Menu button belongs to the system
                Assert.DoesNotContain((ControlHand.Right, ButtonInput.Menu), inputs);
                Assert.Equal("Right A", touch.InputName(ControlHand.Right, ButtonInput.Primary));
                Assert.Equal("Left Y", touch.InputName(ControlHand.Left, ButtonInput.Secondary));
                Assert.Equal("Left stick click", touch.InputName(ControlHand.Left, ButtonInput.StickClick));
                Assert.Equal("Right stick", touch.StickName(ControlHand.Right));

                var wands = Open(controls, "htc_vive_wand.toml");
                Assert.False(wands.HasInput(ControlHand.Left, ButtonInput.Primary));
                Assert.Equal("Right Menu button", wands.InputName(ControlHand.Right, ButtonInput.Secondary));
                Assert.Equal("Left trackpad click", wands.InputName(ControlHand.Left, ButtonInput.StickClick));
                Assert.Equal("Left trackpad", wands.StickName(ControlHand.Left));

                var frame = Open(controls, "steam_frame.toml");
                Assert.Equal(18, frame.Inputs(Handedness.Right).Count); // four face buttons, a bumper and Menu or View on each
                Assert.Equal("Right X", frame.InputName(ControlHand.Right, ButtonInput.Face3));
                Assert.Equal("Left D-pad up", frame.InputName(ControlHand.Left, ButtonInput.Face4));
                Assert.Equal("Left View button", frame.InputName(ControlHand.Left, ButtonInput.Menu));
                Assert.Equal("Right bumper", frame.InputName(ControlHand.Right, ButtonInput.Shoulder));

                var wmr = Open(controls, "windows_mixed_reality.toml");
                Assert.Equal("Left trackpad click", wmr.InputName(ControlHand.Left, ButtonInput.Primary));
                Assert.Equal("Right grip", wmr.InputName(ControlHand.Right, ButtonInput.Grip));

                // A binding on a button the controller does not have is still shown, so it can be cleared.
                touch.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Right, ButtonInput.Menu, PressKind.Press), "pause");
                Assert.Contains((ControlHand.Right, ButtonInput.Menu), touch.Inputs(Handedness.Right));
                Assert.Equal("Right Menu button", touch.InputName(ControlHand.Right, ButtonInput.Menu));
                touch.SetAction(Handedness.Right, BindingKey.Button(ControlHand.Right, ButtonInput.Menu, PressKind.Press), null);
                Assert.Equal(11, touch.Inputs(Handedness.Right).Count);
            }
        }

        // ---- The player's files ----

        [Fact]
        public void TheFileEditedIsTheOneTheLayerUses()
        {
            using (var t = new TempDir())
            {
                var controls = Prepared(t);
                var touch = File.ReadAllText(Path.Combine(controls.DefaultsDir, "oculus_touch.toml"));
                t.Write("controls/a mine.toml", touch.Replace("\"right.trigger.press\" = \"fire\"", "\"right.trigger.press\" = \"jump\""));
                t.Write("controls/B mine.toml", touch.Replace("\"right.trigger.press\" = \"fire\"", "\"right.trigger.press\" = \"dash\""));
                t.Write("controls/pico.toml", File.ReadAllText(Path.Combine(controls.DefaultsDir, "pico4.toml")));
                var profile = "/interaction_profiles/oculus/touch_controller";
                Assert.Equal(new[] { "a mine.toml", "B mine.toml" }, controls.PlayerFilesFor(profile).Select(Path.GetFileName));
                Assert.Equal(t.Combine("controls", "B mine.toml"), controls.PlayerFileFor(profile));

                var edit = Open(controls, "oculus_touch.toml");
                Assert.Equal(t.Combine("controls", "B mine.toml"), edit.PlayerFile);
                Assert.Equal("dash", edit.Get(Handedness.Right, RightTrigger));
                // Back to the built-in controls, but "a mine.toml" would then be used: the file is written, not removed.
                edit.ResetToDefaults();
                Assert.Equal(ControlsSaveResult.Saved, edit.Save());
                Assert.Equal(touch, t.Read("controls/B mine.toml"));
                Assert.Equal(t.Combine("controls", "B mine.toml"), controls.PlayerFileFor(profile));

                // The Pico file, named differently, is found by its profile.
                Assert.Equal(t.Combine("controls", "pico.toml"), Open(controls, "pico4.toml").PlayerFile);
            }
        }
    }
}
