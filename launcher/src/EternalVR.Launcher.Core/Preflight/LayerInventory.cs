using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Text;

namespace EternalVR.Launcher.Core.Preflight
{
    public enum LayerKind { Implicit, Explicit }

    /// <summary>
    /// One registry value naming a layer manifest, as the Vulkan or OpenXR loader reads it (read only): the value's name is
    /// the manifest's path and a DWORD 0 enables it. A Vulkan layer the display driver registers (the adapter's
    /// <c>VulkanImplicitLayers</c>) has no flag and is always on.
    /// </summary>
    public sealed class LayerRegistration
    {
        public LayerRegistration(LayerApi api, LayerKind kind, string scope, string manifestPath, object value,
                                 bool is32Bit = false, bool driver = false)
        {
            Api = api;
            Kind = kind;
            Scope = scope;
            ManifestPath = manifestPath;
            Value = value;
            Is32Bit = is32Bit;
            Driver = driver;
        }

        public LayerApi Api { get; }
        public LayerKind Kind { get; }
        /// <summary>"HKLM", "HKCU", "HKLM 32-bit" (WOW6432Node) or "driver".</summary>
        public string Scope { get; }
        public string ManifestPath { get; }
        /// <summary>The registry value (a DWORD reads as int); unused for a driver's layer.</summary>
        public object Value { get; }
        /// <summary>Registered for 32-bit programs only (WOW6432Node): the 64-bit game never loads it.</summary>
        public bool Is32Bit { get; }
        public bool Driver { get; }

        public bool Enabled => Driver || (Value is int v && v == 0);

        public string State => Driver ? "always on" : Value is int v ? (v == 0 ? "enabled" : "disabled (" + v + ")") : "disabled (not a DWORD)";
    }

    /// <summary>One layer of a manifest: its name, library and environment switches.</summary>
    public sealed class ManifestLayer
    {
        public string Name { get; set; }
        public string LibraryPath { get; set; }
        /// <summary>A Vulkan manifest's <c>library_arch</c> ("32" or "64"); null when it has none.</summary>
        public string LibraryArch { get; set; }
        public string EnableEnvName { get; set; }
        public string EnableEnvValue { get; set; }
        public string DisableEnvName { get; set; }
        public string DisableEnvValue { get; set; }
    }

    /// <summary>A registered manifest as read: its text and layers, or why it could not be read.</summary>
    public sealed class RegisteredManifest
    {
        public RegisteredManifest(LayerRegistration registration, string text, string problem, IReadOnlyList<ManifestLayer> layers)
        {
            Registration = registration;
            Text = text;
            Problem = problem;
            Layers = layers;
        }

        public LayerRegistration Registration { get; }
        /// <summary>The manifest's text; null when it is missing or could not be read.</summary>
        public string Text { get; }
        /// <summary>Null, or why the loader cannot use it ("manifest missing", "not valid JSON", ...).</summary>
        public string Problem { get; }
        public IReadOnlyList<ManifestLayer> Layers { get; }
    }

    /// <summary>
    /// Every OpenXR API layer and Vulkan layer registered on the machine, for the report's system.txt and the launch's log
    /// line per layer that also loads with the game (overlays and capture tools hook every frame and can slow the stream).
    /// The registry and the manifests are read only, by the Windows side; the rules here are the loaders' own.
    /// </summary>
    public static class LayerInventory
    {
        /// <summary>The most manifests read per API; past it they are counted, not read.</summary>
        public const int MostRead = 200;
        /// <summary>The most manifests listed per API in system.txt.</summary>
        public const int MostListed = 40;
        /// <summary>The most launch log lines.</summary>
        public const int MostLogged = 12;
        /// <summary>A larger manifest is not read (layer manifests are a few KB).</summary>
        public const long MostManifestBytes = 256 * 1024;

        public static readonly string[] OpenXrVariables = { "XR_ENABLE_API_LAYERS", "XR_API_LAYER_PATH", "XR_RUNTIME_JSON" };
        public static readonly string[] VulkanVariables =
        {
            "VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE", "VK_LOADER_LAYERS_DISABLE", "VK_ADD_IMPLICIT_LAYER_PATH",
            "VK_ADD_LAYER_PATH", "VK_IMPLICIT_LAYER_PATH", "VK_LAYER_PATH",
        };

