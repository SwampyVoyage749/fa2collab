#requires -Version 5.1
# 崩溃回归：房主故意广播非法格子坐标（(-1,-1) 与 (30000,30000)），
# 对端必须忽略而不是把坐标喂给 FA2 的 MapCoord2ScreenCoord（那会在 0045E8F0 崩）。
param([string]$Map = 'presets_tem.map')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_paths.ps1"
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class C1 {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static bool Yes(string t) { var h = FindWindow(null, t); if (h == IntPtr.Zero) return false; SendMessage(h, 0x0111, (IntPtr)6, IntPtr.Zero); return true; }
}
'@ -Language CSharp


Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
foreach ($d in $A, $B) {
    Copy-Item $dll (Join-Path $d 'FA2sp.dll') -Force
    Remove-Item (Join-Path $d 'FA2sp.log') -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $d 'debug\snapshot-*') -Recurse -Force -ErrorAction SilentlyContinue
}
$nl = [string][char]13 + [string][char]10
$iniA = @('[Collab]','Enabled=1','Host=1','Port=31415','Name=Alpha','SelfTest=1','SelfTestDelayMs=8000','SelfTestBadCursor=1') -join $nl
$iniB = @('[Collab]','Enabled=1','Host=0','HostAddress=127.0.0.1','Port=31415','Name=Beta') -join $nl
[IO.File]::WriteAllText("$A\FA2spCollab.ini", $iniA + $nl, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText("$B\FA2spCollab.ini", $iniB + $nl, [Text.Encoding]::ASCII)

Start-Process -FilePath (Join-Path $A 'Syringe.exe') -ArgumentList "`"FinalAlert2YR.dat`" -o `"$Map`"" -WorkingDirectory $A | Out-Null
Start-Sleep -Seconds 16
Start-Process -FilePath (Join-Path $B 'Syringe.exe') -ArgumentList "`"FinalAlert2YR.dat`" -o `"$Map`"" -WorkingDirectory $B | Out-Null
Start-Sleep -Seconds 14
[void][C1]::Yes('FA2sp Init Checker')
Start-Sleep -Seconds 30

$la = (Get-Content "$A\FA2sp.log") -join "`n"
$lb = (Get-Content "$B\FA2sp.log") -join "`n"
Copy-Item "$A\FA2sp.log" "$ev\crashtest_host.log" -Force
Copy-Item "$B\FA2sp.log" "$ev\crashtest_client.log" -Force
$alive = (Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' }).Count
$snapA = @(Get-ChildItem "$A\debug" -Directory -Filter 'snapshot-*' -ErrorAction SilentlyContinue).Count
$snapB = @(Get-ChildItem "$B\debug" -Directory -Filter 'snapshot-*' -ErrorAction SilentlyContinue).Count
Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue

$res = @()
function A1($n, $ok, $d) { $script:res += [pscustomobject]@{ 检查项=$n; 结果=$(if($ok){'PASS'}else{'FAIL'}); 说明=$d } }
A1 '对端收到非法光标并忽略' ($lb -match 'ignore invalid cursor') 'client: ignore invalid cursor'
A1 '对端没有崩（进程存活）' ($alive -ge 2) ("alive=$alive")
A1 '两边都没有崩溃快照' ($snapA -eq 0 -and $snapB -eq 0) ("snapA=$snapA snapB=$snapB")
A1 '没有 Tick 异常' ($la -notmatch 'exception in Tick' -and $lb -notmatch 'exception in Tick') 'no exception in Tick'
A1 '没有 Exception handler fired' (($la -notmatch 'Exception handler fired') -and ($lb -notmatch 'Exception handler fired')) 'no handler fired'

$res | Format-Table -AutoSize
$fail = ($res | Where-Object { $_.结果 -eq 'FAIL' }).Count
if ($fail -eq 0) { Write-Host '== 非法坐标回归通过 ==' -ForegroundColor Green } else { Write-Host ("== {0} 项失败 ==" -f $fail) -ForegroundColor Red }
