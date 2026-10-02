using System;
using System.Linq;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The headsets known by name and their native panels (<c>data\headsets.txt</c>, <see cref="HeadsetTable"/>).</summary>
    public class HeadsetTableTests
    {
        private static HeadsetTable Shipped => HeadsetTable.Parse(TestData.Read("headsets.txt"));

        [Fact]
        public void TheShippedTableLoadsWithTheOtherDataFiles()
        {
            var table = Shipped;
            Assert.True(table.Models.Count >= 15);
            Assert.Equal(table.Models.Count, LauncherData.Load(TestData.Dir).Headsets.Models.Count);
            // Every name once, every panel a real size.
            Assert.Equal(table.Models.Count, table.Models.Select(m => m.Name).Distinct(StringComparer.OrdinalIgnoreCase).Count());
            Assert.All(table.Models, m => Assert.False(m.Panel.IsEmpty));
        }

        [Fact]
        public void AVendorsRuntimeNamesTheModelExactly()
        {
            var table = Shipped;
            // Virtual Desktop and Meta Link give the product name as the system name (layer logs: "xr: system 'Meta Quest 3'").
            var quest3 = table.BySystemName("Meta Quest 3");
            Assert.Equal("Meta Quest 3", quest3.Name);
            Assert.Equal(new Extent(2064, 2208), quest3.Panel);
            Assert.Equal(new[] { 72, 80, 90, 120 }, quest3.RefreshRates);
            Assert.Equal(new Extent(1832, 1920), table.BySystemName("Oculus Quest2").Panel);
            Assert.Equal("Meta Quest 3S", table.BySystemName("Meta Quest 3S").Name);
            Assert.Equal("Meta Quest Pro", table.BySystemName("Meta Quest Pro").Name);
            Assert.Equal(new Extent(2880, 2880), table.BySystemName("Pimax Crystal").Panel);
            Assert.Equal(new Extent(3840, 3744), table.BySystemName("XR-4").Panel);
            // Exactly: not a part, not another case.
            Assert.Null(table.BySystemName("Meta Quest 3 on WiVRn"));
            Assert.Null(table.BySystemName("meta quest 3"));
            Assert.Null(table.BySystemName(""));
            Assert.Null(table.BySystemName(null));
        }

        [Fact]
        public void SteamVrsTrackingSystemNeverNamesAModel()
        {
            var table = Shipped;
            foreach (var token in new[] { "lighthouse", "oculus", "playstation_vr2", "cv", "aapvr", "pico", "vrlink" })
            {
                Assert.Null(table.BySystemName("SteamVR/OpenXR : " + token));
                Assert.Null(table.BySteamVrModel("SteamVR/OpenXR : " + token));
            }
            // Only a tracking system one headset alone uses names it, in any case; never as a system name or a model.
            Assert.Equal("PlayStation VR2", table.ByTrackingSystem("playstation_vr2").Name);
            Assert.Equal(new Extent(2000, 2040), table.ByTrackingSystem("PlayStation_VR2").Panel);
            foreach (var token in new[] { "lighthouse", "oculus", "cv", "aapvr", "pico", "vrlink", "holographic", "" })
                Assert.Null(table.ByTrackingSystem(token));
            Assert.Null(table.ByTrackingSystem(null));
            Assert.Null(table.BySystemName("playstation_vr2"));
            Assert.Null(table.BySteamVrModel("playstation_vr2"));
            // ALVR reports the headset it imitates: never matched.
            Assert.Null(table.BySteamVrModel("Miramar"));
        }

        [Fact]
        public void SteamVrsModelMatchesExactlyOrByAPart()
        {
            var table = Shipped;
            Assert.Equal("Valve Index", table.BySteamVrModel("Index").Name);
            Assert.Equal(new Extent(1440, 1600), table.BySteamVrModel("Index").Panel);
            Assert.Null(table.BySteamVrModel("index"));
            Assert.Equal("Steam Frame", table.BySteamVrModel("Deckard MP").Name);
            Assert.Equal("HTC Vive Pro 2", table.BySteamVrModel("VIVE_Pro 2 MV").Name);
            Assert.Equal("HTC Vive Pro", table.BySteamVrModel("Vive_Pro MV").Name);
            Assert.Equal("Meta Quest 3", table.BySteamVrModel("Meta Quest 3").Name);
            // A part, in any case.
            Assert.Equal("Meta Quest 3S", table.BySteamVrModel("Oculus Ventura").Name);
            Assert.Equal("Samsung Odyssey+", table.BySteamVrModel("Samsung Windows Mixed Reality 800ZBA").Name);
            // The more specific name comes first: Crystal Light is not the Crystal.
            Assert.Equal("Pimax Crystal Light", table.BySteamVrModel("Pimax Crystal Light").Name);
            Assert.Equal("Pimax Crystal Super", table.BySteamVrModel("PIMAX CRYSTAL SUPER QLED").Name);
            Assert.Equal("Pimax Crystal", table.BySteamVrModel("Pimax Crystal").Name);
            // An exact SteamVR name is not a system name.
            Assert.Null(table.BySystemName("Index"));
            Assert.Null(table.BySteamVrModel("Some Headset 9000"));
        }

        [Fact]
        public void TheFirstMatchInFileOrderWins()
        {
            var table = HeadsetTable.Parse("A | vr~quest | 1000x1000 | 90\nB | vr=Quest Pro | 2000x2000 | 90\n");
            Assert.Equal("A", table.BySteamVrModel("Quest Pro").Name);
            Assert.Null(HeadsetTable.Empty.BySystemName("Meta Quest 3"));
        }

        [Theory]
        [InlineData("Name | xr=A | 2064x2208")]
        [InlineData(" | xr=A | 2064x2208 | 90")]
        [InlineData("Name | | 2064x2208 | 90")]
        [InlineData("Name | id=A | 2064x2208 | 90")]
        [InlineData("Name | tk= | 2064x2208 | 90")]
        [InlineData("Name | xr= | 2064x2208 | 90")]
        [InlineData("Name | xr=A | 2064 | 90")]
        [InlineData("Name | xr=A | 100x100 | 90")]
        [InlineData("Name | xr=A | 2064x2208 | fast")]
        [InlineData("Name | xr=A | 2064x2208 | 90\nname | xr=B | 2064x2208 | 90")]
        public void ALineOutOfFormatIsRefused(string text)
        {
            Assert.Throws<FormatException>(() => HeadsetTable.Parse(text));
        }

        [Fact]
        public void CommentsAndBlankLinesAreSkipped()
        {
            var table = HeadsetTable.Parse("# a comment\n\nA | xr=A; vr=B ; vr~c | 2064x2208 | 72, 90\n");
            var a = Assert.Single(table.Models);
            Assert.Equal(3, a.Ids.Count);
            Assert.Equal(new[] { 72, 90 }, a.RefreshRates);
            Assert.Same(a, table.BySteamVrModel("abc"));
        }
    }
}
