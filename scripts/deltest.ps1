#requires -Version 5.1
# 删除同步端到端验证：
#   房主放建筑(key=999) -> 对端出现（截图像素差） -> 房主删掉这条键 -> 对端建筑消失（截图像素差）
# 用法:
#   deltest.ps1                                  # 空白图
#   deltest.ps1 -Map "D:\...\map.map"            # 指定地图
param(
    [string]$Map = 'presets_tem.map',
    [int]$DeleteDelayMs = 15000
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_paths.ps1"
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class D1 {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X, Y; }
  public static readonly IntPtr TOPMOST = new IntPtr(-1);
  public static bool Yes(string t) { var h = FindWindow(null, t); if (h == IntPtr.Zero) return false; SendMessage(h, 0x0111, (IntPtr)6, IntPtr.Zero); return true; }
  public static void Front(IntPtr h) { SetWindowPos(h, TOPMOST, 0, 0, 0, 0, 0x0001 | 0x0002); SetForegroundWindow(h); }
  public static IntPtr FindMain(uint pid) {
    IntPtr best = IntPtr.Zero; long bestArea = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid || !IsWindowVisible(h) || GetWindowTextLength(h) == 0) return true;
      RECT rc; if (!GetWindowRect(h, out rc)) return true;
      long area = (long)(rc.Right - rc.Left) * (rc.Bottom - rc.Top);
      if (area > bestArea) { bestArea = area; best = h; }
      return true;
    }, IntPtr.Zero);
    return best;
  }
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
  // 采样比较（每 2 像素取一点），返回不同点数
  public static long Diff(string a, string b) {
    using (var x = new System.Drawing.Bitmap(a)) using (var y = new System.Drawing.Bitmap(b)) {
      if (x.Width != y.Width || x.Height != y.Height) return -1;
      long n = 0;
      for (int j = 0; j < x.Height; j += 2)
        for (int i = 0; i < x.Width; i += 2)
          if (x.GetPixel(i, j).ToArgb() != y.GetPixel(i, j).ToArgb()) n++;
      return n;
    }
  }
}
'@ -Language CSharp -ReferencedAssemblies 'System.Drawing'

New-Item -ItemType Directory -Force -Path $ev | Out-Null
$tag = [IO.Path]::GetFileNameWithoutExtension($Map)

Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
foreach ($d in $A, $B) {
    Copy-Item $dll (Join-Path $d 'FA2sp.dll') -Force
    Remove-Item (Join-Path $d 'FA2sp.log') -ErrorAction SilentlyContinue
}

$nl = [string][char]13 + [string][char]10
$iniA = @('[Collab]','Enabled=1','Host=1','Port=31415','Name=Alpha','SelfTest=1','SelfTestDelayMs=25000',
          'SelfTestObject=1',"SelfTestDeleteDelayMs=$DeleteDelayMs") -join $nl
$iniB = @('[Collab]','Enabled=1','Host=0','HostAddress=127.0.0.1','Port=31415','Name=Beta') -join $nl
[IO.File]::WriteAllText("$A\FA2spCollab.ini", $iniA + $nl, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText("$B\FA2spCollab.ini", $iniB + $nl, [Text.Encoding]::ASCII)

function WaitNewProcess([int[]]$before, [int]$timeoutSec) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        $p = Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' -and $before -notcontains $_.Id } | Select-Object -First 1
        if ($p) { return $p }
        Start-Sleep -Milliseconds 500
    }
    return $null
}

Write-Host '[1/5] start host ...'
$before = @(Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | ForEach-Object { $_.Id })
Start-Process -FilePath (Join-Path $A 'Syringe.exe') -ArgumentList "`"FinalAlert2YR.dat`" -o `"$Map`"" -WorkingDirectory $A | Out-Null
$pA = WaitNewProcess $before 30
$before = @(Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | ForEach-Object { $_.Id })
Start-Sleep -Seconds 12
Write-Host '[2/5] start client ...'
Start-Process -FilePath (Join-Path $B 'Syringe.exe') -ArgumentList "`"FinalAlert2YR.dat`" -o `"$Map`"" -WorkingDirectory $B | Out-Null
$pB = WaitNewProcess $before 30
Start-Sleep -Seconds 14
[void][D1]::Yes('FA2sp Init Checker')
Start-Sleep -Seconds 6

$hB = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    $hB = [D1]::FindMain([uint32]$pB.Id)
    if ($hB -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 500
}
$cr = New-Object D1+RECT
if ($hB -ne [IntPtr]::Zero -and [D1]::GetClientRect($hB, [ref]$cr)) {
    $pt = New-Object D1+POINT
    $pt.X = [int](($cr.Right - $cr.Left) / 2); $pt.Y = [int](($cr.Bottom - $cr.Top) / 2)
    [void][D1]::ClientToScreen($hB, [ref]$pt)
    [void][D1]::Front($hB)
    [void][D1]::SetCursorPos($pt.X, $pt.Y)
}

