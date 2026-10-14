param([string]$Mode = "hide")
# Hides (or moves off screen) every top level window of the openjk processes as soon as it appears,
# until the game exits. Mode: hide = ShowWindow(SW_HIDE), offscreen = move to -6000,-6000 without activation.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
$deadline = (Get-Date).AddSeconds(30)
while ($true) {
  $procs = @(Get-Process -Name "openjk_sp.x86_64", "openjk.x86_64" -ErrorAction SilentlyContinue)
  if ($procs.Count -eq 0) {
    if ((Get-Date) -gt $deadline) { break }
    Start-Sleep -Milliseconds 20; continue
  }
  $deadline = (Get-Date).AddSeconds(5)
  $ids = $procs | ForEach-Object { [uint32]$_.Id }
  [W]::EnumWindows({ param($h, $l)
    $p = [uint32]0; [void][W]::GetWindowThreadProcessId($h, [ref]$p)
    if ($ids -contains $p -and [W]::IsWindowVisible($h)) {
      if ($Mode -eq "hide") { [void][W]::ShowWindow($h, 0) }
      else { [void][W]::SetWindowPos($h, [IntPtr]::Zero, -6000, -6000, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) }
    }
    return $true }, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 20
}
