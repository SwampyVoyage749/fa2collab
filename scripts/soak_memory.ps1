#requires -Version 5.1
# 10 分钟内存趋势观察（默认配置，每 8 秒一次编辑）
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_paths.ps1"
$csv = 'F:\Deepseek\DeepSeekHarness\fa2collab\evidence\memory_soak.csv'

Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
foreach ($d in $A, $B) {
    Copy-Item $dll (Join-Path $d 'FA2sp.dll') -Force
    Remove-Item (Join-Path $d 'FA2sp.log') -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $d 'debug') -Recurse -Force -ErrorAction SilentlyContinue
}
$nl = [string][char]13 + [string][char]10
$iniA = @('[Collab]','Enabled=1','Host=1','Port=31415','Name=Alpha','SelfTest=1','SelfTestValue=5','SelfTestLoopMs=8000','SyncObjects=0','IniDeleteSync=0') -join $nl
$iniB = @('[Collab]','Enabled=1','Host=0','HostAddress=127.0.0.1','Port=31415','Name=Beta','SelfTest=1','SelfTestDelayMs=3000','SelfTestValue=7','SelfTestLoopMs=8000','SyncObjects=0','IniDeleteSync=0') -join $nl
[IO.File]::WriteAllText("$A\FA2spCollab.ini", $iniA + $nl, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText("$B\FA2spCollab.ini", $iniB + $nl, [Text.Encoding]::ASCII)

Start-Process -FilePath (Join-Path $A 'Syringe.exe') -ArgumentList '"FinalAlert2YR.dat" -o "presets_tem.map"' -WorkingDirectory $A | Out-Null
Start-Sleep -Seconds 18
Start-Process -FilePath (Join-Path $B 'Syringe.exe') -ArgumentList '"FinalAlert2YR.dat" -o "presets_tem.map"' -WorkingDirectory $B | Out-Null
Start-Sleep -Seconds 7

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class Closer2 {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static bool Yes(string t) { var h = FindWindow(null, t); if (h == IntPtr.Zero) return false; SendMessage(h, 0x0111, (IntPtr)6, IntPtr.Zero); return true; }
}
'@ -Language CSharp
[void][Closer2]::Yes('FA2sp Init Checker')

$rows = @()
for ($i = 0; $i -le 10; $i++) {
    Start-Sleep -Seconds 60
    $p = Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' }
    $rows += [pscustomobject]@{
        minute = $i + 1
        procs  = $p.Count
        mbA    = if ($p.Count -ge 1) { [math]::Round($p[0].WorkingSet64 / 1MB, 0) } else { 0 }
        mbB    = if ($p.Count -ge 2) { [math]::Round($p[1].WorkingSet64 / 1MB, 0) } else { 0 }
    }
    $rows[-1] | Format-Table -AutoSize | Out-String | Write-Host
}
$rows | Export-Csv $csv -NoTypeInformation -Encoding UTF8

$exc = 0
foreach ($d in $A, $B) {
    $exc += ((Get-Content (Join-Path $d 'FA2sp.log') | Select-String 'Exception handler|exception in Tick') | Measure-Object).Count
}
$snap = (Get-ChildItem $A, $B -Filter 'snapshot-*' -Recurse -Directory -ErrorAction SilentlyContinue | Measure-Object).Count
"异常总数=$exc 崩溃快照=$snap"
Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue
