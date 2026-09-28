using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace EternalVR.FakeGame
{
    /// <summary>
    /// Behaves like the game as far as the launcher can tell: it records its command line and the
    /// environment it was given, rewrites a config file the way a VR session would, and stays running
    /// for a while. Its instructions come from <c>fake-game.ini</c> in its working directory:
    /// <c>record</c> (output file), <c>config</c> (file to rewrite), <c>config_text</c> (new content,
    /// <c>\n</c> for line breaks) and <c>sleep_ms</c>. With <c>handoff=1</c> it plays a Steam hand-off:
    /// it exits at once and a second copy of it appears about two seconds later (started through
    /// <c>cmd.exe</c>, as Steam would start it after the first has gone), which runs and rewrites the
    /// config instead. With <c>cloud_accept=&lt;remote folder&gt;</c> it plays Steam taking the cloud files
    /// on disk as the current version: it writes <c>remotecache.vdf</c> next to that folder. When it was
    /// given <c>ETERNALVR_WINDOW</c> it also saves that window into the config on exit
    /// (<c>r_windowPosX</c>, <c>r_windowPosY</c>, <c>r_windowWidth</c>, <c>r_windowHeight</c>,
    /// <c>r_fullscreen</c>), as the game saves the window the layer placed.
    /// </summary>
    internal static class Program
    {
        private static int Main(string[] args)
        {
            var ini = File.ReadAllLines("fake-game.ini")
                .Select(l => l.Split(new[] { '=' }, 2))
                .Where(p => p.Length == 2)
                .ToDictionary(p => p[0].Trim(), p => p[1].Trim(), StringComparer.OrdinalIgnoreCase);
            bool child = args.Length > 0 && args[0] == "--child";
            bool handoff = ini.TryGetValue("handoff", out var h) && h == "1";

            var lines = new List<string> { (child ? "child " : string.Empty) + "cwd=" + Environment.CurrentDirectory, "cmdline=" + Environment.CommandLine };
            foreach (DictionaryEntry e in Environment.GetEnvironmentVariables())
            {
                var key = (string)e.Key;
                if (key.StartsWith("ETERNALVR_", StringComparison.OrdinalIgnoreCase) || key.StartsWith("VK_", StringComparison.OrdinalIgnoreCase)
                    || key.StartsWith("XR_", StringComparison.OrdinalIgnoreCase) || key.StartsWith("DISABLE_", StringComparison.OrdinalIgnoreCase)
                    || key.Equals("SteamAppId", StringComparison.OrdinalIgnoreCase))
                    lines.Add("env:" + key + "=" + e.Value);
            }
            if (child) File.AppendAllLines(ini["record"], lines);
            else File.WriteAllLines(ini["record"], lines);

            if (handoff && !child)
            {
                var self = Process.GetCurrentProcess().MainModule.FileName;
                var psi = new ProcessStartInfo("cmd.exe", "/c ping -n 3 127.0.0.1 >nul & \"" + self + "\" --child")
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };
                Process.Start(psi).Dispose();
                return 0;
            }

            // Plays Steam at the end of a short app session: the cloud files on disk become its record.
            if (ini.TryGetValue("cloud_accept", out var remote)) WriteCloudRecord(remote);

            Thread.Sleep(int.Parse(ini.TryGetValue("sleep_ms", out var ms) ? ms : "500"));
            // Written at the end, as the game writes its config on exit.
            if (ini.TryGetValue("config", out var config))
                File.WriteAllText(config, ini["config_text"].Replace("\\n", "\n") + WindowKeys());
            return 0;
        }

        /// <summary>The window keys the game saves on exit for the window <c>ETERNALVR_WINDOW</c> placed; empty without it.</summary>
        private static string WindowKeys()
        {
            var parts = (Environment.GetEnvironmentVariable("ETERNALVR_WINDOW") ?? string.Empty).Split(',');
            if (parts.Length != 4) return string.Empty;
            return "r_windowPosX \"" + parts[0] + "\"\nr_windowPosY \"" + parts[1] + "\"\nr_windowWidth \"" + parts[2]
                + "\"\nr_windowHeight \"" + parts[3] + "\"\nr_fullscreen \"0\"\n";
        }

        /// <summary>Writes <c>remotecache.vdf</c> next to <paramref name="remote"/> in Steam's format: size and SHA-1 per file.</summary>
        private static void WriteCloudRecord(string remote)
        {
            var root = Path.GetFullPath(remote).TrimEnd('\\');
            var sb = new StringBuilder("\"782330\"\n{\n\t\"ChangeNumber\"\t\t\"8\"\n");
            foreach (var f in Directory.GetFiles(root, "*", SearchOption.AllDirectories).OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
            {
                string sha;
                using (var h = SHA1.Create())
                    sha = string.Concat(h.ComputeHash(File.ReadAllBytes(f)).Select(b => b.ToString("x2")));
                sb.Append("\t\"").Append(f.Substring(root.Length + 1).Replace('\\', '/')).Append("\"\n\t{\n")
                  .Append("\t\t\"root\"\t\t\"0\"\n\t\t\"size\"\t\t\"").Append(new FileInfo(f).Length).Append("\"\n")
                  .Append("\t\t\"sha\"\t\t\"").Append(sha).Append("\"\n\t\t\"syncstate\"\t\t\"1\"\n\t}\n");
            }
            sb.Append("}\n");
            File.WriteAllText(Path.Combine(Path.GetDirectoryName(root), "remotecache.vdf"), sb.ToString());
        }
    }
}
