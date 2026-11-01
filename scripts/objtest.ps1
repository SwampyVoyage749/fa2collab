#requires -Version 5.1
# 物件渲染刷新端到端验证
#   规则：注入端的自测把一条真实 [Structures] 记录写进「对方光标旁 3 格」，
#         对方收到 INI 节后必须重建对象列表并整屏重绘，画面里出现这栋建筑。
#   用法:
#     objtest.ps1                                   # 空白图，房主注入，抓客户端
#     objtest.ps1 -Map "D:\...\c_battlecity.map"     # 真实图
#     objtest.ps1 -Injector client                   # 改由客户端注入，抓房主窗口
#   抓图用 PrintWindow（不受窗口遮挡影响）。
param(
    [string]$Map = 'presets_tem.map',
    [ValidateSet('host', 'client')][string]$Injector = 'host'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_paths.ps1"
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class U3 {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X, Y; }
  public static readonly IntPtr TOPMOST = new IntPtr(-1);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public static IntPtr FindMain(uint pid) {
    IntPtr best = IntPtr.Zero;
    long bestArea = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid || !IsWindowVisible(h) || GetWindowTextLength(h) == 0) return true;
      RECT rc;
      if (!GetWindowRect(h, out rc)) return true;
      long area = (long)(rc.Right - rc.Left) * (rc.Bottom - rc.Top);
      if (area > bestArea) { bestArea = area; best = h; }
      return true;
    }, IntPtr.Zero);
    return best;
  }
  public static bool Yes(string t) { var h = FindWindow(null, t); if (h == IntPtr.Zero) return false; SendMessage(h, 0x0111, (IntPtr)6, IntPtr.Zero); return true; }
  public static void Front(IntPtr h) { SetWindowPos(h, TOPMOST, 0, 0, 0, 0, 0x0001 | 0x0002); SetForegroundWindow(h); }
  public static bool Grab(IntPtr h, string path) {
    RECT rc; if (!GetWindowRect(h, out rc)) return false;
    int w = rc.Right - rc.Left, ht = rc.Bottom - rc.Top;
    if (w <= 0 || ht <= 0) return false;
    using (var bmp = new System.Drawing.Bitmap(w, ht)) {
      using (var g = System.Drawing.Graphics.FromImage(bmp)) {
        g.Clear(System.Drawing.Color.Black);
        IntPtr hdc = g.GetHdc();
        bool ok = PrintWindow(h, hdc, 2);
        g.ReleaseHdc(hdc);
        if (!ok) return false;
      }
      bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    }
    return true;
  }
}
'@ -Language CSharp -ReferencedAssemblies 'System.Drawing'

New-Item -ItemType Directory -Force -Path $ev | Out-Null
$map = $Map
$tag = [IO.Path]::GetFileNameWithoutExtension($map)

Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
foreach ($d in $A, $B) {
    Copy-Item $dll (Join-Path $d 'FA2sp.dll') -Force
    Remove-Item (Join-Path $d 'FA2sp.log') -ErrorAction SilentlyContinue
}

# 注入端要多等一会儿，等「对方光标」到位再动手
$nl = [string][char]13 + [string][char]10
$testOpt = @('SelfTest=1', 'SelfTestDelayMs=20000', 'SelfTestObject=1')
if ($Injector -eq 'host') {
    $iniA = @('[Collab]','Enabled=1','Host=1','Port=31415','Name=Alpha') + $testOpt
    $iniB = @('[Collab]','Enabled=1','Host=0','HostAddress=127.0.0.1','Port=31415','Name=Beta')
} else {
    $iniA = @('[Collab]','Enabled=1','Host=1','Port=31415','Name=Alpha')
    $iniB = @('[Collab]','Enabled=1','Host=0','HostAddress=127.0.0.1','Port=31415','Name=Beta') + $testOpt
}
[IO.File]::WriteAllText("$A\FA2spCollab.ini", ($iniA -join $nl) + $nl, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText("$B\FA2spCollab.ini", ($iniB -join $nl) + $nl, [Text.Encoding]::ASCII)

function WaitNewProcess([int[]]$before, [int]$timeoutSec) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        $p = Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' -and $before -notcontains $_.Id } | Select-Object -First 1
        if ($p) { return $p }
        Start-Sleep -Milliseconds 500
    }
    return $null
}

$before = @(Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | ForEach-Object { $_.Id })
Write-Host '[1/4] start host A ...'
Start-Process -FilePath (Join-Path $A 'Syringe.exe') -ArgumentList "`"FinalAlert2YR.dat`" -o `"$map`"" -WorkingDirectory $A | Out-Null
$pA = WaitNewProcess $before 30
$before = @(Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | ForEach-Object { $_.Id })
Start-Sleep -Seconds 12
Write-Host '[2/4] start client B ...'
Start-Process -FilePath (Join-Path $B 'Syringe.exe') -ArgumentList "`"FinalAlert2YR.dat`" -o `"$map`"" -WorkingDirectory $B | Out-Null
$pB = WaitNewProcess $before 30
Start-Sleep -Seconds 14
[void][U3]::Yes('FA2sp Init Checker')
Start-Sleep -Seconds 6

