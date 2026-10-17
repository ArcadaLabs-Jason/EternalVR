# The rig QA suite's Steam-Cloud files (qa-suite.ps1), dot-sourced after qa-common.ps1.
#
# A game scenario's stop keeps the cloud files the game wrote (PROFILE\profile.bin, the save slot its map start
# autosaves into): the game wrote them through Steam, so Steam's record matches them and the next launch reads
# them as the game left them (tools\rig\README.md, Steam Cloud). Before its first game run the suite holds a cloud
# restore of the settings as they are then (cleanup.ps1 -HoldCloudRestore: a pending <stamp>-cloud-restore run
# folder that implicit cleanups leave alone while the suite's process lives); after the last game run it finishes
# that restore by name (the restore, the one resync launch, Steam's record checked) instead of restoring after
# every scenario. A suite that ends any other way (an error, Ctrl+C, a killed host) leaves it pending and the next
# cleanup or run start finishes it; on STOP-RIG it is left for that too, so no resync launch starts while the owner
# uses the PC. What a run carries to the next one is checked after every scenario from the run folder's
# config-before\ and config-after\ copies of profile.bin:
#   - the fields the game itself rewrites in every run carry over: its lifetime statistics (STAT_*), the telemetry
#     queue and its event windows (count, start, end and type only with the values those hold, see
#     $script:QaProfileCarriedValues), the online progression and item store, and r_antialiasing and
#     r_raytracedReflectionsTemporalUpscaleQuality (the first is the per-eye TAA's held 0, which every stereo run
#     saves; the second the game's own Reflections value): with either one carried the game runs with the same
#     values as from the profile the suite started with (rig: r_antialiasing 1 until the layer's hold, the
#     reflections 1);
#   - a text field outside that list that changed and is named like a cvar (lowercase prefix, underscore: the
#     serializer writes each cvar under its own name) is a held value saved into the player's profile: the scenario
#     fails (a profile leak, docs/rig-findings/profile-leak.md); the layer holds only cvars, so a changed field with
#     another name (the online item store's ItemInstanceId, merge QA r63a) cannot be one and only sends the files
#     back;
#   - a profile that cannot be read, or one the run added or removed, fails it too (nothing shows it held no value);
#   - any other field that changed (a tutorial marked as seen, a field the parse cannot name), any of the above,
#     or a Steam record that does not match the files: the cloud files go back before the next scenario, so it
#     starts as the first one did.
# The save slot carries the previous run's autosave: a +map run starts from the developer loadout at the map's
# own difficulty whatever the slot holds (qconsole.log: haveDevLoadout 1; the autosave's game.details:
# difficulty=1 after a slot holding the Nightmare 100% save), and only the main menu's save list reads it.

