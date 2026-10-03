using System;
using System.Collections.Generic;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Launch;

namespace EternalVR.Launcher.UiShots
{
    /// <summary>
    /// Opens the launcher's main window (in test mode, on a fake tree under --work) on one display and every dialog in each
    /// of its states over it, and saves each as a PNG with a .txt of its controls and the layout faults found. See README.md.
    /// </summary>
    internal static class Program
    {
        private const string Usage = "evr-ui-shots --out <dir> --work <dir> [--screen <n>] [--drag <n>] [--only <text>] [--live-update <folder>]";

        [STAThread]
        private static int Main(string[] args)
        {
            string output = null, work = null, only = null, live = null;
            int screen = 0;
            var moves = new List<Move>();
            for (int i = 0; i < args.Length; i++)
            {
                string Value() => i + 1 < args.Length ? args[++i] : throw new ArgumentException(args[i] + " needs a value");
                switch (args[i])
                {
                    case "--out": output = Path.GetFullPath(Value()); break;
                    case "--work": work = Path.GetFullPath(Value()); break;
                    case "--screen": screen = int.Parse(Value()); break;
                    case "--drag": moves.Add(new Move { Screen = Screen.AllScreens[int.Parse(Value())] }); break;
                    case "--only": only = Value(); break;
                    case "--live-update": live = Path.GetFullPath(Value()); break;
                    default: Console.Error.WriteLine(Usage); return 64;
                }
            }
            if (output == null || work == null)
            {
                Console.Error.WriteLine(Usage);
                return 64;
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Directory.CreateDirectory(output);
            var shots = new Shots(output, work, Screen.AllScreens[Math.Min(screen, Screen.AllScreens.Length - 1)], moves, only) { LiveFolder = live };
            return shots.Run();
        }
    }

    /// <summary>The fake tree a test-mode launcher needs (as tests/e2e.ps1 makes it): nothing outside it is read or written.</summary>
    internal static class FakeTree
    {
        public static LauncherContext Create(string dir)
        {
            var data = Path.Combine(dir, "data");
            var saved = Path.Combine(dir, "saved");
            var steam = Path.Combine(dir, "steam");
            var game = Path.Combine(dir, "game", "evr-fake-game.exe");
            foreach (var d in new[] { data, Path.Combine(saved, "base"), Path.Combine(steam, "userdata", "111", "782330", "remote", "PROFILE"), Path.GetDirectoryName(game) })
                Directory.CreateDirectory(d);
            File.WriteAllText(Path.Combine(saved, "base", "DOOMEternalConfig.local"), "r_mode \"25\"\n");
            if (!File.Exists(game)) File.WriteAllBytes(game, new byte[0]);
            foreach (var b in new[] { "20261002-171338", "20261001-213010", "20260930-090502" })
                Directory.CreateDirectory(Path.Combine(data, "save-backups", b));
            var options = LauncherOptions.Parse(new[] { "--data-root", data, "--saved-games", saved, "--steam-root", steam, "--test-exe", game });
            return LauncherContext.Create(options, new Log());
        }
    }

    internal static class Reflect
    {
        private const BindingFlags Any = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;

        public static T Get<T>(object o, string name) => (T)o.GetType().GetField(name, Any).GetValue(o);

        public static void Set(object o, string name, object value)
        {
            var p = o.GetType().GetProperty(name, Any);
            if (p != null) p.SetValue(o, value);
            else o.GetType().GetField(name, Any).SetValue(o, value);
        }

        public static object Call(object o, string name, params object[] args) =>
            o.GetType().GetMethods(Any).First(m => m.Name == name && m.GetParameters().Length == args.Length).Invoke(o, args);
    }

    /// <summary>One window state to shoot: <see cref="Open"/> shows it (modal over the main window or not), <see cref="Prepare"/> sets it up.</summary>
    internal sealed class Scenario
    {
        public string Name;
        public Action Open;
        public Action<Form> Prepare;
    }

    internal static class ImageSave
    {
        public static void Png(System.Drawing.Bitmap bmp, string path) => bmp.Save(path, ImageFormat.Png);
    }
}