$fBefore = Join-Path $ev ("deltest_{0}_1_before.png" -f $tag)
$fPlaced = Join-Path $ev ("deltest_{0}_2_placed.png" -f $tag)
$fDeleted = Join-Path $ev ("deltest_{0}_3_deleted.png" -f $tag)
Start-Sleep -Seconds 2
[void][D1]::Grab($hB, $fBefore)

function WaitLog([string]$path, [string]$pattern, [int]$timeoutSec) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        $txt = ''
        try { $txt = Get-Content $path -Raw -ErrorAction Stop } catch { }
        if ($txt -and ($txt -match $pattern)) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

Write-Host '[3/5] wait for building to be placed (watch host log) ...'
$placedOk = WaitLog "$A\FA2sp.log" 'selftest: placed structure key=999' 120
Start-Sleep -Seconds 3
[void][D1]::Front($hB)
Start-Sleep -Milliseconds 700
[void][D1]::Grab($hB, $fPlaced)

Write-Host '[4/5] wait for delete (watch host log) ...'
$deletedOk = WaitLog "$A\FA2sp.log" 'selftest: deleted structure key=999' ([int]($DeleteDelayMs / 1000) + 90)
Start-Sleep -Seconds 3
[void][D1]::Front($hB)
Start-Sleep -Milliseconds 700
[void][D1]::Grab($hB, $fDeleted)

Write-Host '[5/5] assert ...'
$la = (Get-Content "$A\FA2sp.log") -join "`n"
$lb = (Get-Content "$B\FA2sp.log") -join "`n"
Copy-Item "$A\FA2sp.log" "$ev\deltest_host_$tag.log" -Force
Copy-Item "$B\FA2sp.log" "$ev\deltest_client_$tag.log" -Force
$alive = (Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' }).Count
Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue

$counts = @()
foreach ($m in [regex]::Matches($lb, 'object lists rebuilt: buildings=(\d+)')) { $counts += [int]$m.Groups[1].Value }
$dPlaced = [D1]::Diff($fBefore, $fPlaced)
$dDeleted = [D1]::Diff($fPlaced, $fDeleted)
$dBack = [D1]::Diff($fBefore, $fDeleted)

$res = @()
function A1($n, $ok, $d) { $script:res += [pscustomobject]@{ 检查项=$n; 结果=$(if($ok){'PASS'}else{'FAIL'}); 说明=$d } }
A1 '房主放入建筑' ($la -match 'selftest: placed structure key=999') 'placed key=999'
A1 '房主删除该建筑' ($la -match 'selftest: deleted structure key=999') 'deleted key=999'
A1 '对端收到删除' ($lb -match 'removed 1 keys \(deleted by peer\)') 'removed 1 keys (deleted by peer)'
# 判据：注入让建筑数 +1，删除后又回到「最大值-1」；空白图就是 [1,0]，真实图是 [282,283,282]
$maxCnt = if ($counts.Count -gt 0) { ($counts | Measure-Object -Maximum).Maximum } else { -1 }
$lastCnt = if ($counts.Count -gt 0) { $counts[-1] } else { -1 }
A1 '对端建筑数 +1 后又回到基线' ($counts.Count -ge 1 -and $lastCnt -eq ($maxCnt - 1) -and $maxCnt -gt 0) ("rebuild buildings=[" + ($counts -join ',') + "]")
if ($tag -like 'presets_*') {
    # 空白图（画面静态）：建筑一定在视野里，用像素差严格证明「出现 / 消失 / 回到原样」
    A1 '画面出现建筑（像素差）' ($dPlaced -gt 500) ("diff(before,placed)=" + $dPlaced)
    A1 '删除后画面回退（像素差）' ($dDeleted -gt 500) ("diff(placed,deleted)=" + $dDeleted)
    A1 '删除后与初始画面完全一致' ($dBack -eq 0) ("diff(before,deleted)=" + $dBack)
} else {
    # 真实图：已有 $baseCnt 个建筑，注入点可能被遮挡；且水面/灯光动画会让像素一直变，
    # 像素差只作参考，判定以「建筑计数 +1 再回到基线」+ 日志为准
    A1 '画面变化（参考，真实图有动画/遮挡）' ($true) ("diff(before,placed)=$dPlaced diff(placed,deleted)=$dDeleted diff(before,deleted)=$dBack")
}
A1 '两边存活且无异常' ($alive -ge 2 -and $lb -notmatch 'exception in Tick' -and $la -notmatch 'exception in Tick') ("alive=" + $alive)

$res | Format-Table -AutoSize
$fail = ($res | Where-Object { $_.结果 -eq 'FAIL' }).Count
if ($fail -eq 0) { Write-Host '== 删除同步验证通过 ==' -ForegroundColor Green } else { Write-Host ("== {0} 项失败 ==" -f $fail) -ForegroundColor Red }