if (-not ('QaProfile' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
public static class QaProfile {
    [StructLayout(LayoutKind.Sequential)]
    struct AuthInfo {
        public int cbSize; public int dwInfoVersion;
        public IntPtr pbNonce; public int cbNonce;
        public IntPtr pbAuthData; public int cbAuthData;
        public IntPtr pbTag; public int cbTag;
        public IntPtr pbMacContext; public int cbMacContext;
        public int cbAAD; public long cbData; public int dwFlags;
    }
    [DllImport("bcrypt.dll", CharSet = CharSet.Unicode)] static extern int BCryptOpenAlgorithmProvider(out IntPtr alg, string id, string impl, int flags);
    [DllImport("bcrypt.dll", CharSet = CharSet.Unicode)] static extern int BCryptSetProperty(IntPtr obj, string prop, byte[] input, int size, int flags);
    [DllImport("bcrypt.dll")] static extern int BCryptGenerateSymmetricKey(IntPtr alg, out IntPtr key, IntPtr keyObj, int keyObjSize, byte[] secret, int secretSize, int flags);
    [DllImport("bcrypt.dll")] static extern int BCryptDecrypt(IntPtr key, byte[] input, int inputSize, ref AuthInfo info, byte[] iv, int ivSize, byte[] output, int outputSize, out int result, int flags);
    [DllImport("bcrypt.dll")] static extern int BCryptDestroyKey(IntPtr key);
    [DllImport("bcrypt.dll")] static extern int BCryptCloseAlgorithmProvider(IntPtr alg, int flags);

    // profile.bin as the game writes it on Steam: AES-128-GCM, key = SHA-256("<SteamID64>MANCUBUSprofile.bin")[0..16),
    // the same text as the associated data, the 12-byte nonce first and the 16-byte tag last; the plain text starts
    // with a9 0d 8d aa (docs/rig-findings/profile-leak.md).
    public static byte[] Decrypt(string path, ulong steamId64) {
        byte[] data = System.IO.File.ReadAllBytes(path);
        if (data.Length < 12 + 16 + 12) throw new Exception("profile.bin is too short (" + data.Length + " bytes)");
        byte[] aad = Encoding.ASCII.GetBytes(steamId64.ToString() + "MANCUBUSprofile.bin");
        byte[] key = new byte[16];
        using (var sha = SHA256.Create()) Array.Copy(sha.ComputeHash(aad), key, 16);
        byte[] nonce = new byte[12], tag = new byte[16], cipher = new byte[data.Length - 28];
        Array.Copy(data, 0, nonce, 0, 12);
        Array.Copy(data, 12, cipher, 0, cipher.Length);
        Array.Copy(data, data.Length - 16, tag, 0, 16);
        byte[] plain = new byte[cipher.Length];
        IntPtr alg, hkey;
        int st = BCryptOpenAlgorithmProvider(out alg, "AES", null, 0);
        if (st != 0) throw new Exception("BCryptOpenAlgorithmProvider 0x" + st.ToString("X8"));
        GCHandle hn = GCHandle.Alloc(nonce, GCHandleType.Pinned), ha = GCHandle.Alloc(aad, GCHandleType.Pinned), ht = GCHandle.Alloc(tag, GCHandleType.Pinned);
        try {
            byte[] mode = Encoding.Unicode.GetBytes("ChainingModeGCM\0");
            st = BCryptSetProperty(alg, "ChainingMode", mode, mode.Length, 0);
            if (st != 0) throw new Exception("BCryptSetProperty 0x" + st.ToString("X8"));
            st = BCryptGenerateSymmetricKey(alg, out hkey, IntPtr.Zero, 0, key, key.Length, 0);
            if (st != 0) throw new Exception("BCryptGenerateSymmetricKey 0x" + st.ToString("X8"));
            try {
                var info = new AuthInfo();
                info.cbSize = Marshal.SizeOf(typeof(AuthInfo)); info.dwInfoVersion = 1;
                info.pbNonce = hn.AddrOfPinnedObject(); info.cbNonce = nonce.Length;
                info.pbAuthData = ha.AddrOfPinnedObject(); info.cbAuthData = aad.Length;
                info.pbTag = ht.AddrOfPinnedObject(); info.cbTag = tag.Length;
                int got;
                st = BCryptDecrypt(hkey, cipher, cipher.Length, ref info, null, 0, plain, plain.Length, out got, 0);
                if (st != 0) throw new Exception("not this account's profile, or not intact (BCryptDecrypt 0x" + st.ToString("X8") + ")");
            } finally { BCryptDestroyKey(hkey); }
        } finally { hn.Free(); ha.Free(); ht.Free(); BCryptCloseAlgorithmProvider(alg, 0); }
        if (plain.Length < 12 || plain[0] != 0xa9 || plain[1] != 0x0d || plain[2] != 0x8d || plain[3] != 0xaa) throw new Exception("profile.bin decrypted, but not a profile");
        return plain;
    }

    static bool NameByte(byte c, bool first) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (!first && c >= '0' && c <= '9');
    }

    // The profile's fields: <2 x name length><name><type byte><value>, the value up to the next field. Returns
    // name -> its values (type byte and value as hex, one per occurrence). A field is found wherever those bytes
    // fit, so a run of bytes inside a binary value can read as a short field of its own; both copies of a profile
    // are read the same way, so a change is still found, under that name.
    public static Dictionary<string, List<string>> Fields(byte[] p) {
        var starts = new List<int>(); var names = new List<string>();
        for (int i = 0; i + 2 < p.Length; ++i) {
            int len = p[i];
            // Names of one character are left out: those bytes are a short value and the next field's start
            // ("numOwnedItems" 02 'D' 01 ...), and the value is then counted under its own field.
            if (len < 4 || (len & 1) != 0) continue;
            int n = len / 2;
            if (i + 1 + n >= p.Length || p[i + 1 + n] >= 0x10) continue;
            bool ok = true;
            for (int j = 0; j < n && ok; ++j) ok = NameByte(p[i + 1 + j], j == 0);
            if (!ok) continue;
            starts.Add(i); names.Add(Encoding.ASCII.GetString(p, i + 1, n));
        }
        var d = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        for (int k = 0; k < starts.Count; ++k) {
            int from = starts[k] + 1 + names[k].Length;
            int to = k + 1 < starts.Count ? starts[k + 1] : p.Length;
            List<string> list;
            if (!d.TryGetValue(names[k], out list)) { list = new List<string>(); d[names[k]] = list; }
            // A field found inside another one's name ends that one's value where it starts: empty.
            list.Add(to > from ? BitConverter.ToString(p, from, to - from).Replace("-", "") : "");
        }
        foreach (var l in d.Values) l.Sort(StringComparer.Ordinal);
        return d;
    }

    // A text value (type 0x0a, <2 x length><ASCII>) as its text, else null.
    public static string Text(string hex) {
        if (hex.Length < 4 || !hex.StartsWith("0A")) return null;
        int len = Convert.ToInt32(hex.Substring(2, 2), 16);
        if ((len & 1) != 0 || hex.Length != 4 + len) return null;
        var sb = new StringBuilder();
        for (int i = 4; i < hex.Length; i += 2) {
            int c = Convert.ToInt32(hex.Substring(i, 2), 16);
            if (c < 32 || c > 126) return null;
            sb.Append((char)c);
        }
        return sb.ToString();
    }
}
'@
}

