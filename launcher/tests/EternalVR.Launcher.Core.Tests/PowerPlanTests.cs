using System;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class PowerPlanTests
    {
        private static readonly Guid Balanced = new Guid("381b4222-f694-41f0-9685-ff5bb260df2e");

        [Fact]
        public void NameAndGuid() =>
            Assert.Equal("Balanced {381b4222-f694-41f0-9685-ff5bb260df2e}", PowerPlan.Describe("Balanced", Balanced));

        [Fact]
        public void ABuiltInSchemeIsNamedFromItsGuidWhenTheNameCannotBeRead() =>
            Assert.Equal("Balanced {381b4222-f694-41f0-9685-ff5bb260df2e}", PowerPlan.Describe(null, Balanced));

        [Fact]
        public void ACustomSchemeWithNoNameIsItsGuid()
        {
            var g = new Guid("11111111-2222-3333-4444-555555555555");
            Assert.Equal("{11111111-2222-3333-4444-555555555555}", PowerPlan.Describe("  ", g));
        }

        [Fact]
        public void ANameWithoutAGuidStandsAlone() => Assert.Equal("My plan", PowerPlan.Describe(" My   plan ", null));

        [Fact]
        public void NothingIsUnknown() => Assert.Equal("unknown", PowerPlan.Describe(null, null));
    }
}
