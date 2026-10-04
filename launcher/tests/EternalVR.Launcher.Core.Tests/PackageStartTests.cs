using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The Game Pass start inside the package: the plan file, the environment block and the PowerShell command.</summary>
    public class PackageStartTests
    {
        private static KeyValuePair<string, string> Kv(string k, string v) => new KeyValuePair<string, string>(k, v);

        [Fact]
        public void ThePlanFileReadsBackAsWritten()
        {
            var plan = new PackageStartPlan
            {
                ExePath = @"C:\XboxGames\Doom Eternal - PC\Content\DOOMEternalx64vk.exe",
                WorkingDirectory = @"C:\XboxGames\Doom Eternal - PC\Content",
                CommandLine = "+logFile 2 +com_skipIntroVideo 1",
                Environment = new[]
                {
                    Kv("ETERNALVR_ENABLE_LAYER", "1"),
                    Kv("VK_ADD_IMPLICIT_LAYER_PATH", @"C:\Users\Tester\Desktop\ETERNALVR\layer"),
                    Kv("=C:", @"C:\Users\Tester"),
                    Kv("ODD", "line\nbreak"), // left out
                    Kv("PATH", "a;b=c"),
                },
            };

            var back = PackageStart.Read(PackageStart.Write(plan));

            Assert.Equal(plan.ExePath, back.ExePath);
            Assert.Equal(plan.WorkingDirectory, back.WorkingDirectory);
            Assert.Equal(plan.CommandLine, back.CommandLine);
            Assert.Equal(new[] { "ETERNALVR_ENABLE_LAYER", "VK_ADD_IMPLICIT_LAYER_PATH", "=C:", "PATH" }, back.Environment.Select(kv => kv.Key));
            Assert.Equal("a;b=c", back.Environment.Last().Value);
            Assert.Equal(@"C:\Users\Tester", back.Environment[2].Value);
            Assert.Null(PackageStart.Read("cwd\tC:\\\n"));
            Assert.Null(PackageStart.Read(null));
        }

        [Fact]
        public void TheEnvironmentBlockIsSortedWithoutCaseAndDoublyEnded()
        {
            var block = PackageStart.EnvironmentBlock(new[] { Kv("b", "2"), Kv("A", "1"), Kv("B", "3") });
            Assert.Equal("A=1\0b=2\0\0", block);
            Assert.Equal("\0\0", PackageStart.EnvironmentBlock(null));
        }

        [Fact]
        public void TheCommandStartsTheHelperInsideTheDoomPackage()
        {
            var command = PackageStart.Command(@"C:\Users\O'Brien\EternalVR\EternalVR.Launcher.exe", @"C:\data\logs\1\package-start.txt");
            Assert.Contains("Get-AppxPackage -Name 'BethesdaSoftworks.DOOMEternal-PC'", command);
            Assert.Contains("Invoke-CommandInDesktopPackage -PackageFamilyName $p.PackageFamilyName -AppId $app", command);
            Assert.Contains(@"-Command 'C:\Users\O''Brien\EternalVR\EternalVR.Launcher.exe'", command);
            Assert.Contains(@"-Args '--start-in-package ""C:\data\logs\1\package-start.txt""'", command);
            Assert.Contains("$ProgressPreference = 'SilentlyContinue'", command);
        }

        [Fact]
        public void TheLauncherRouteSurvivesThePlanFile()
        {
            var plan = new PackageStartPlan { ExePath = @"C:\XboxGames\Doom Eternal - PC\Content\DOOMEternalx64vk.exe", ViaLauncher = true };
            Assert.True(PackageStart.Read(PackageStart.Write(plan)).ViaLauncher);
            plan.ViaLauncher = false;
            Assert.False(PackageStart.Read(PackageStart.Write(plan)).ViaLauncher);
        }

        [Fact]
        public void TheDoomEternalLauncherAndItsSettingsAreInTheGameFolder()
        {
            const string exe = @"C:\XboxGames\Doom Eternal - PC\Content\DOOMEternalx64vk.exe";
            Assert.Equal(@"C:\XboxGames\Doom Eternal - PC\Content\launcher\idTechLauncher.exe", PackageStart.BethesdaLauncher(exe));
            Assert.Equal(@"C:\XboxGames\Doom Eternal - PC\Content\doom-launcher-settings.json", PackageStart.BethesdaSettings(exe));
        }

        [Fact]
        public void OnlyTheLaunchTargetChanges()
        {
            const string settings = "{\n\t\"_settings\": {\n\t\t\"remember_login\": true,\n\t\t\"launch_target\": \"portal\",\n\t\t\"language\": \"en-us\"\n\t}\n}";
            Assert.Equal("portal", PackageStart.LaunchTarget(settings));
            var skipped = PackageStart.WithLaunchTarget(settings, PackageStart.SkipLauncherTarget);
            Assert.Equal(settings.Replace("\"portal\"", "\"retail\""), skipped);
            Assert.Equal(settings, PackageStart.WithLaunchTarget(skipped, "portal"));
            Assert.Equal("retail", PackageStart.LaunchTarget("{\"launch_target\" : \"retail\"}"));
        }

        [Fact]
        public void SettingsWithoutALaunchTargetAreLeftAlone()
        {
            Assert.Null(PackageStart.LaunchTarget("{\"_settings\": {}}"));
            Assert.Null(PackageStart.WithLaunchTarget("{\"_settings\": {}}", "retail"));
            Assert.Null(PackageStart.WithLaunchTarget(null, "retail"));
        }
    }
}