        /// <summary>
        /// Reads each registration's manifest through <paramref name="readManifest"/> (null: missing; it may throw on a file
        /// it cannot read), at most <see cref="MostRead"/> per API. A manifest that cannot be read or parsed, for whatever
        /// reason, is that manifest's problem: nothing throws out of here (it runs in every launch's checks).
        /// </summary>
        public static IReadOnlyList<RegisteredManifest> Read(IEnumerable<LayerRegistration> registrations, Func<string, string> readManifest)
        {
            var result = new List<RegisteredManifest>();
            var read = new Dictionary<LayerApi, int>();
            foreach (var r in registrations ?? Enumerable.Empty<LayerRegistration>())
            {
                read.TryGetValue(r.Api, out var n);
                read[r.Api] = n + 1;
                if (n >= MostRead)
                {
                    result.Add(new RegisteredManifest(r, null, "not read (more than " + MostRead + " registered)", new ManifestLayer[0]));
                    continue;
                }
                RegisteredManifest m;
                try
                {
                    var text = readManifest(r.ManifestPath);
                    m = text == null ? new RegisteredManifest(r, null, "manifest missing", new ManifestLayer[0]) : Parse(r, text);
                }
                catch (Exception e) when (!(e is OutOfMemoryException))
                {
                    var problem = e is FileNotFoundException || e is DirectoryNotFoundException ? "manifest missing" : "manifest unreadable: " + e.Message;
                    m = new RegisteredManifest(r, null, problem, new ManifestLayer[0]);
                }
                result.Add(m);
            }
            return result;
        }

        private static RegisteredManifest Parse(LayerRegistration r, string text)
        {
            object root;
            try { root = MiniJson.Parse(text); }
            catch (FormatException) { return new RegisteredManifest(r, text, "not valid JSON", new ManifestLayer[0]); }
            var nodes = new List<object>();
            if (r.Api == LayerApi.Vulkan)
            {
                if (MiniJson.Get(root, "layer") is Dictionary<string, object> one) nodes.Add(one);
                if (MiniJson.Get(root, "layers") is List<object> many) nodes.AddRange(many.OfType<Dictionary<string, object>>());
            }
            else if (MiniJson.Get(root, "api_layer") is Dictionary<string, object> xr)
            {
                nodes.Add(xr);
            }
            var layers = nodes.Select(node =>
            {
                var l = new ManifestLayer
                {
                    Name = MiniJson.Get(node, "name") as string ?? r.ManifestPath,
                    LibraryPath = MiniJson.Get(node, "library_path") as string,
                    LibraryArch = MiniJson.Get(node, "library_arch") as string,
                };
                ReadSwitch(MiniJson.Get(node, "enable_environment"), out var enName, out var enValue);
                ReadSwitch(MiniJson.Get(node, "disable_environment"), out var disName, out var disValue);
                l.EnableEnvName = enName;
                l.EnableEnvValue = enValue;
                l.DisableEnvName = disName;
                l.DisableEnvValue = disValue;
                return l;
            }).ToList();
            return new RegisteredManifest(r, text, layers.Count == 0 ? "no layer in it" : null, layers);
        }

        // Vulkan: {"VAR": "value"} (the first entry); OpenXR: the variable's name (any value counts).
        private static void ReadSwitch(object node, out string name, out string value)
        {
            name = null;
            value = null;
            if (node is string s && s.Length > 0) name = s;
            else if (node is Dictionary<string, object> d && d.Count > 0)
            {
                name = d.Keys.First();
                value = d[name] as string ?? string.Empty;
            }
        }

        /// <summary>
        /// The implicit layers of the 64-bit registry keys, for <see cref="KnownLayers.Evaluate"/>: a manifest that cannot be
        /// read cannot be loaded either, so it counts as disabled (named by its path).
        /// </summary>
        public static IReadOnlyList<InstalledLayer> Implicit(IEnumerable<RegisteredManifest> manifests)
        {
            var result = new List<InstalledLayer>();
            foreach (var m in manifests ?? Enumerable.Empty<RegisteredManifest>())
            {
                var r = m.Registration;
                if (r.Kind != LayerKind.Implicit || r.Is32Bit || r.Driver) continue;
                result.AddRange(m.Text == null
                    ? new[] { new InstalledLayer(r.Api, r.ManifestPath, r.ManifestPath, r.Scope, false, null, null) }
                    : InstalledLayer.FromManifest(r.Api, r.ManifestPath, m.Text, r.Scope, r.Enabled));
            }
            return result;
        }

        /// <summary>The variables the launch sets to turn layers off (<see cref="LayerAction.Disable"/>).</summary>
        public static IReadOnlyList<KeyValuePair<string, string>> LaunchDisables(IEnumerable<LayerDecision> decisions) =>
            (decisions ?? Enumerable.Empty<LayerDecision>())
                .Where(d => d.Action == LayerAction.Disable && d.Environment.HasValue)
                .Select(d => d.Environment.Value)
                .ToList();

