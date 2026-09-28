using System;
using System.IO;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// A fresh folder per test under <c>EVR_TEST_TMP</c> (the rig points it at the development drive),
    /// else the system temp folder. Deleted on dispose.
    /// </summary>
    public sealed class TempDir : IDisposable
    {
        public TempDir()
        {
            var baseDir = Environment.GetEnvironmentVariable("EVR_TEST_TMP");
            if (string.IsNullOrEmpty(baseDir)) baseDir = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "evr-launcher-tests");
            Path = System.IO.Path.Combine(baseDir, Guid.NewGuid().ToString("N").Substring(0, 12));
            Directory.CreateDirectory(Path);
        }

        public string Path { get; }

        public string Combine(params string[] parts)
        {
            var all = new string[parts.Length + 1];
            all[0] = Path;
            parts.CopyTo(all, 1);
            return System.IO.Path.Combine(all);
        }

        /// <summary>Writes a file (creating folders) and returns its full path.</summary>
        public string Write(string relative, string text)
        {
            var full = Combine(relative.Split('/', '\\'));
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(full));
            File.WriteAllText(full, text);
            return full;
        }

        public string Read(string relative) => File.ReadAllText(Combine(relative.Split('/', '\\')));

        public void Dispose()
        {
            try { FileUtil.DeleteDirectory(Path); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }

    /// <summary>The shipped data files, copied next to the test assembly.</summary>
    public static class TestData
    {
        public static string Dir => System.IO.Path.Combine(AppContext.BaseDirectory, "data");

        public static string Read(string name) => File.ReadAllText(System.IO.Path.Combine(Dir, name));
    }
}
