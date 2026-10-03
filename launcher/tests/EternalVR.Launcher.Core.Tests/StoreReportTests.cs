using System;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>Windows' crash reports (Report.wer) and the Store packages in a report.</summary>
    public class StoreReportTests
    {
        private static readonly DateTime Now = new DateTime(2026, 10, 3, 22, 30, 0);

        /// <summary>A Report.wer as WER writes it (UTF-16 with a byte order mark, CRLF), last written at <paramref name="written"/>.</summary>
        private static string Wer(TempDir t, string folder, string app, DateTime written)
        {
            var path = t.Combine(@"wer\ReportArchive", folder, "Report.wer");
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            File.WriteAllText(path, "Version=1\r\nEventType=APPCRASH\r\nSig[0].Name=Application Name\r\nSig[0].Value=" + app
                + "\r\nSig[6].Name=Exception Code\r\nSig[6].Value=c0000005\r\nLoadedModule[0]=C:\\Users\\Tester\\Desktop\\ETERNALVR\\layer\\EternalVR.dll\r\n",
                Encoding.Unicode);
            File.SetLastWriteTime(path, written);
            return path;
        }

        [Fact]
        public void TheGamesNewestWindowsCrashReportsAreFoundAndOthersAreNot()
        {
            using (var t = new TempDir())
            {
                var a = Wer(t, "AppCrash_DOOMEternalx64vk_be2f4ec8_8441614d_83d2fee4", "DOOMEternalx64vk.exe", Now.AddHours(-1));
                var b = Wer(t, "AppCrash_DOOMEternalx64vk_be2f4ec8_8441614d_11111111", "DOOMEternalx64vk.exe", Now.AddHours(-2));
                var c = Wer(t, "AppHang_EternalVR.Launch_0a1b2c3d_5e6f7a8b_22222222", "EternalVR.Launcher.exe", Now.AddHours(-3));
                Wer(t, "AppCrash_DOOMEternalx64vk_be2f4ec8_8441614d_33333333", "DOOMEternalx64vk.exe", Now.AddHours(-4));
                Wer(t, "AppCrash_DOOMEternalx64vk_old_44444444", "DOOMEternalx64vk.exe", Now.AddDays(-8));
                Wer(t, "AppCrash_HWiNFO64.EXE_129e513f_05c59164_7e802b9f", "HWiNFO64.EXE", Now.AddHours(-1));
                Wer(t, "AppCrash_DOOMEternalx64vk_another_55555555", "SomethingElse.exe", Now.AddHours(-1)); // the name does not match inside

                var found = WindowsErrorReports.Find(new[] { t.Combine("wer"), t.Combine("missing") }, Now.AddDays(-7), 3, out var notTaken);

                Assert.Equal(new[] { a, b, c }, found);
                Assert.Equal(3, notTaken);
                Assert.Empty(WindowsErrorReports.Find(null, Now.AddDays(-7), 3, out _));
            }
        }

        [Fact]
        public void WindowsCrashReportsAreInTheZipAsTextRedacted()
        {
            using (var t = new TempDir())
            {
                var inputs = ReportTests.Setup(t);
                inputs.Now = Now;
                inputs.WindowsErrorReportDirs = new[] { t.Combine("wer") };
                Wer(t, "AppCrash_DOOMEternalx64vk_be2f4ec8_8441614d_83d2fee4", "DOOMEternalx64vk.exe", new DateTime(2026, 10, 3, 22, 21, 52));
                Wer(t, "AppCrash_DOOMEternalx64vk_be2f4ec8_8441614d_11111111", "DOOMEternalx64vk.exe", new DateTime(2026, 10, 3, 22, 21, 52));

                var entries = ReportTests.Unzip(ReportBuilder.Build(inputs).Zip);

                var text = entries["windows-crashes/AppCrash-20261003-222152.txt"];
                Assert.True(entries.ContainsKey("windows-crashes/AppCrash-20261003-222152-2.txt"));
                Assert.Contains("Sig[6].Value=c0000005\n", text);
                Assert.Contains(@"LoadedModule[0]=%USERPROFILE%\Desktop\ETERNALVR\layer\EternalVR.dll", text);
                Assert.DoesNotContain("Tester", text);
            }
        }

        [Theory]
        [InlineData("DOOMEternalx64vk.exe", "DOOMEternalx64vk")]
        [InlineData("EternalVR.Launcher.exe", "EternalVR.Launch")]
        [InlineData("EternalVR.dll", "EternalVR.dll")]
        public void ReportFolderPrefixesAreTheExeNameAsWerShortensIt(string program, string expected) =>
            Assert.Equal(expected, WindowsErrorReports.FolderPrefix(program));

        [Fact]
        public void StoreProcessesAreLoggedByNameIdAndStart()
        {
            var seen = new[]
            {
                new SeenProcess("XboxPcApp", 8812, new DateTime(2026, 10, 3, 13, 20, 5)),
                new SeenProcess("gamingservices", 4120, null),
            };
            Assert.Equal("store processes at launch: gamingservices 4120, XboxPcApp 8812 (since 13:20:05)", StoreProcesses.Describe("at launch", seen));
            Assert.Equal("store processes after the early exit: none", StoreProcesses.Describe("after the early exit", null));
        }

        [Fact]
        public void StorePackagesAreDescribedByVersionStatusAndTheGamesFolder()
        {
            var output = @"Microsoft.GamingServices|38.116.6003.0|Ok|Store|C:\Program Files\WindowsApps\Microsoft.GamingServices_38.116.6003.0_x64__8wekyb3d8bbwe|Microsoft.GamingServices_38.116.6003.0_x64__8wekyb3d8bbwe" + "\r\n"
                + @"BethesdaSoftworks.DOOMEternal-PC|1.0.56.0|LicenseIssue|Store|D:\XboxGames\Doom Eternal - PC\Content|BethesdaSoftworks.DOOMEternal-PC_1.0.56.0_x64__3275kfvn8vcwc" + "\r\n"
                + "garbage line\r\n";

            var lines = StorePackages.Describe(output, null).Select(kv => kv.Key + ": " + kv.Value).ToList();

            Assert.Equal(new[]
            {
                @"store package: 1.0.56.0, status LicenseIssue, signature Store, BethesdaSoftworks.DOOMEternal-PC_1.0.56.0_x64__3275kfvn8vcwc in D:\XboxGames\Doom Eternal - PC\Content",
                "gaming services: 38.116.6003.0, status Ok, signature Store",
                "xbox app: not installed",
                "xbox identity provider: not installed",
            }, lines);
            Assert.Equal(new[] { "store packages: could not be read (no answer in 20 s)" },
                StorePackages.Describe(null, "no answer in 20 s").Select(kv => kv.Key + ": " + kv.Value));
            Assert.Contains("Get-AppxPackage", StorePackages.Query());
            Assert.Contains("'Microsoft.GamingServices'", StorePackages.Query());
        }
    }
}