        /// <summary>
        /// Whether the layer loads into a 64-bit game started with <paramref name="env"/> (the loaders' rules), and why not:
        /// null when it loads, else a short reason.
        /// </summary>
        public static string NotLoaded(RegisteredManifest m, ManifestLayer l, Func<string, string> env, out bool needsEnable)
        {
            needsEnable = false;
            var r = m.Registration;
            if (r.Is32Bit) return "32-bit only";
            if (!r.Enabled) return "disabled in the registry";
            if (m.Problem != null) return m.Problem;
            if (r.Api == LayerApi.Vulkan && l.LibraryArch == "32") return "32-bit only (library_arch)";
            // Both loaders drop an implicit layer without one while reading its manifest, before any variable counts (Vulkan:
            // LoaderLayerInterface.md, LLP_LAYER_9).
            if (r.Kind == LayerKind.Implicit && l.DisableEnvName == null)
                return "no disable_environment (the " + (r.Api == LayerApi.Vulkan ? "Vulkan" : "OpenXR") + " loader skips it)";
            if (r.Api == LayerApi.Vulkan)
            {
                if (MatchesFilter(env("VK_LOADER_LAYERS_ENABLE"), l.Name, r.Kind)) return null;
                if (MatchesFilter(env("VK_LOADER_LAYERS_DISABLE"), l.Name, r.Kind)) return "off through VK_LOADER_LAYERS_DISABLE";
            }
            if (r.Kind == LayerKind.Explicit)
            {
                var list = env(r.Api == LayerApi.Vulkan ? "VK_INSTANCE_LAYERS" : "XR_ENABLE_API_LAYERS");
                return Named(list, l.Name) ? null : "explicit: loads only when named";
            }
            if (l.DisableEnvName != null && !string.IsNullOrEmpty(env(l.DisableEnvName))) return "off: " + l.DisableEnvName + " is set";
            if (l.EnableEnvName != null)
            {
                var value = env(l.EnableEnvName);
                bool on = r.Api == LayerApi.Vulkan ? value != null && value == (l.EnableEnvValue ?? string.Empty) : !string.IsNullOrEmpty(value);
                if (!on)
                {
                    needsEnable = true;
                    return "only with " + EnableText(l) + " (not set)";
                }
            }
            return null;
        }

        private static string EnableText(ManifestLayer l) =>
            l.EnableEnvName + (string.IsNullOrEmpty(l.EnableEnvValue) ? string.Empty : "=" + l.EnableEnvValue);

        private static bool Named(string list, string name) =>
            !string.IsNullOrEmpty(list) && list.Split(';').Any(n => string.Equals(n.Trim(), name, StringComparison.Ordinal));

        // VK_LOADER_LAYERS_ENABLE / _DISABLE: comma-separated names with '*' at either end, ~all~, ~implicit~, ~explicit~.
        private static bool MatchesFilter(string filter, string name, LayerKind kind)
        {
            if (string.IsNullOrEmpty(filter)) return false;
            foreach (var raw in filter.Split(','))
            {
                var f = raw.Trim();
                if (f.Length == 0) continue;
                if (f == "~all~" || (f == "~implicit~" && kind == LayerKind.Implicit) || (f == "~explicit~" && kind == LayerKind.Explicit)) return true;
                bool head = f.StartsWith("*", StringComparison.Ordinal), tail = f.EndsWith("*", StringComparison.Ordinal) && f.Length > 1;
                var core = f.Trim('*');
                if (core.Length == 0) continue;
                if (head && tail ? name.IndexOf(core, StringComparison.OrdinalIgnoreCase) >= 0
                    : head ? name.EndsWith(core, StringComparison.OrdinalIgnoreCase)
                    : tail ? name.StartsWith(core, StringComparison.OrdinalIgnoreCase)
                    : string.Equals(name, core, StringComparison.OrdinalIgnoreCase)) return true;
            }
            return false;
        }

        private static string ApiKey(LayerApi api) => api == LayerApi.Vulkan ? "vulkan" : "openxr";