# The fields the game rewrites in every run, carried from one scenario to the next (see above).
$script:QaProfileCarried = '^(STAT_\w+|t_ghost_\w+|discardedTelemetryEvents|set_progression|idWarehouseManager|numOwnedItems|ownedItem_\d+|r_antialiasing|r_raytracedReflectionsTemporalUpscaleQuality)$'
# Names too plain to carry by name: the telemetry events' count, start, end and type. The parse is flat (it finds
# fields by their bytes, not by the profile's nesting), so the section a field sits in is not known; these carry
# only with the values the telemetry holds there: count a number (not text); start and end a UTC time stamp and type
# an event or read-receipt name when the value is text (the same names also hold short numbers there, which carry).
# The same name with any other text is judged like any other field.
$script:QaProfileCarriedValues = @{
    count = $null
    start = '^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(\.\d+)?Z$'
    end   = '^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(\.\d+)?Z$'
    type  = '^(t_ghost_\w+|READ_RECEIPT_TYPE_\w+)$'
}
# A cvar's name as the profile stores it: a lowercase prefix and an underscore (r_sharpening, view_skipShakes).
$script:QaProfileCvarName = '^[a-z][a-z0-9]*_[A-Za-z0-9_]+$'
# The suite's held cloud restore (Start-QaCloudHold): the run folder its end finishes.
$script:QaCloudHeld = $null

# A changed field the game rewrites every run (see $script:QaProfileCarried and $script:QaProfileCarriedValues).
function Test-QaProfileCarried([string]$Name, [string[]]$Values) {
    if ($Name -cmatch $script:QaProfileCarried) { return $true }
    if (-not $script:QaProfileCarriedValues.ContainsKey($Name)) { return $false }
    $shape = $script:QaProfileCarriedValues[$Name]
    foreach ($v in $Values) {
        $text = [QaProfile]::Text($v)
        if ($null -eq $shape) { if ($null -ne $text) { return $false } }
        elseif ($null -ne $text -and $text -cnotmatch $shape) { return $false }
    }
    return $true
}

function Format-QaProfileValue([string[]]$Values) {
    $shown = @($Values | ForEach-Object {
        $text = [QaProfile]::Text($_)
        if ($null -ne $text) { "'$text'" } elseif ($_.Length -le 24) { "0x$_" } else { "$($_.Length / 2) bytes" }
    })
    if ($shown.Count -eq 0) { return '(none)' }
    return ($shown -join ' ')
}

# The fields that differ between two decoded copies of a profile: Name, Before, After, Text (a text value on
# either side) and Carried.
function Compare-QaProfile($Before, $After) {
    $out = @()
    foreach ($name in (@($Before.Keys) + @($After.Keys) | Sort-Object -Unique)) {
        $a = @(); if ($Before.ContainsKey($name)) { $a = @($Before[$name]) }
        $b = @(); if ($After.ContainsKey($name)) { $b = @($After[$name]) }
        if (($a -join ',') -ceq ($b -join ',')) { continue }
        $text = [bool](@(@($a) + @($b) | Where-Object { $null -ne [QaProfile]::Text($_) }).Count)
        $out += [pscustomobject]@{ Name = $name; Before = $a; After = $b; Text = $text; Carried = (Test-QaProfileCarried $name (@($a) + @($b))) }
    }
    return $out
}

