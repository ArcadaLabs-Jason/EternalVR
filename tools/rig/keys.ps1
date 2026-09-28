# Sends key presses to the running game window with SendInput (scan codes, so raw input sees them).
#   keys.ps1 -Press SPACE            tap a key
#   keys.ps1 -Hold R -Ms 1500        hold a key (skips a cinematic)
#   keys.ps1 -Press ESC -Repeat 2 -GapMs 300
#   keys.ps1 -MoveX 20 -Steps 50 -StepMs 10  relative mouse moves (raw input sees them): 50 x 20 counts right
# The game window is brought to the foreground first; the previous foreground window is restored after
# unless -KeepFocus is given.
param(
    [string]$Press,
    [string]$Hold,
    [int]$Ms = 1500,
    [int]$Repeat = 1,
    [int]$GapMs = 200,
    [string[]]$Process = @('DOOMEternalx64vk'),
    [int]$MoveX = 0,
    [int]$MoveY = 0,
    [int]$Steps = 1,
    [int]$StepMs = 10,
    [switch]$KeepFocus
)
$ErrorActionPreference = 'Stop'

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class RigKeys {
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit, Size = 40)] struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    const uint KEYEVENTF_KEYUP = 2, KEYEVENTF_SCANCODE = 8, KEYEVENTF_EXTENDEDKEY = 1;
    public static bool Focus(IntPtr h) {
        IntPtr fg = GetForegroundWindow(); uint pid;
        uint t1 = GetWindowThreadProcessId(fg, out pid), t2 = GetCurrentThreadId();
        AttachThreadInput(t2, t1, true);
        keybd_event(0x12, 0, 0, UIntPtr.Zero); keybd_event(0x12, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
        ShowWindow(h, 9); bool ok = SetForegroundWindow(h);
        AttachThreadInput(t2, t1, false);
        return ok && GetForegroundWindow() == h;
    }
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit, Size = 40)] struct MINPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true, EntryPoint = "SendInput")] static extern uint SendMouse(uint n, MINPUT[] inputs, int size);
    public static void Move(int dx, int dy) {
        var i = new MINPUT[1]; i[0].type = 0; i[0].mi.dx = dx; i[0].mi.dy = dy; i[0].mi.dwFlags = 1; // MOUSEEVENTF_MOVE, relative
        SendMouse(1, i, Marshal.SizeOf(typeof(MINPUT)));
    }
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
    // The game's own window (class Ghost_CLASS): an OpenXR runtime's preview window in the same process can
    // be the process's main window.
    public static IntPtr GameWindow(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p != pid || !IsWindowVisible(h)) return true;
            var c = new System.Text.StringBuilder(64); GetClassName(h, c, 64);
            if (c.ToString() == "Ghost_CLASS") { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static void Key(ushort scan, bool extended, bool up) {
        var i = new INPUT[1]; i[0].type = 1; i[0].ki.wScan = scan;
        i[0].ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
        SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
    }
}
'@

# Set 1 scan codes for the keys the runs need.
$scan = @{
    SPACE = 0x39; ESC = 0x01; ENTER = 0x1C; TAB = 0x0F; R = 0x13; E = 0x12; Q = 0x10; F = 0x21
    W = 0x11; A = 0x1E; S = 0x1F; D = 0x20; UP = 0x48; DOWN = 0x50; LEFT = 0x4B; RIGHT = 0x4D; TILDE = 0x29
    LSHIFT = 0x2A; LCTRL = 0x1D; LALT = 0x38; C = 0x2E; G = 0x22; V = 0x2F
    D1 = 0x02; D2 = 0x03; D3 = 0x04; D4 = 0x05; D5 = 0x06; D6 = 0x07; D7 = 0x08; D8 = 0x09
}
$extended = @('UP', 'DOWN', 'LEFT', 'RIGHT')

function Get-Code([string]$k) {
    $u = $k.ToUpperInvariant()
    if (-not $scan.ContainsKey($u)) { throw "unknown key '$k' (known: $($scan.Keys -join ', '))" }
    return @{ Scan = [uint16]$scan[$u]; Ext = ($extended -contains $u) }
}

if (-not $Press -and -not $Hold -and $MoveX -eq 0 -and $MoveY -eq 0) { throw 'give -Press <key>, -Hold <key> or -MoveX/-MoveY' }
$p = Get-Process -Name $Process -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Output 'no game window found'; exit 2 }

$window = [RigKeys]::GameWindow([uint32]$p.Id)
if ($window -eq [IntPtr]::Zero) { $window = $p.MainWindowHandle }
# guard.ps1 hands the foreground back to the owner; runs\FOCUS_HOLD (this process's id) pauses that
# while the keys are sent, and with -KeepFocus until the run ends or another keys.ps1 replaces it.
$runsRoot = $env:EVR_RIG_RUNS_ROOT
if (-not $runsRoot) { . (Join-Path $PSScriptRoot 'workspace.ps1'); $runsRoot = Join-Path (Get-EvrWorkspace) 'runs' }
$holdFile = Join-Path $runsRoot 'FOCUS_HOLD'
$holder = $PID
if ($KeepFocus) { $holder = $p.Id }
try { [IO.File]::WriteAllText($holdFile, [string]$holder) } catch { $holdFile = $null }
$previous = [RigKeys]::GetForegroundWindow()
if (-not [RigKeys]::Focus($window)) { Write-Output "could not focus pid $($p.Id)"; exit 1 }
Start-Sleep -Milliseconds 150

for ($n = 0; $n -lt $Repeat; $n++) {
    if ($Press) {
        $c = Get-Code $Press
        [RigKeys]::Key($c.Scan, $c.Ext, $false); Start-Sleep -Milliseconds 60; [RigKeys]::Key($c.Scan, $c.Ext, $true)
        Write-Output "pressed $Press (pid $($p.Id))"
    }
    if ($Hold) {
        $c = Get-Code $Hold
        [RigKeys]::Key($c.Scan, $c.Ext, $false); Start-Sleep -Milliseconds $Ms; [RigKeys]::Key($c.Scan, $c.Ext, $true)
        Write-Output "held $Hold for $Ms ms (pid $($p.Id))"
    }
    if ($MoveX -ne 0 -or $MoveY -ne 0) {
        for ($s = 0; $s -lt $Steps; $s++) { [RigKeys]::Move($MoveX, $MoveY); Start-Sleep -Milliseconds $StepMs }
        Write-Output "moved the mouse $Steps x ($MoveX, $MoveY) (pid $($p.Id))"
    }
    if ($n -lt $Repeat - 1) { Start-Sleep -Milliseconds $GapMs }
}

if (-not $KeepFocus -and $previous -ne [IntPtr]::Zero -and $previous -ne $window) {
    [void][RigKeys]::Focus($previous)
}
if ($holdFile -and -not $KeepFocus) { try { Remove-Item -LiteralPath $holdFile -ErrorAction Stop } catch { } }
exit 0