        /// <summary>
        /// The report's lines (system.txt): per API a count, a line per registered manifest (its layers, where and how it is
        /// registered, its library and switches, whether it loads with a launch), at most <see cref="MostListed"/>, and the
        /// loader variables set in the launcher's environment. <paramref name="launchEnv"/> is the environment a launch
        /// would give the game; <paramref name="launcherEnv"/> the launcher's own. Whether a layer loads is a prediction: the
        /// loaders' settings files, path variables and version checks are not followed. A name registered twice (HKLM and
        /// HKCU) counts once, as the loader loads it once.
        /// </summary>
        public static IReadOnlyList<KeyValuePair<string, string>> ReportLines(IReadOnlyList<RegisteredManifest> manifests,
            Func<string, string> launchEnv, Func<string, string> launcherEnv)
        {
            var lines = new List<KeyValuePair<string, string>>();
            void Add(string key, string value) => lines.Add(new KeyValuePair<string, string>(key, value));
            foreach (var api in new[] { LayerApi.OpenXR, LayerApi.Vulkan })
            {
                var key = ApiKey(api);
                var list = (manifests ?? new RegisteredManifest[0]).Where(m => m.Registration.Api == api).ToList();
                int loading = list.SelectMany(m => m.Layers.Where(l => NotLoaded(m, l, launchEnv, out _) == null).Select(l => l.Name))
                    .Distinct(StringComparer.Ordinal).Count();
                Add(key + " layers", list.Count == 0 ? "none registered"
                    : $"{list.Count} manifest(s) registered, {loading} layer(s) load with a launch "
                      + "(prediction from the registry and the launch environment; the layer log shows what loaded)");
                foreach (var m in list.Take(MostListed)) Add(key + " layer", Describe(m, launchEnv));
                if (list.Count > MostListed) Add(key + " layers left out", $"{list.Count - MostListed} more (the first {MostListed} are listed)");
                var names = api == LayerApi.OpenXR ? OpenXrVariables : VulkanVariables;
                var set = names.Where(n => !string.IsNullOrEmpty(launcherEnv(n))).Select(n => n + "=" + launcherEnv(n)).ToList();
                Add(key + " layer environment", set.Count == 0 ? "none set (" + string.Join(", ", names) + ")" : string.Join("; ", set));
            }
            return lines;
        }

        private static string Describe(RegisteredManifest m, Func<string, string> env)
        {
            var r = m.Registration;
            var where = (r.Kind == LayerKind.Implicit ? "implicit" : "explicit") + ", " + r.Scope + ", " + r.State;
            if (m.Layers.Count == 0) return $"{r.ManifestPath} ({where}): {m.Problem ?? "no layer"}";
            var parts = m.Layers.Select(l =>
            {
                var why = NotLoaded(m, l, env, out _);
                var verdict = why != null ? "not loaded, " + why
                    : r.Driver ? "registered by a display driver; loads if that adapter is present" : "loads with a launch";
                var s = l.Name + " (" + where + "): " + verdict;
                if (!string.IsNullOrEmpty(l.LibraryPath)) s += "; library " + l.LibraryPath;
                if (l.DisableEnvName != null) s += "; off with " + l.DisableEnvName + (string.IsNullOrEmpty(l.DisableEnvValue) ? string.Empty : "=" + l.DisableEnvValue);
                if (l.EnableEnvName != null) s += "; on only with " + EnableText(l);
                return s;
            });
            return string.Join(" | ", parts) + "; manifest " + r.ManifestPath;
        }

        /// <summary>
        /// The launch's log lines: one per layer other than EternalVR's and the display driver's that should also load into the
        /// game with <paramref name="gameEnv"/> (a name registered twice once), at most <see cref="MostLogged"/>. Layers that
        /// load only with their enable variable (Steam's overlay outside Steam) are in system.txt only.
        /// </summary>
        public static IReadOnlyList<string> LaunchLines(IReadOnlyList<RegisteredManifest> manifests, Func<string, string> gameEnv)
        {
            var lines = new List<string>();
            var named = new HashSet<string>(StringComparer.Ordinal);
            foreach (var m in manifests ?? new RegisteredManifest[0])
            {
                if (m.Registration.Driver) continue;
                foreach (var l in m.Layers)
                {
                    if (l.Name.StartsWith("VK_LAYER_ETERNALVR", StringComparison.OrdinalIgnoreCase)) continue;
                    if (NotLoaded(m, l, gameEnv, out _) != null) continue;
                    if (!named.Add(ApiKey(m.Registration.Api) + " " + l.Name)) continue;
                    var api = m.Registration.Api == LayerApi.Vulkan ? "A Vulkan" : "An OpenXR";
                    lines.Add($"{api} layer should also load with the game: {l.Name} ({m.Registration.ManifestPath})");
                }
            }
            if (lines.Count <= MostLogged) return lines;
            var kept = lines.Take(MostLogged).ToList();
            kept.Add($"... and {lines.Count - MostLogged} more such layer(s) (an exported report's system.txt lists every layer)");
            return kept;
        }
    }
}
