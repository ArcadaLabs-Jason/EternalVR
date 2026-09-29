using System;
using System.Collections.Generic;
using System.Linq;

namespace EternalVR.Launcher.Core.Controls
{
    /// <summary>What is wrong (the layer's <c>BindingIssueKind</c>, features/input/binding_issue.hpp).</summary>
    public enum ControlIssueKind { Syntax, DuplicateKey, UnknownKey, UnknownValue, Conflict }

    /// <summary>Which rule a conflict breaks (the layer's <c>BindingConflict</c>).</summary>
    public enum ControlConflict { None, PressWithTapOrHold, SharedStickRole, GestureOffTurnStick }

    /// <summary>A problem the layer would find in a controller file, which makes it use the built-in controls instead.</summary>
    public sealed class ControlIssue
    {
        public ControlIssueKind Kind { get; set; }
        public ControlConflict Conflict { get; set; }
        /// <summary>The section the problem is in ("map.right"), or null for the file as a whole.</summary>
        public string Section { get; set; }
        /// <summary>The key at fault, or null.</summary>
        public string Key { get; set; }
        public string Value { get; set; }
        /// <summary>For a conflict: the binding it clashes with, which the layer keeps.</summary>
        public string OtherKey { get; set; }
        public string OtherValue { get; set; }
        /// <summary>The 1-based line in the file, or 0.</summary>
        public int Line { get; set; }
        /// <summary>What is wrong, in plain words for the player.</summary>
        public string Message { get; set; }

        public override string ToString() => Message;
    }

