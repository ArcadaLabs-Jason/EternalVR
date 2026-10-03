using System;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The window's words state facts: what a setting does, what was measured, what was found. They never tell the player
    /// to lower a setting or what to change for a faster game; the player decides those trade-offs.
    /// </summary>
    public static class UiText
    {
        private static readonly Regex Advice = new Regex(
            @"\b(lower|reduce|decrease|change) (it|them|the|your)\b"
            + @"|\bturn (it|them) (down|off|on)\b|\bturn down\b"
            + @"|\btry (it|them|lowering|turning|reducing)\b"
            + @"|\bleave it (off|on|to)\b"
            + @"|\ba good start\b"
            + @"|\bif the (game|frame rate) (stutters|drops|crashes|is slow)\b",
            RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

        public static void AssertNoAdvice(string text, string where)
        {
            var m = Advice.Match(text ?? string.Empty);
            Assert.False(m.Success, $"{where}: \"{m.Value}\" tells the player what to change: {text}");
        }
    }

    public class UiTextTests
    {
        [Theory]
        [InlineData("Lower the render scale if the game stutters.")]
        [InlineData("Try them if the frame rate drops.")]
        [InlineData("Turn it off if the fades get in the way.")]
        [InlineData("A good start: K with Quality.")]
        [InlineData("Leave it off unless the game cannot keep up.")]
        public void TheScanFindsAdvice(string text)
        {
            Assert.ThrowsAny<Exception>(() => UiText.AssertNoAdvice(text, "sample"));
        }

        [Theory]
        [InlineData("Shades the edges of each eye at a lower rate.")]
        [InlineData("Turns down a few of the game's detail settings.")]
        [InlineData("A lower setting of your own is kept.")]
        [InlineData("Moves the HUD panel up or down (negative is lower).")]
        public void TheScanLeavesFactsAlone(string text)
        {
            UiText.AssertNoAdvice(text, "sample");
        }

        [Fact]
        public void SettingTextsGiveNoAdvice()
        {
            foreach (Setting setting in Enum.GetValues(typeof(Setting)))
            {
                var text = SettingTexts.For(setting);
                UiText.AssertNoAdvice(text.Label, setting + " label");
                UiText.AssertNoAdvice(text.Tooltip, setting + " tooltip");
                foreach (var choice in text.Choices) UiText.AssertNoAdvice(choice, setting + " choice");
            }
        }

        [Fact]
        public void CpuSaverTextsGiveNoAdvice()
        {
            foreach (var item in CpuSaver.Parse(TestData.Read("cpu-saver.txt")).Items)
            {
                UiText.AssertNoAdvice(item.Label, item.Id + " label");
                UiText.AssertNoAdvice(item.Note, item.Id + " note");
                UiText.AssertNoAdvice(item.Tooltip, item.Id + " tooltip");
            }
        }

        [Fact]
        public void TheFoveationTooltipSaysWhereItsGainWasMeasured()
        {
            var tip = SettingTexts.For(Setting.Foveation).Tooltip;
            Assert.Contains("16%", tip);
            Assert.Contains("on the test rig", tip);
        }
    }
}