# After a game scenario's stop: what its run left in the cloud files for the next one. Sets $Ctx.CloudRestore when
# the next scenario must not start from it.
function Test-QaCloudState($Ctx) {
    if (-not $Ctx.RunDir -or -not (Test-Path -LiteralPath (Join-Path $Ctx.RunDir 'config-after'))) {
        Add-QaResult $Ctx 'profile.bin: no held value saved' $false 'no config-after in the run folder (the cleanup did not finish)'
        $Ctx.CloudRestore = $true
        return
    }
    $cleanup = $null
    try { $cleanup = Get-Content -LiteralPath (Join-Path $Ctx.RunDir 'cleanup.json') -Raw | ConvertFrom-Json } catch { }
    $stale = @(if ($cleanup) { @($cleanup.cloudStale) | Where-Object { $_ } })
    Add-QaResult $Ctx 'Steam''s record matches the cloud files' ($null -ne $cleanup -and $stale.Count -eq 0) $(if (-not $cleanup) { 'no cleanup.json' } else { "$($stale.Count) stale: $(@($stale | ForEach-Object { $_.key }) -join ', ')" })
    if (-not $cleanup -or $stale.Count -gt 0) { $Ctx.CloudRestore = $true }

    $leaks = @(); $others = @(); $carried = 0; $read = 0
    # ConvertFrom-Json returns the array as one object: foreach enumerates it.
    $locations = Get-Content -LiteralPath (Join-Path $Ctx.RunDir 'config-before\locations.json') -Raw | ConvertFrom-Json
    foreach ($loc in $locations) {
        if ($loc.kind -ne 'steam-remote') { continue }
        $before = Join-Path $Ctx.RunDir "config-before\$($loc.name)\PROFILE\profile.bin"
        $after = Join-Path $Ctx.RunDir "config-after\$($loc.name)\PROFILE\profile.bin"
        $hasBefore = Test-Path -LiteralPath $before; $hasAfter = Test-Path -LiteralPath $after
        if (-not $hasBefore -and -not $hasAfter) { continue }
        if ($hasBefore -and $hasAfter -and (Get-FileHash -LiteralPath $before).Hash -eq (Get-FileHash -LiteralPath $after).Hash) { continue }
        # Fail closed: a profile the run added or removed, or one that cannot be read, cannot show it holds no leak.
        if ($hasBefore -ne $hasAfter) { $leaks += "$($loc.name): profile.bin $(if ($hasAfter) { 'added' } else { 'removed' }) by the run"; continue }
        try {
            $id = [uint64](76561197960265728 + [uint64]($loc.name -replace '^steam-', ''))
            $changes = @(Compare-QaProfile ([QaProfile]::Fields([QaProfile]::Decrypt($before, $id))) ([QaProfile]::Fields([QaProfile]::Decrypt($after, $id))))
            $read++
        } catch {
            $leaks += "$($loc.name): profile.bin not read ($($_.Exception.Message))"
            continue
        }
        foreach ($c in $changes) {
            $line = "$($loc.name) $($c.Name) $(Format-QaProfileValue $c.Before) -> $(Format-QaProfileValue $c.After)"
            if ($c.Carried) { $carried++ }
            elseif ($c.Text -and $c.Name -cmatch $script:QaProfileCvarName) { $leaks += "text field changed: $line" }
            else { $others += $line }
        }
    }
    Add-QaResult $Ctx 'profile.bin: no held value saved' ($leaks.Count -eq 0) $(if ($leaks.Count) { "$($leaks -join '; ')" } else { "$read profile(s) read, $carried field(s) the game rewrites every run" })
    if ($others.Count -gt 0) { Add-QaNote $Ctx "profile.bin: $($others -join '; '); the Steam Cloud files go back before the next scenario" }
    if ($leaks.Count -gt 0 -or $others.Count -gt 0) { $Ctx.CloudRestore = $true }
}

# The command that finishes the held restore by hand: what qa.log and the report name when the suite leaves it.
function Get-QaCloudCommand([string]$Leaf) {
    return "powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\cleanup.ps1 -Run $Leaf"
}

