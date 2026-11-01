#requires -Version 5.1
# 物件重建压测：房主每 8 秒注入一条会改变位置的 [Structures] 记录，
# 对端每次都重建整套对象列表 + 整屏重绘。观察崩溃、异常、内存与重建次数。
param(
    [string]$Map = ($env:FA2MAP -as [string]),
    [int]$Minutes = 5
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_paths.ps1"
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class W1 {
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
$iniA = @('[Collab]','Enabled=1','Host=1','Port=31415','Name=Alpha','SelfTest=1','SelfTestDelayMs=5000','SelfTestObject=1','SelfTestObjectWalk=1','SelfTestLoopMs=8000') -join $nl
$iniB = @('[Collab]','Enabled=1','Host=0','HostAddress=127.0.0.1','Port=31415','Name=Beta') -join $nl
[IO.File]::WriteAllText("$A\FA2spCollab.ini", $iniA + $nl, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText("$B\FA2spCollab.ini", $iniB + $nl, [Text.Encoding]::ASCII)

Write-Host '[1/3] start host + client ...'
Start-Process -FilePath (Join-Path $A 'Syringe.exe') -ArgumentList "`"FinalAlert2YR.dat`" -o `"$Map`"" -WorkingDirectory $A | Out-Null
Start-Sleep -Seconds 18
Start-Process -FilePath (Join-Path $B 'Syringe.exe') -ArgumentList "`"FinalAlert2YR.dat`" -o `"$Map`"" -WorkingDirectory $B | Out-Null
Start-Sleep -Seconds 14
[void][W1]::Yes('FA2sp Init Checker')
Start-Sleep -Seconds 10

$logA = "$A\FA2sp.log"; $logB = "$B\FA2sp.log"
$samples = @()
$end = (Get-Date).AddMinutes($Minutes)
Write-Host ("[2/3] soak {0} min ..." -f $Minutes)
while ((Get-Date) -lt $end) {
    $pa = Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Sort-Object StartTime
    $ma = if ($pa.Count -ge 1) { [int]($pa[0].WorkingSet64 / 1MB) } else { -1 }
    $mb = if ($pa.Count -ge 2) { [int]($pa[1].WorkingSet64 / 1MB) } else { -1 }
    $samples += [pscustomobject]@{ t = (Get-Date).ToString('HH:mm:ss'); alive = $pa.Count; memA = $ma; memB = $mb }
    Start-Sleep -Seconds 30
}

Write-Host '[3/3] collect ...'
$la = (Get-Content $logA) -join "`n"
$lb = (Get-Content $logB) -join "`n"
Copy-Item $logA "$ev\soakobj_host.log" -Force
Copy-Item $logB "$ev\soakobj_client.log" -Force
$samples | Export-Csv "$ev\soakobj_memory.csv" -NoTypeInformation -Encoding UTF8
$alive = (Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' }).Count
$snapA = @(Get-ChildItem "$A\debug" -Filter 'snapshot-*' -Directory -ErrorAction SilentlyContinue).Count
$snapB = @(Get-ChildItem "$B\debug" -Filter 'snapshot-*' -Directory -ErrorAction SilentlyContinue).Count
Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue

$inj   = ([regex]::Matches($la, 'selftest: placed structure key=999')).Count
$rebA  = ([regex]::Matches($la, 'object lists rebuilt:')).Count
$rebB  = ([regex]::Matches($lb, 'object lists rebuilt:')).Count
$excA  = ([regex]::Matches($la, 'exception in Tick')).Count
$excB  = ([regex]::Matches($lb, 'exception in Tick')).Count

[pscustomobject]@{
    注入次数 = $inj
    A重建次数 = $rebA
    B重建次数 = $rebB
    A异常 = $excA
    B异常 = $excB
    A崩溃快照 = $snapA
    B崩溃快照 = $snapB
    结束时存活 = $alive
    内存A起止 = ("{0} -> {1} MB" -f $samples[0].memA, $samples[-1].memA)
    内存B起止 = ("{0} -> {1} MB" -f $samples[0].memB, $samples[-1].memB)
} | Format-List
if ($excA -eq 0 -and $excB -eq 0 -and $alive -eq 2 -and $inj -gt 10 -and $rebB -ge $inj) {
    Write-Host '== 物件重建压测通过 ==' -ForegroundColor Green
} else {
    Write-Host '== 压测未通过，看日志 ==' -ForegroundColor Red
}
