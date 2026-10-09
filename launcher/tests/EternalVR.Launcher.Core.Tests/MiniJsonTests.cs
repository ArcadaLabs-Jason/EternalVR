using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Text;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The manifests' JSON reader: anything malformed is a <see cref="FormatException"/>, never another exception.</summary>
    public class MiniJsonTests
    {
        [Fact]
        public void ReadsAManifest()
        {
            var root = MiniJson.Parse("\uFEFF{ \"layer\": { \"name\": \"VK_LAYER_x\", \"n\": [1.5, -2e3, true, null] } }");
            Assert.Equal("VK_LAYER_x", MiniJson.Get(root, "layer", "name"));
            Assert.Equal(new object[] { 1.5, -2000.0, true, null }, (List<object>)MiniJson.Get(root, "layer", "n"));
        }

        [Fact]
        public void ANumberOutOfRangeIsAFormatException()
        {
            // .NET Core reads 1e400 as infinity.
            Assert.Throws<FormatException>(() => MiniJson.Parse("{ \"v\": 1e400 }"));
            Assert.Throws<FormatException>(() => MiniJson.Parse("-1e400"));
            // The launcher runs on .NET Framework 4.8, whose double.Parse throws OverflowException there instead.
            Assert.Throws<FormatException>(() => MiniJson.Number("1e400", _ => throw new OverflowException()));
            Assert.Equal(12.5, MiniJson.Number("12.5", _ => 12.5));
            Assert.Throws<FormatException>(() => MiniJson.Parse("1-2"));
        }

        [Fact]
        public void NestingPastTheDepthIsAFormatException()
        {
            // Deep enough to overflow the stack without the cap (which nothing could catch).
            Assert.Throws<FormatException>(() => MiniJson.Parse(new string('[', 100000) + new string(']', 100000)));
            Assert.Throws<FormatException>(() => MiniJson.Parse(string.Concat(Enumerable.Repeat("{\"a\":", 65)) + "1" + new string('}', 65)));
            // Up to the cap reads.
            Assert.IsType<List<object>>(MiniJson.Parse(new string('[', MiniJson.MostDepth) + new string(']', MiniJson.MostDepth)));
            Assert.Throws<FormatException>(() => MiniJson.Parse(new string('[', MiniJson.MostDepth + 1) + new string(']', MiniJson.MostDepth + 1)));
        }
    }
}
