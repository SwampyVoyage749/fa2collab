#requires -Version 5.1
# 两台实例联机冒烟测试：启动 -> 握手 -> 移动鼠标 -> 截图取证
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_paths.ps1"
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class U {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X, Y; }
  public static bool ClickYes(string t) { var h = FindWindow(null, t); if (h == IntPtr.Zero) return false; SendMessage(h, 0x0111, (IntPtr)6, IntPtr.Zero); return true; }
}
'@ -Language CSharp

New-Item -ItemType Directory -Force -Path $ev | Out-Null

Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
foreach ($d in $A, $B) {
    Copy-Item $dll (Join-Path $d 'FA2sp.dll') -Force
    Remove-Item (Join-Path $d 'FA2sp.log') -ErrorAction SilentlyContinue
}
"dll = {0:N0} bytes" -f (Get-Item $dll).Length

Start-Process -FilePath (Join-Path $A 'Syringe.exe') -ArgumentList '"FinalAlert2YR.dat" -o "presets_tem.map"' -WorkingDirectory $A | Out-Null
Start-Sleep -Seconds 20
Start-Process -FilePath (Join-Path $B 'Syringe.exe') -ArgumentList '"FinalAlert2YR.dat" -o "presets_tem.map"' -WorkingDirectory $B | Out-Null
Start-Sleep -Seconds 8
[void][U]::ClickYes('FA2sp Init Checker')
Start-Sleep -Seconds 25

$procs = Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Sort-Object StartTime
$procs | Select-Object Id, MainWindowTitle, MainWindowHandle | Format-Table -AutoSize
if ($procs.Count -lt 2) { "!! 只有 $($procs.Count) 个实例存活"; }

# 把鼠标移到 A 的视图里晃两下，触发光标广播
$h = [IntPtr]$procs[0].MainWindowHandle
if ($h -ne [IntPtr]::Zero) {
    $r = New-Object U+RECT
    [void][U]::GetWindowRect($h, [ref]$r)
    [void][U]::SetForegroundWindow($h)
    [void][U]::SetCursorPos($r.Left + 520, $r.Top + 360)
    Start-Sleep -Milliseconds 700
    [void][U]::SetCursorPos($r.Left + 640, $r.Top + 430)
    Start-Sleep -Seconds 2
}

# 截图 A
if ($h -ne [IntPtr]::Zero) {
    $r = New-Object U+RECT
    [void][U]::GetWindowRect($h, [ref]$r)
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -gt 0 -and $ht -gt 0) {
        $bmp = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
        $g.Dispose()
        $img = Join-Path $ev 'host_overlay.png'
        $bmp.Save($img, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        "screenshot: $img"
    }
}

Copy-Item "$A\FA2sp.log" "$ev\host_FA2sp.log" -Force -ErrorAction SilentlyContinue
Copy-Item "$B\FA2sp.log" "$ev\client_FA2sp.log" -Force -ErrorAction SilentlyContinue
"--- A ---"; Get-Content "$A\FA2sp.log" | Select-String 'Collab' | ForEach-Object { $_.Line }
"--- B ---"; Get-Content "$B\FA2sp.log" | Select-String 'Collab' | ForEach-Object { $_.Line }
