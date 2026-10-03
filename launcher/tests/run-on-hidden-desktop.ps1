# Runs a program on a desktop of its own (not shown) and waits for it: its windows never take the focus, so the visual
# QA (UiShots) and the update test (update-e2e.ps1, run by powershell.exe there) can run while the game is tested.
# UI Automation and PrintWindow work there as on the shown desktop.
#   powershell -NoProfile -ExecutionPolicy Bypass -File launcher\tests\run-on-hidden-desktop.ps1 -Exe <exe> -Arguments "<args>"
param([Parameter(Mandatory = $true)][string]$Exe, [string]$Arguments = '', [int]$TimeoutSeconds = 1800)
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class Desk {
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct STARTUPINFO { public int cb; public string lpReserved; public string lpDesktop; public string lpTitle; public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags; public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError; }
  [StructLayout(LayoutKind.Sequential)] public struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
  [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern IntPtr CreateDesktop(string name, IntPtr dev, IntPtr mode, int flags, uint access, IntPtr sa);
  [DllImport("user32.dll")] public static extern bool CloseDesktop(IntPtr h);
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern bool CreateProcess(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
  [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr h, uint ms);
  [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr h, out uint code);
}
'@
$desk = [Desk]::CreateDesktop('evr-ui-qa', [IntPtr]::Zero, [IntPtr]::Zero, 0, 0x10000000, [IntPtr]::Zero)
if ($desk -eq [IntPtr]::Zero) { throw 'CreateDesktop failed' }
$si = New-Object Desk+STARTUPINFO
$si.cb = [Runtime.InteropServices.Marshal]::SizeOf($si)
$si.lpDesktop = 'evr-ui-qa'
$pi = New-Object Desk+PROCESS_INFORMATION
if (-not [Desk]::CreateProcess($Exe, "`"$Exe`" $Arguments", [IntPtr]::Zero, [IntPtr]::Zero, $false, 0, [IntPtr]::Zero, (Split-Path $Exe), [ref]$si, [ref]$pi)) { throw "CreateProcess failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
[void][Desk]::WaitForSingleObject($pi.hProcess, [uint32]($TimeoutSeconds * 1000))
$code = 0; [void][Desk]::GetExitCodeProcess($pi.hProcess, [ref]$code)
[void][Desk]::CloseDesktop($desk)
"exit $code"
