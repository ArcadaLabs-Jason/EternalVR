using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Data;

namespace EternalVR.Launcher.Core.Launch
{
    public sealed class ForcedCvar
    {
        public ForcedCvar(string name, string value, bool stereoOnly = false)
        {
            Name = name;
            Value = value;
            StereoOnly = stereoOnly;
        }

        public string Name { get; }
        /// <summary>The value, or a placeholder the launch plan fills (<see cref="ForcedCvars.EyeWidth"/>, <see cref="ForcedCvars.EyeHeight"/>).</summary>
        public string Value { get; }
        /// <summary>Forced only in stereo mode.</summary>
        public bool StereoOnly { get; }
    }

    /// <summary>
    /// The one list of cvars a VR launch forces on the command line (T-092), kept as data in
    /// <c>data\forced-cvars.txt</c> (<c>name | value [| stereo]</c>). The same names, stereo ones
    /// included, are the keys the settings restore puts back after the session (T-036).
    /// </summary>
    public sealed class ForcedCvars
    {
        /// <summary>Placeholder values: the render size when known before the launch (<see cref="RenderSizeChoice"/>), else the stereo window's client size (<see cref="StereoWindow"/>).</summary>
        public const string EyeWidth = "$eye_width";
        public const string EyeHeight = "$eye_height";

        public ForcedCvars(IEnumerable<ForcedCvar> cvars) { All = cvars.ToList(); }

        public IReadOnlyList<ForcedCvar> All { get; }

        public IEnumerable<string> Names => All.Select(c => c.Name);

        /// <summary>The cvars one launch forces: every one in stereo, the others in mono.</summary>
        public IEnumerable<ForcedCvar> For(bool stereo) => All.Where(c => stereo || !c.StereoOnly);

        public static ForcedCvars Parse(string text)
        {
            var list = new List<ForcedCvar>();
            foreach (var r in DataFile.ParseRecords(text))
            {
                var name = DataFile.Field(r, 0);
                var value = DataFile.Field(r, 1);
                var when = DataFile.Field(r, 2);
                if (when.Length > 0 && !string.Equals(when, "stereo", StringComparison.OrdinalIgnoreCase))
                    throw new FormatException("forced-cvars: the third field of " + name + " is 'stereo' or nothing, not " + when);
                bool stereoOnly = when.Length > 0;
                if (value.StartsWith("$", StringComparison.Ordinal) && (!stereoOnly || (value != EyeWidth && value != EyeHeight)))
                    throw new FormatException("forced-cvars: " + value + " is not a placeholder of a stereo cvar");
                if (name.Length == 0 || name.StartsWith("+", StringComparison.Ordinal) || name.Contains(" "))
                    throw new FormatException("forced-cvars: bad cvar name: " + name);
                if (value.Length == 0) throw new FormatException("forced-cvars: no value for " + name);
                if (list.Any(c => string.Equals(c.Name, name, StringComparison.OrdinalIgnoreCase)))
                    throw new FormatException("forced-cvars: listed twice: " + name);
                list.Add(new ForcedCvar(name, value, stereoOnly));
            }
            return new ForcedCvars(list);
        }
    }
}
