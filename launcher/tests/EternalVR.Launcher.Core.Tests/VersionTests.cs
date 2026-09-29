using EternalVR.Launcher.Core.Preflight;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class VersionTests
    {
        private const string Sha = "0123456789abcdef0123456789abcdef01234567";

        [Theory]
        [InlineData("0.1.0", "0.1.0")]
        [InlineData(" 0.1.0+" + Sha + " ", "0.1.0")]
        [InlineData("1.2.3-beta1+abc", "1.2.3-beta1")]
        [InlineData("0.1.0-dev", "0.1.0-dev")]
        public void NormalizeDropsBuildMetadata(string raw, string expected) => Assert.Equal(expected, VersionCheck.Normalize(raw));

        [Theory]
        [InlineData(null)]
        [InlineData("")]
        [InlineData("unknown")]
        [InlineData("0.1")]
        [InlineData("0.1.0.0")]
        [InlineData("v0.1.0")]
        public void NormalizeRejectsWhatIsNotAVersion(string raw) => Assert.Null(VersionCheck.Normalize(raw));

        [Theory]
        [InlineData("0.1.0+" + Sha, "0.1.0")]
        [InlineData("0.1.0", "0.1.0")]
        [InlineData("0.2.0-beta1+x", "0.2.0-BETA1")]
        public void SameReleaseMatches(string launcher, string layer) =>
            Assert.Equal(VersionVerdict.Match, VersionCheck.Compare(launcher, layer));

        [Theory]
        [InlineData("0.1.0+" + Sha, "0.2.0")]
        [InlineData("0.2.0", "0.1.0")]
        [InlineData("0.1.0", "0.1.1")]
        [InlineData("1.0.0", "0.1.0")]
        [InlineData("0.2.0-beta1", "0.2.0-beta2")]
        [InlineData("0.2.0", "0.2.0-beta2")]
        public void DifferentReleasesMismatch(string launcher, string layer) =>
            Assert.Equal(VersionVerdict.Mismatch, VersionCheck.Compare(launcher, layer));

        [Theory]
        [InlineData("0.1.0-dev+" + Sha, "0.2.0")]
        [InlineData("0.1.0", "0.2.0-dev")]
        [InlineData("0.1.0", "0.2.0-dev.3")]
        [InlineData("0.1.0", null)]
        [InlineData("0.1.0", "")]
        [InlineData(null, "0.1.0")]
        [InlineData("0.1.0", "garbage")]
        public void DevOrUnknownIsUnverified(string launcher, string layer) =>
            Assert.Equal(VersionVerdict.Unverified, VersionCheck.Compare(launcher, layer));

        [Fact]
        public void DevMarkerMustBeAWholePart()
        {
            Assert.True(VersionCheck.IsDev("0.1.0-dev"));
            Assert.True(VersionCheck.IsDev("0.1.0-rc.DEV"));
            Assert.False(VersionCheck.IsDev("0.1.0-devices"));
            Assert.False(VersionCheck.IsDev("0.1.0"));
        }

        [Fact]
        public void MismatchRefusesWithBothVersionsAndTheFix()
        {
            var c = VersionCheck.Evaluate("0.1.0+" + Sha, "0.2.0", @"D:\EternalVR\layer");
            Assert.Equal(Severity.Fail, c.Severity);
            Assert.Equal("version", c.Id);
            Assert.Contains("launcher is 0.1.0", c.Message);
            Assert.Contains("layer in D:\\EternalVR\\layer is 0.2.0", c.Message);
            Assert.Contains("unzip the whole release again", c.Message);
            Assert.DoesNotContain(Sha, c.Message);
        }

        [Fact]
        public void DevAndUnknownOnlyWarn()
        {
            var dev = VersionCheck.Evaluate("0.1.0-dev", "0.3.0", "x");
            Assert.Equal(Severity.Warn, dev.Severity);
            Assert.Contains("development build", dev.Message);
            var unknown = VersionCheck.Evaluate("0.1.0-dev", null, "x");
            Assert.Equal(Severity.Warn, unknown.Severity);
            Assert.Contains("layer unknown", unknown.Message);
            Assert.Contains("could not be read", unknown.Message);
            var noLauncher = VersionCheck.Evaluate(null, "0.1.0", "x");
            Assert.Equal(Severity.Warn, noLauncher.Severity);
        }

        [Fact]
        public void AReleaseLauncherRefusesALayerWithoutAVersion()
        {
            // A layer folder left in the settings loaded a layer built before the handshake (2026-09-28).
            var c = VersionCheck.Evaluate("0.1.2+" + Sha, null, @"E:\old\layer");
            Assert.Equal(Severity.Fail, c.Severity);
            Assert.Contains(@"E:\old\layer", c.Message);
            Assert.Contains("layer_dir", c.Message);
        }

        [Fact]
        public void MatchPasses()
        {
            var c = VersionCheck.Evaluate("0.1.0+" + Sha, "0.1.0", "x");
            Assert.Equal(Severity.Pass, c.Severity);
            Assert.Contains("0.1.0", c.Message);
        }

        [Fact]
        public void PreflightChecksVersionOnlyWithACompleteLayer()
        {
            var f = new PreflightFacts { LayerManifestExists = true, LayerLibraryExists = false, LauncherVersion = "0.1.0", LayerVersion = "0.2.0" };
            Assert.DoesNotContain(PreflightEvaluator.Evaluate(f).Checks, c => c.Id == "version");
            f.LayerLibraryExists = true;
            Assert.Contains(PreflightEvaluator.Evaluate(f).Checks, c => c.Id == "version" && c.Severity == Severity.Fail);
        }
    }
}
