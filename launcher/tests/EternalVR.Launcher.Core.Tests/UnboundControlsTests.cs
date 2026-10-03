using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The controls a session bound, from the layer's <c>controllers:</c> lines (<see cref="SessionSummary"/>), and the status
    /// line after a session that bound none, named for the cause (<see cref="UnboundControls"/>).
    /// </summary>
    public class UnboundControlsTests
    {
        private const string Steam = "SteamVR/OpenXR";
        private const string Vd = "VirtualDesktopXR";
        private const string Sense = "/interaction_profiles/sony/playstation_vr2_sense_controller";

        private static string Bound(int bound, int all) =>
            $"[   31.204] [24384] controllers: {bound} of {all} action(s) bound (left hand {Sense}, right hand {Sense}); "
            + "unbound on the left hand: move, turn, fire; on the right hand: move, turn, fire";

        private const string NoPose = "[   52.118] [24384] controllers: WARNING no hand pose has been valid for 10 s of play with the headset "
            + "tracked and the runtime reporting controllers (left hand " + Sense + ", right hand " + Sense + "). If the controllers are on "
            + "and tracked, the likely cause: SteamVR is using a custom or workshop controller binding for DOOM Eternal";

        /// <summary>A short session on <paramref name="runtime"/> (one 10 s window at 90 Hz) with <paramref name="extra"/> lines.</summary>
        private static SessionSummary Session(string runtime, params string[] extra)
        {
            var lines = new List<string>
            {
                "[    7.657] [10176] xr: runtime '" + runtime + "' 1.0.10, api 1.0",
                "[    7.672] [10176] xr: system 'PlayStation VR2', max swapchain 16384x16384",
                "[    7.800] [24384] xr: 1 frame(s), 0 new image(s), 1 repeat(s); 0 head-tracked, 1 on the screen; pose age average 0.0 ms, max 0.0 ms; display period 11.11 ms, pose lead 0.0 ms",
                "[   17.800] [24384] xr: 900 frame(s), 895 new image(s), 5 repeat(s); 0 head-tracked, 900 on the screen; pose age average 0.0 ms, max 0.0 ms; display period 11.11 ms, pose lead 0.0 ms",
                "[   21.204] [24384] controllers: the runtime reports " + Sense + " for the right hand",
            };
            lines.AddRange(extra);
            return SessionSummary.FromLines(lines);
        }

        // Made-up workshop item; the key as a PS VR2 player's steamvr.vrsettings had it for the game's app key.
        private const string Psvr2Binding = @"{
   ""steam.app.782330"" : {
      ""playstation_vr2_sense_250820_CurrentURL_openxr"" : ""vr-input-workshop://1000000010""
   }
}";

        [Fact]
        public void TheBoundLineIsRead()
        {
            var s = Session(Steam, Bound(0, 12), NoPose);
            Assert.Equal(0, s.ControlsBound);
            Assert.Equal(12, s.Controls);
            Assert.True(s.NoHandPose);
            Assert.Equal("0 of 12 bound, no hand pose in play", s.ControlsText());
            Assert.EndsWith("; controls 0 of 12 bound, no hand pose in play", s.LogText());

            // The last line counts (a session can list them again).
            var again = Session(Vd, Bound(0, 12), Bound(12, 12));
            Assert.Equal(12, again.ControlsBound);
            Assert.False(again.NoHandPose);
            Assert.Equal("12 of 12 bound", again.ControlsText());

            var none = Session(Vd);
            Assert.Null(none.ControlsBound);
            Assert.Null(none.Controls);
            Assert.Equal("not logged", none.ControlsText());
        }

        [Fact]
        public void SteamVrWithAChosenBindingNamesTheBinding()
        {
            Assert.Equal(UnboundCause.SteamVrBinding, UnboundControls.Decide(Session(Steam, Bound(0, 12)), () => true));
            Assert.Equal(UnboundCause.SteamVrBinding, UnboundControls.Decide(Session(Steam, Bound(0, 12), NoPose), () => true));
            Assert.Equal("Controllers did nothing: SteamVR's controller binding for DOOM Eternal maps none of EternalVR's controls. "
                + "While the game runs: SteamVR > Settings > Controllers > Manage Controller Bindings > DOOM Eternal > Default.",
                UnboundControls.StatusText(UnboundCause.SteamVrBinding));
        }

        [Fact]
        public void AnyOtherCauseGetsTheReportOnlyWhenNoHandPoseArrived()
        {
            const string text = "No controller input reached EternalVR this session. Click Export report and post the zip with your bug report.";
            // SteamVR without a chosen binding, and another runtime (whose binding SteamVR's file says nothing about).
            Assert.Equal(UnboundCause.Unknown, UnboundControls.Decide(Session(Steam, Bound(0, 12), NoPose), () => false));
            Assert.Equal(UnboundCause.Unknown, UnboundControls.Decide(Session(Vd, Bound(0, 12), NoPose),
                () => throw new InvalidOperationException("SteamVR's settings are not asked for another runtime")));
            Assert.Equal(text, UnboundControls.StatusText(UnboundCause.Unknown));
            // Some runtimes list no source for working bindings: zero alone is not shown.
            Assert.Equal(UnboundCause.None, UnboundControls.Decide(Session(Steam, Bound(0, 12)), () => false));
            Assert.Equal(UnboundCause.None, UnboundControls.Decide(Session(Vd, Bound(0, 12)), null));
        }

        [Fact]
        public void BoundControlsOrNoCountSayNothing()
        {
            Func<bool> never = () => throw new InvalidOperationException("SteamVR's settings are read only when nothing was bound");
            Assert.Equal(UnboundCause.None, UnboundControls.Decide(Session(Steam, Bound(12, 12), NoPose), never));
            Assert.Equal(UnboundCause.None, UnboundControls.Decide(Session(Steam, Bound(1, 12)), never));
            Assert.Equal(UnboundCause.None, UnboundControls.Decide(Session(Steam, Bound(0, 0), NoPose), never));
            Assert.Equal(UnboundCause.None, UnboundControls.Decide(Session(Steam, NoPose), never));
            Assert.Equal(UnboundCause.None, UnboundControls.Decide(null, never));
            Assert.Equal(string.Empty, UnboundControls.StatusText(UnboundCause.None));
        }

        [Fact]
        public void SteamVrsSettingsFileDecidesTheCause()
        {
            using (var t = new TempDir())
            {
                var steam = t.Combine("steam");
                var s = Session(Steam, Bound(0, 12), NoPose);
                UnboundCause Decide() => UnboundControls.Decide(s, () => SteamVrSummary.ReadCustomBindings(steam).Count > 0);

                Assert.Equal(UnboundCause.Unknown, Decide()); // no settings file
                t.Write(@"steam\config\steamvr.vrsettings", Psvr2Binding);
                Assert.Equal(UnboundCause.SteamVrBinding, Decide());
                Assert.Equal("workshop binding for playstation_vr2_sense", Assert.Single(SteamVrSummary.ReadCustomBindings(steam)).ToString());
                t.Write(@"steam\config\steamvr.vrsettings", @"{ ""steam.app.620980"" : { ""knuckles_CurrentURL_openxr"" : ""vr-input-workshop://1000000011"" } }");
                Assert.Equal(UnboundCause.Unknown, Decide());
                t.Write(@"steam\config\steamvr.vrsettings", @"{ ""steam.app.782330"" : { ""playstation_vr2_sense_250820_CurrentURL_openxr"" : """" } }");
                Assert.Equal(UnboundCause.Unknown, Decide());
                t.Write(@"steam\config\steamvr.vrsettings", @"{ ""steam.app.782330"" : { ""playstation_vr2_sense_250820_CurrentURL_openxr"" ");
                Assert.Equal(UnboundCause.Unknown, Decide());
            }
        }

        [Fact]
        public void TheReportSaysTheControlsAndTheCause()
        {
            var ended = new DateTime(2026, 10, 2, 20, 15, 0);
            var facts = LastHeadset.AfterSession(new HeadsetFacts(), Session(Steam, Bound(0, 12)), "20261002-200000", ended, UnboundCause.SteamVrBinding);
            var back = LastHeadset.Parse(LastHeadset.Serialize(facts));
            Assert.Equal("0 of 12 bound; cause: SteamVR binding for DOOM Eternal", back.SessionControls);
            var lines = HeadsetView.ReportLines(back, null).ToDictionary(kv => kv.Key, kv => kv.Value);
            Assert.Equal("0 of 12 bound; cause: SteamVR binding for DOOM Eternal", lines["last session controls"]);

            var unknown = LastHeadset.AfterSession(facts, Session(Vd, Bound(0, 12), NoPose), "20261002-210000", ended.AddHours(1), UnboundCause.Unknown);
            Assert.Equal("0 of 12 bound, no hand pose in play; cause: unknown", unknown.SessionControls);
            Assert.Equal("12 of 12 bound", LastHeadset.AfterSession(unknown, Session(Vd, Bound(12, 12)), "20261002-220000", ended).SessionControls);
            Assert.Equal("not logged", HeadsetView.ReportLines(new HeadsetFacts { Session = "20261002-200000" }, null)
                .Single(kv => kv.Key == "last session controls").Value);
        }
    }
}
