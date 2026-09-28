using System;
using System.IO;

namespace EternalVR.Launcher
{
    /// <summary>The launcher log: <c>logs\launcher.log</c> in the data folder, plus listeners (window, console).</summary>
    public sealed class Log
    {
        private readonly object gate = new object();
        private string file;

        public event Action<string> Line;

        public void SetFile(string path)
        {
            lock (gate)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                file = path;
            }
        }

        public void Info(string message) => Write("INFO", message);
        public void Warn(string message) => Write("WARN", message);
        public void Error(string message) => Write("ERROR", message);

        private void Write(string level, string message)
        {
            var line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {level,-5} {message}";
            lock (gate)
            {
                if (file != null)
                {
                    try { File.AppendAllText(file, line + Environment.NewLine); }
                    catch (IOException) { }
                    catch (UnauthorizedAccessException) { }
                }
            }
            Line?.Invoke(line);
        }
    }
}
