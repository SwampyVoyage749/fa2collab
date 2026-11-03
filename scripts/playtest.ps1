# playtest.ps1 - 一键试玩（第一版，命令行原型）
#
# 作用：把 FA2 存出来的 .map 塞进 MO 的地图列表，并把它设为「遭遇战默认选中」，
#       然后启动 MO 客户端。用户进遭遇战就能看到这张图已经选中，点开始即可。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File playtest.ps1 -Map "D:\xxx\mymap.map"
#   powershell -NoProfile -ExecutionPolicy Bypass -File playtest.ps1 -Map "..." -NoLaunch
#
# 可选参数：
#   -GameDir   MO 目录，默认 D:\Mental Omega Normal - 副本
#   -Name      在地图列表里显示的文件名（不含扩展名），默认 collab_playtest
#   -NoLaunch  只放图、不启动客户端

param(
    [Parameter(Mandatory = $true)][string]$Map,
    [string]$GameDir = 'D:\Mental Omega Normal - 副本',
    [string]$Name = 'collab_playtest',
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Map)) { throw "地图不存在: $Map" }
if (-not (Test-Path $GameDir)) { throw "游戏目录不存在: $GameDir" }

$mapDir = Join-Path $GameDir 'MapsMO\Standard'
$clientDir = Join-Path $GameDir 'Client'
if (-not (Test-Path $mapDir)) { throw "找不到地图目录: $mapDir" }

$dest = Join-Path $mapDir ($Name + '.map')
Copy-Item $Map $dest -Force

$sha1 = (Get-FileHash $dest -Algorithm SHA1).Hash.ToLower()

$ini = Join-Path $clientDir 'SkirmishSettings.ini'
if (Test-Path $ini) {
    $lines = Get-Content $ini
    $out = New-Object System.Collections.Generic.List[string]
    $inSettings = $false
    $setMap = $false
    foreach ($line in $lines) {
        if ($line -match '^\s*\[') { $inSettings = ($line -match '^\s*\[Settings\]') }
        if ($inSettings -and $line -match '^\s*Map\s*=') {
            $out.Add("Map=$sha1"); $setMap = $true; continue
        }
        $out.Add($line)
    }
    if (-not $setMap) {
        # 没有 Map 行就插到 [Settings] 后面
        $idx = $out.IndexOf('[Settings]')
        if ($idx -ge 0) { $out.Insert($idx + 1, "Map=$sha1") } else { $out.Add('[Settings]'); $out.Add("Map=$sha1") }
    }
    Set-Content -Path $ini -Value $out -Encoding Default
} else {
    Set-Content -Path $ini -Value @('[Settings]', "Map=$sha1") -Encoding Default
}

Write-Host "已放入地图目录: $dest"
Write-Host "SHA1: $sha1 （已写入 SkirmishSettings.ini 的 [Settings] Map）"

if (-not $NoLaunch) {
    $client = Join-Path $GameDir 'MentalOmegaClient.exe'
    if (Test-Path $client) {
        Start-Process -FilePath $client -WorkingDirectory $GameDir
        Write-Host "已启动 MO 客户端，进「遭遇战」应该能看到 $Name 已选中"
    } else {
        Write-Host "没找到 MentalOmegaClient.exe，请手动启动游戏"
    }
}