# Holds a cloud restore of the settings as they are now, for the runs that follow (cleanup.ps1 -HoldCloudRestore,
# which finishes what an earlier suite left pending first). Returns Ok and Detail; sets $script:QaCloudHeld.
function Start-QaCloudHold([string]$OutFile) {
    $out = & (Join-Path $script:QaRig 'cleanup.ps1') -HoldCloudRestore -HeldBy 'the rig QA suite' *>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    if ($OutFile) { Add-Content -LiteralPath $OutFile -Value $out -Encoding utf8 }
    $line = @($out | Where-Object { $_ -match 'cloud restore held: (.+?) \(finish it' }) | Select-Object -Last 1
    if ($code -ne 0 -or -not ($line -match 'cloud restore held: (.+?) \(finish it')) {
        $why = @($out | Where-Object { $_ -match 'REFUSED|ERROR' }) | Select-Object -Last 1
        return [pscustomobject]@{ Ok = $false; Detail = "cleanup.ps1 -HoldCloudRestore exit $code$(if ($why) { ': ' + ($why -replace '^.*?\] ', '') })" }
    }
    $script:QaCloudHeld = $Matches[1].Trim()
    $leaf = Split-Path -Leaf $script:QaCloudHeld
    Write-QaLine "  Steam Cloud files held as they are now: $leaf (the suite puts them back after its last game run; if it ends otherwise, the next cleanup does, or: $(Get-QaCloudCommand $leaf))"
    return [pscustomobject]@{ Ok = $true; Detail = $leaf }
}

# Finishes the held restore by name: the cloud files back as they were when it was held, the resync launch, Steam's
# record checked (cleanup.ps1 -Run <folder>). Returns Ok and Detail; the script's output goes to $OutFile.
function Invoke-QaCloudRestore([string]$OutFile) {
    if (-not $script:QaCloudHeld) { return [pscustomobject]@{ Ok = $false; Detail = 'no cloud restore held for the suite' } }
    $leaf = Split-Path -Leaf $script:QaCloudHeld
    Write-QaLine "  cloud restore: the Steam Cloud files back as they were held ($leaf)"
    $out = & (Join-Path $script:QaRig 'cleanup.ps1') -Run $leaf *>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    if ($OutFile) { Add-Content -LiteralPath $OutFile -Value $out -Encoding utf8 }
    $restored = @($out | Where-Object { $_ -match 'cleanup done: ' }) | Select-Object -Last 1
    $ok = ($code -eq 0 -and [bool]$restored -and -not (Test-Path -LiteralPath (Join-Path $script:QaCloudHeld 'CLEANUP_PENDING')))
    $detail = "cleanup.ps1 -Run $leaf exit $code" + $(if ($restored) { '; ' + ($restored -replace '^.*cleanup done: ', '') } else { '' })
    if ($ok) { $script:QaCloudHeld = $null } else { $detail += "; still pending, finish it with: $(Get-QaCloudCommand $leaf)" }
    return [pscustomobject]@{ Ok = $ok; Detail = $detail }
}

# The suite's end: the held restore finished and the 100% save checked, into $Results. On STOP-RIG the restore is
# left pending (no resync launch while the owner uses the PC) and the command that finishes it is reported.
function Complete-QaCloud($Results, [string]$OutFile) {
    if (-not $script:QaCloudHeld) { return }
    $name = 'Steam Cloud files back as before the suite'
    if (Test-QaStopRequested) {
        $leaf = Split-Path -Leaf $script:QaCloudHeld
        [void]$Results.Add([pscustomobject]@{ Name = $name; Ok = $false
            Detail = "STOP-RIG: left pending, no resync launch while the owner uses the PC; the next cleanup or game start finishes it, or: $(Get-QaCloudCommand $leaf)" })
    } else {
        $r = Invoke-QaCloudRestore $OutFile
        [void]$Results.Add([pscustomobject]@{ Name = $name; Ok = $r.Ok; Detail = $r.Detail })
        [void]$Results.Add([pscustomobject]@{ Name = 'the 100% save in GAME-AUTOSAVE0'; Ok = (Test-QaSaveInPlace); Detail = $script:QaSaveTarget })
    }
    $script:QaCloudHeld = $null
}

# The 100% save in slot 0: $true when every file there matches the save's copy byte for byte.
function Test-QaSaveInPlace {
    $files = @(Get-ChildItem -LiteralPath $script:QaSaveSource -File -ErrorAction SilentlyContinue)
    if ($files.Count -eq 0) { return $false }
    foreach ($f in $files) {
        $there = Join-Path $script:QaSaveTarget $f.Name
        if (-not (Test-Path -LiteralPath $there)) { return $false }
        if ((Get-FileHash -LiteralPath $f.FullName).Hash -ne (Get-FileHash -LiteralPath $there).Hash) { return $false }
    }
    return $true
}
