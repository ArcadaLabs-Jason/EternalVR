using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Text;

namespace EternalVR.Launcher.Core.Preflight
{
    public enum LayerApi { Vulkan, OpenXR }

    /// <summary>An implicit layer found in the registry (read only) and its manifest.</summary>
    public sealed class InstalledLayer
    {
        public InstalledLayer(LayerApi api, string name, string manifestPath, string registryScope, bool enabledInRegistry,
                              string disableEnvName, string disableEnvValue)
        {
            Api = api;
            Name = name;
            ManifestPath = manifestPath;
            RegistryScope = registryScope;
            EnabledInRegistry = enabledInRegistry;
            DisableEnvName = disableEnvName;
            DisableEnvValue = disableEnvValue;
        }

        public LayerApi Api { get; }
        /// <summary>The layer name from the manifest; the manifest path when it could not be read.</summary>
        public string Name { get; }
        public string ManifestPath { get; }
        public string RegistryScope { get; }
        public bool EnabledInRegistry { get; }
        public string DisableEnvName { get; }
        public string DisableEnvValue { get; }

        /// <summary>
        /// Reads the names and the <c>disable_environment</c> of a manifest. Vulkan manifests carry
        /// <c>layer</c> or <c>layers[]</c> with an object <c>{"VAR": "value"}</c>; OpenXR manifests carry
        /// <c>api_layer</c> with the variable's name as a string (set to 1 to disable).
        /// </summary>
        public static IReadOnlyList<InstalledLayer> FromManifest(LayerApi api, string manifestPath, string manifestText,
                                                                 string registryScope, bool enabled)
        {
            var result = new List<InstalledLayer>();
            object root;
            try { root = MiniJson.Parse(manifestText); }
            catch (FormatException) { root = null; }

            var layerNodes = new List<object>();
            if (api == LayerApi.Vulkan)
            {
                if (MiniJson.Get(root, "layer") is Dictionary<string, object> one) layerNodes.Add(one);
                if (MiniJson.Get(root, "layers") is List<object> many) layerNodes.AddRange(many);
            }
            else if (MiniJson.Get(root, "api_layer") is Dictionary<string, object> xr)
            {
                layerNodes.Add(xr);
            }

            foreach (var node in layerNodes)
            {
                var name = MiniJson.Get(node, "name") as string ?? manifestPath;
                string envName = null, envValue = null;
                var disable = MiniJson.Get(node, "disable_environment");
                if (disable is string s) { envName = s; envValue = "1"; }
                else if (disable is Dictionary<string, object> d && d.Count > 0)
                {
                    envName = d.Keys.First();
                    envValue = d[envName] as string ?? "1";
                }
                result.Add(new InstalledLayer(api, name, manifestPath, registryScope, enabled, envName, envValue));
            }
            if (result.Count == 0) result.Add(new InstalledLayer(api, manifestPath, manifestPath, registryScope, enabled, null, null));
            return result;
        }
    }

    public enum LayerAction { Ignore, Warn, Disable, DisableUnlessVdxr }

    public sealed class KnownLayer
    {
        public KnownLayer(LayerApi api, string namePattern, LayerAction action, string reason)
        {
            Api = api;
            NamePattern = namePattern;
            Action = action;
            Reason = reason;
        }

        public LayerApi Api { get; }
        /// <summary>A layer name, or a prefix ending in '*'.</summary>
        public string NamePattern { get; }
        public LayerAction Action { get; }
        public string Reason { get; }

        public bool Matches(InstalledLayer layer)
        {
            if (layer.Api != Api) return false;
            if (NamePattern.EndsWith("*", StringComparison.Ordinal))
                return layer.Name.StartsWith(NamePattern.Substring(0, NamePattern.Length - 1), StringComparison.OrdinalIgnoreCase);
            return string.Equals(layer.Name, NamePattern, StringComparison.OrdinalIgnoreCase);
        }
    }