    /// <summary>
    /// The checks the layer makes on a control map before using it (features/input/binding_compiler.cpp): every key known,
    /// every value an action (or a hand, or a stick role), no <c>press</c> together with a <c>tap</c> or <c>hold</c> on one
    /// button, the two sticks not given one role, and stick gestures only on the turn stick. A tap and a hold on one button
    /// are fine, and so is one action on several buttons. The layer drops the faulty binding and, for a player's file, uses
    /// the built-in controls instead; the editor refuses to save.
    /// </summary>
    public static class ControlMapRules
    {
        /// <summary>
        /// The problems of one map's entries (key to value, as written). <paramref name="inputName"/> and
        /// <paramref name="stickName"/> name a button and a stick for the messages ("Right A", "Right trackpad"); the defaults
        /// are their generic names.
        /// </summary>
        public static IReadOnlyList<ControlIssue> Check(IReadOnlyDictionary<string, string> entries, string section = null,
            Func<ControlHand, ButtonInput, string> inputName = null, Func<ControlHand, string> stickName = null)
        {
            if (inputName == null) inputName = (hand, input) => ControlNames.Capitalized(hand) + " " + ControlNames.GenericInputName(input);
            if (stickName == null) stickName = hand => ControlNames.Capitalized(hand) + " stick";
            var issues = new List<ControlIssue>();
            var roles = new StickRole[2];
            var roleKeys = new string[2];
            var buttons = new List<(string Key, BindingKey Parsed, string Action)>();
            var gestures = new List<(string Key, BindingKey Parsed, string Action)>();

            // The layer walks its entries in key order (a std::map), which sets the order of the messages.
            foreach (var entry in entries.OrderBy(e => e.Key, StringComparer.Ordinal))
            {
                var key = entry.Key;
                var value = entry.Value;
                if (!BindingKey.TryParse(key, out var parsed))
                {
                    issues.Add(Issue(ControlIssueKind.UnknownKey, section, key, value,
                        "'" + key + "' is not a button, stick or gesture the mod knows"));
                    continue;
                }
                switch (parsed.Kind)
                {
                    case BindingKeyKind.WeaponHand:
                        if (!ControlNames.TryParseHand(value, out _))
                            issues.Add(Issue(ControlIssueKind.UnknownValue, section, key, value,
                                "'" + value + "' is not a hand for " + key + "; use 'left' or 'right'"));
                        break;
                    case BindingKeyKind.StickRole:
                        if (!ControlNames.TryParseRole(value, out var role))
                        {
                            issues.Add(Issue(ControlIssueKind.UnknownValue, section, key, value,
                                stickName(parsed.Hand) + ": '" + value + "' is not a stick role; use move, turn or none"));
                            break;
                        }
                        roles[(int)parsed.Hand] = role;
                        roleKeys[(int)parsed.Hand] = key;
                        break;
                    default:
                        if (value == ControlNames.Unbound) break;
                        if (ControlNames.FindAction(value) == null)
                        {
                            issues.Add(Issue(ControlIssueKind.UnknownValue, section, key, value,
                                Where(parsed, inputName, stickName) + ": '" + value + "' is not an action the mod knows"));
                            break;
                        }
                        (parsed.Kind == BindingKeyKind.Button ? buttons : gestures).Add((key, parsed, value));
                        break;
                }
            }

            // Both sticks with one role: the left one keeps it.
            var turnStick = (ControlHand?)null;
            var moveStick = (ControlHand?)null;
            foreach (var hand in new[] { ControlHand.Left, ControlHand.Right })
            {
                var role = roles[(int)hand];
                if (role == StickRole.None) continue;
                var taken = role == StickRole.Move ? moveStick : turnStick;
                if (taken.HasValue)
                {
                    issues.Add(Conflict(ControlConflict.SharedStickRole, section,
                        roleKeys[(int)hand], ControlNames.RoleName(role), roleKeys[(int)taken.Value], ControlNames.RoleName(role),
                        "Both sticks are set to " + ControlNames.RoleLabel(role) + "; give one of them another role"));
                    continue;
                }
                if (role == StickRole.Move) moveStick = hand;
                else turnStick = hand;
            }

            // A press on a button with a tap or a hold: the press would fire on every tap and hold. The press is kept.
            foreach (var b in buttons.Where(b => b.Parsed.Press != PressKind.Press))
            {
                var press = buttons.FirstOrDefault(p => p.Parsed.Press == PressKind.Press && p.Parsed.Hand == b.Parsed.Hand && p.Parsed.Input == b.Parsed.Input);
                if (press.Key == null) continue;
                issues.Add(Conflict(ControlConflict.PressWithTapOrHold, section, b.Key, b.Action, press.Key, press.Action,
                    inputName(b.Parsed.Hand, b.Parsed.Input) + ": " + ControlNames.ActionLabel(press.Action) + " (press) and "
                    + ControlNames.ActionLabel(b.Action) + " (" + ControlNames.PressLabel(b.Parsed.Press) + ") cannot share a button, "
                    + "because a press also fires on every tap and hold; clear one of them"));
            }

            // Gestures only on the turn stick.
            foreach (var g in gestures)
            {
                if (turnStick == g.Parsed.Hand) continue;
                var hand = g.Parsed.Hand;
                var roleKey = roleKeys[(int)hand] ?? BindingKey.Role(hand).ToString();
                issues.Add(Conflict(ControlConflict.GestureOffTurnStick, section, g.Key, g.Action, roleKey, ControlNames.RoleName(roles[(int)hand]),
                    Where(g.Parsed, inputName, stickName) + " (" + ControlNames.ActionLabel(g.Action) + ") only works on the turn stick, and the " + stickName(hand).ToLowerInvariant()
                    + " is set to " + ControlNames.RoleLabel(roles[(int)hand])));
            }
            return issues;
        }

        /// <summary>"Right A, tap" or "Right stick, down, hold".</summary>
        private static string Where(BindingKey key, Func<ControlHand, ButtonInput, string> inputName, Func<ControlHand, string> stickName) =>
            key.Kind == BindingKeyKind.Gesture
                ? stickName(key.Hand) + ", " + ControlNames.GestureLabel(key.Gesture)
                : inputName(key.Hand, key.Input) + ", " + ControlNames.PressLabel(key.Press);

        private static ControlIssue Issue(ControlIssueKind kind, string section, string key, string value, string message) =>
            new ControlIssue { Kind = kind, Section = section, Key = key, Value = value, Message = message };

        private static ControlIssue Conflict(ControlConflict conflict, string section, string key, string value, string otherKey, string otherValue, string message) =>
            new ControlIssue
            {
                Kind = ControlIssueKind.Conflict, Conflict = conflict, Section = section, Key = key, Value = value,
                OtherKey = otherKey, OtherValue = otherValue, Message = message,
            };
    }
}