function HwndOf($p) {
    for ($i = 0; $i -lt 40; $i++) {
        $h = [U3]::FindMain([uint32]$p.Id)
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 500
    }
    return [IntPtr]::Zero
}
$hA = HwndOf $pA
$hB = HwndOf $pB
Write-Host ("A pid={0} hwnd={1} / B pid={2} hwnd={3} / injector={4}" -f $pA.Id, $hA, $pB.Id, $hB, $Injector)

# 观察端 = 注入端的对端；鼠标停在它的视图中心，注入端以它的光标为锚点
$hWatch = if ($Injector -eq 'host') { $hB } else { $hA }
$cr = New-Object U3+RECT
if ($hWatch -ne [IntPtr]::Zero -and [U3]::GetClientRect($hWatch, [ref]$cr)) {
    $pt = New-Object U3+POINT
    $pt.X = [int](($cr.Right - $cr.Left) / 2); $pt.Y = [int](($cr.Bottom - $cr.Top) / 2)
    [void][U3]::ClientToScreen($hWatch, [ref]$pt)
    [void][U3]::Front($hWatch)
    [void][U3]::SetCursorPos($pt.X, $pt.Y)
    Write-Host ("mouse over watched window center {0},{1}" -f $pt.X, $pt.Y)
}
Start-Sleep -Seconds 2
[void][U3]::Grab($hWatch, (Join-Path $ev ('objtest_{0}_{1}_before.png' -f $tag, $Injector)))

Write-Host '[3/4] wait for injection (delay 20s after peer join) ...'
Start-Sleep -Seconds 22
[void][U3]::Front($hWatch)
Start-Sleep -Milliseconds 800
[void][U3]::Grab($hWatch, (Join-Path $ev ('objtest_{0}_{1}_after.png' -f $tag, $Injector)))
Start-Sleep -Seconds 3

Write-Host '[4/4] assert logs ...'
$la = (Get-Content "$A\FA2sp.log") -join "`n"
$lb = (Get-Content "$B\FA2sp.log") -join "`n"
Copy-Item "$A\FA2sp.log" "$ev\objtest_host_${tag}_$Injector.log" -Force
Copy-Item "$B\FA2sp.log" "$ev\objtest_client_${tag}_$Injector.log" -Force

$injLog = if ($Injector -eq 'host') { $la } else { $lb }
$watLog = if ($Injector -eq 'host') { $lb } else { $la }
$alive = (Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' }).Count
Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue

$res = @()
function A1($n, $ok, $d) { $script:res += [pscustomobject]@{ 检查项=$n; 结果=$(if($ok){'PASS'}else{'FAIL'}); 说明=$d } }
$m  = [regex]::Match($injLog, 'selftest: placed structure key=999 type=(\S+) at \((\d+),(\d+)\)')
$type = if ($m.Success) { $m.Groups[1].Value } else { '' }
$nb = [regex]::Match($watLog, 'object lists rebuilt: buildings=(\d+) units=(\d+) infantry=(\d+) aircraft=(\d+)')

A1 '注入真实建筑' $m.Success ($(if($m.Success){"type={0} at ({1},{2}) [{3}]" -f $type, $m.Groups[2].Value, $m.Groups[3].Value, $Injector} else {'not found'}))
A1 '观察端收到 [Structures]' ($watLog -match 'applied ini section \[Structures\]') 'applied [Structures]'
A1 '观察端重建对象列表' $nb.Success ($(if($nb.Success){"buildings={0} units={1} infantry={2} aircraft={3}" -f $nb.Groups[1].Value, $nb.Groups[2].Value, $nb.Groups[3].Value, $nb.Groups[4].Value} else {'none'}))
A1 '观察端载入该建筑素材' ($type -ne '' -and $watLog -match ("LoadObjects loading: " + [regex]::Escape($type))) ("expect LoadObjects loading: $type")
A1 '两边存活' ($alive -ge 2) ("alive={0}" -f $alive)
A1 '观察端无异常' ($watLog -notmatch 'exception in Tick') 'no collab exception'

$res | Format-Table -AutoSize
$fail = ($res | Where-Object { $_.结果 -eq 'FAIL' }).Count
if ($fail -eq 0) { Write-Host '== 物件刷新验证通过 ==' -ForegroundColor Green } else { Write-Host ("== {0} 项失败 ==" -f $fail) -ForegroundColor Red }