    public sealed class LayerDecision
    {
        public LayerDecision(InstalledLayer layer, LayerAction action, string message, KeyValuePair<string, string>? env)
        {
            Layer = layer;
            Action = action;
            Message = message;
            Environment = env;
        }

        public InstalledLayer Layer { get; }
        public LayerAction Action { get; }
        public string Message { get; }
        /// <summary>The variable set in the game's environment to disable the layer, if any.</summary>
        public KeyValuePair<string, string>? Environment { get; }
    }

    /// <summary>
    /// The known-layer list (T-095, T-110), data in <c>data\known-layers.txt</c>:
    /// <c>vulkan|openxr | name or prefix* | ignore|warn|disable|disable-unless-vdxr | reason</c>.
    /// </summary>
    public sealed class KnownLayers
    {
        public KnownLayers(IEnumerable<KnownLayer> layers) { All = layers.ToList(); }

        public IReadOnlyList<KnownLayer> All { get; }

        public static KnownLayers Parse(string text)
        {
            var list = new List<KnownLayer>();
            foreach (var r in DataFile.ParseRecords(text))
            {
                var api = DataFile.Field(r, 0).ToLowerInvariant();
                var action = DataFile.Field(r, 2).ToLowerInvariant();
                LayerApi parsedApi = api == "vulkan" ? LayerApi.Vulkan
                    : api == "openxr" ? LayerApi.OpenXR
                    : throw new FormatException("known-layers: unknown api " + api);
                LayerAction parsedAction;
                switch (action)
                {
                    case "ignore": parsedAction = LayerAction.Ignore; break;
                    case "warn": parsedAction = LayerAction.Warn; break;
                    case "disable": parsedAction = LayerAction.Disable; break;
                    case "disable-unless-vdxr": parsedAction = LayerAction.DisableUnlessVdxr; break;
                    default: throw new FormatException("known-layers: unknown action " + action);
                }
                list.Add(new KnownLayer(parsedApi, DataFile.Field(r, 1), parsedAction, DataFile.Field(r, 3)));
            }
            return new KnownLayers(list);
        }

        /// <summary>
        /// One decision per installed layer that is enabled in the registry. Unlisted layers are
        /// reported as "not tested with EternalVR" warnings; a listed layer whose manifest has no
        /// disable variable cannot be disabled and is a warning instead.
        /// </summary>
        public IReadOnlyList<LayerDecision> Evaluate(IEnumerable<InstalledLayer> installed, bool runtimeIsVdxr)
        {
            var decisions = new List<LayerDecision>();
            foreach (var layer in installed.Where(l => l.EnabledInRegistry))
            {
                var known = All.FirstOrDefault(k => k.Matches(layer));
                var api = layer.Api == LayerApi.Vulkan ? "Vulkan" : "OpenXR";
                if (known == null)
                {
                    decisions.Add(new LayerDecision(layer, LayerAction.Warn, $"{api} layer {layer.Name} is not on the known list (untested with EternalVR)", null));
                    continue;
                }
                var action = known.Action == LayerAction.DisableUnlessVdxr
                    ? (runtimeIsVdxr ? LayerAction.Ignore : LayerAction.Disable)
                    : known.Action;
                if (action == LayerAction.Disable && layer.DisableEnvName == null)
                {
                    decisions.Add(new LayerDecision(layer, LayerAction.Warn, $"{api} layer {layer.Name} should be off for VR ({known.Reason}) but its manifest has no disable variable", null));
                    continue;
                }
                var env = action == LayerAction.Disable
                    ? new KeyValuePair<string, string>(layer.DisableEnvName, layer.DisableEnvValue ?? "1")
                    : (KeyValuePair<string, string>?)null;
                var verb = action == LayerAction.Disable ? "disabled for this launch" : action == LayerAction.Warn ? "warning" : "ok";
                decisions.Add(new LayerDecision(layer, action, $"{api} layer {layer.Name}: {verb} ({known.Reason})", env));
            }
            return decisions;
        }
    }
}
