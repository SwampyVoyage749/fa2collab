#requires -Version 5.1
# 一键复现验证：编译 -> 单测 -> 双实例联机 -> 断言日志
# 用法：powershell -ExecutionPolicy Bypass -File verify.ps1
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\_paths.ps1"
$results = @()

function Add-Result([string]$name, [bool]$ok, [string]$detail) {
    $script:results += [pscustomobject]@{ 检查项 = $name; 结果 = $(if ($ok) { 'PASS' } else { 'FAIL' }); 说明 = $detail }
}

Write-Host "[1/4] 编译 FA2sp.dll ..." -ForegroundColor Cyan
$out = & $msb "$build\FA2sp.sln" /nologo /m /v:q /p:Configuration=Release /p:Platform=x86 /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:DebugInformationFormat=None 2>&1
Add-Result '编译 FA2sp.vcxproj' ($LASTEXITCODE -eq 0 -and (Test-Path $dll)) ("exit={0}" -f $LASTEXITCODE)

Write-Host "[2/4] 运行核心单元测试 ..." -ForegroundColor Cyan
$t = & cmd /c "chcp 65001 >nul && `"$proj\build\test_collab.exe`"" 2>&1
Add-Result '联机内核单测' (($t -join "`n") -match 'ALL PASS') '期望 ALL PASS'

Write-Host "[3/4] 双实例联机（约 60 秒）..." -ForegroundColor Cyan
Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
foreach ($d in $A, $B) {
    Copy-Item $dll (Join-Path $d 'FA2sp.dll') -Force
    Remove-Item (Join-Path $d 'FA2sp.log') -ErrorAction SilentlyContinue
}
# 固定配置：A 先改（delay 0），B 5 秒后再改同一块 -> B 必然被拒
$nl = [string][char]13 + [string][char]10
$iniA = @('[Collab]','Enabled=1','Host=1','Port=31415','Name=Alpha','SelfTest=1','SelfTestValue=5','SelfTestObject=1') -join $nl
$iniB = @('[Collab]','Enabled=1','Host=0','HostAddress=127.0.0.1','Port=31415','Name=Beta','SelfTest=1','SelfTestDelayMs=5000','SelfTestValue=7') -join $nl
[IO.File]::WriteAllText("$A\FA2spCollab.ini", $iniA + $nl, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText("$B\FA2spCollab.ini", $iniB + $nl, [Text.Encoding]::ASCII)

Start-Process -FilePath (Join-Path $A 'Syringe.exe') -ArgumentList '"FinalAlert2YR.dat" -o "presets_tem.map"' -WorkingDirectory $A | Out-Null
Start-Sleep -Seconds 18
Start-Process -FilePath (Join-Path $B 'Syringe.exe') -ArgumentList '"FinalAlert2YR.dat" -o "presets_tem.map"' -WorkingDirectory $B | Out-Null
Start-Sleep -Seconds 7

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class DlgCloser {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static bool Yes(string t) { var h = FindWindow(null, t); if (h == IntPtr.Zero) return false; SendMessage(h, 0x0111, (IntPtr)6, IntPtr.Zero); return true; }
}
'@ -Language CSharp
[void][DlgCloser]::Yes('FA2sp Init Checker')
Start-Sleep -Seconds 32

$la = (Get-Content "$A\FA2sp.log") -join "`n"
$lb = (Get-Content "$B\FA2sp.log") -join "`n"
Get-Process | Where-Object { $_.ProcessName -match 'FinalAlert' } | Stop-Process -Force -ErrorAction SilentlyContinue

Write-Host "[4/4] 断言日志 ..." -ForegroundColor Cyan
Add-Result 'A 建图 + 开房'        ($la -match 'map ready' -and $la -match 'hosting on port') 'A: map ready / hosting'
Add-Result 'B 加入并看到房主'      ($lb -match 'joining' -and $lb -match 'peer joined: id=0 name=Alpha') 'B: joining / peer joined Alpha'
Add-Result 'A 看到 B'             ($la -match 'peer joined: id=1 name=Beta') 'A: peer joined Beta'
Add-Result '地形增量到达'          ($lb -match 'applied remote chunk \(12,12\) cells=16') 'B: applied cells=16'
Add-Result '物件列表重建（建筑）'    ($lb -match 'object lists rebuilt: buildings=1') 'B: object lists rebuilt buildings=1'
Add-Result '物件列表重建（单位）'    ($lb -match 'object lists rebuilt:.*units=') 'B: units 计数已重建'
Add-Result '物件节到达'            ($lb -match 'applied ini section \[Structures\]') 'B: applied [Structures]'
Add-Result '租约拒绝生效'          ($lb -match 'chunk \(12,12\) owned by player 0') 'B: owned by player 0'
Add-Result '被占块被还原'          ($lb -match 'protected chunk \(12,12\)') 'B: protected chunk'
Add-Result 'INI 节同步'            ($la -match 'ini section' -and $lb -match 'ini section') 'A/B: ini section'
Add-Result '快照推送完成'          ($la -match 'snapshot push finished') 'A: snapshot push finished'
Add-Result '无崩溃（两边都活着到断言）' ($la.Length -gt 0 -and $lb.Length -gt 0) '日志非空'

$results | Format-Table -AutoSize
$fail = ($results | Where-Object { $_.结果 -eq 'FAIL' }).Count
Write-Host ""
if ($fail -eq 0) { Write-Host "== 全部通过 ==" -ForegroundColor Green; exit 0 }
else { Write-Host ("== {0} 项失败 ==" -f $fail) -ForegroundColor Red; exit 1 }
