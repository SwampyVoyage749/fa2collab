#requires -Version 5.1
# 把本地新提交推到 GitHub，并把 Release 说明更新成 docs/RELEASE_NOTES_v0.4.0.md
# 用法：双击 push-update.cmd（或右键「使用 PowerShell 运行」）
# 凭据：优先读 F:\Deepseek\secrets\gh_token.txt；没有就当场提示粘贴（只存在内存里）
$ErrorActionPreference = 'Stop'
$repoDir = $PSScriptRoot
$owner = 'SwampyVoyage749'
$repo = 'fa2collab'
$tag = 'v0.4.0'
$git = 'F:\Tools\MinGit\cmd\git.exe'
$notesFile = Join-Path $repoDir 'docs\RELEASE_NOTES_v0.4.0.md'

$tokenPath = 'F:\Deepseek\secrets\gh_token.txt'
$token = $null
if (Test-Path $tokenPath) {
    $token = (Get-Content $tokenPath -Raw).Trim()
    Write-Host "[i] 使用 $tokenPath 里的 token" -ForegroundColor Gray
}
if (-not $token) {
    $sec = Read-Host -AsSecureString '粘贴 GitHub token（repo 权限，输入不回显）'
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
    try { $token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}
if (-not $token -or $token.Length -lt 20) { Write-Host '[x] token 无效' -ForegroundColor Red; exit 1 }

$headers = @{ Authorization = 'Bearer ' + $token; 'User-Agent' = 'fa2collab-push'; Accept = 'application/vnd.github+json' }
try {
    $me = Invoke-RestMethod -Uri 'https://api.github.com/user' -Headers $headers -TimeoutSec 30
    Write-Host ("[ok] token 属于 {0}" -f $me.login) -ForegroundColor Green
} catch {
    Write-Host ("[x] token 校验失败（可能已撤销或过期）：{0}" -f $_.Exception.Message) -ForegroundColor Red
    exit 1
}

# 1) 推代码
$askpass = Join-Path $env:TEMP 'fa2collab_askpass.cmd'
@"
@echo off
echo %~1 | findstr /i "username" >nul
if errorlevel 1 (echo %GH_TOKEN%) else (echo %GH_USER%)
"@ | Set-Content -Path $askpass -Encoding ASCII
$env:GH_TOKEN = $token
$env:GH_USER = $owner
$env:GIT_ASKPASS = $askpass
$env:GIT_TERMINAL_PROMPT = '0'
Push-Location $repoDir
try {
    & $git -c credential.helper= -c http.sslBackend=schannel -c core.askpass=$askpass push origin main
    if ($LASTEXITCODE -ne 0) { Write-Host '[x] push 失败' -ForegroundColor Red; exit 1 }
    & $git -c credential.helper= -c http.sslBackend=schannel -c core.askpass=$askpass push origin $tag 2>$null
    Write-Host '[ok] main 已推送' -ForegroundColor Green
} finally {
    Pop-Location
    Remove-Item $askpass -Force -ErrorAction SilentlyContinue
    Remove-Item Env:GH_TOKEN, Env:GH_USER, Env:GIT_ASKPASS -ErrorAction SilentlyContinue
}

# 2) 更新 Release 说明
if (Test-Path $notesFile) {
    $body = Get-Content $notesFile -Raw -Encoding UTF8
    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$owner/$repo/releases/tags/$tag" -Headers $headers -TimeoutSec 30
    $payload = @{ body = $body } | ConvertTo-Json
    $null = Invoke-RestMethod -Uri "https://api.github.com/repos/$owner/$repo/releases/$($rel.id)" -Method Patch -Headers $headers -Body $payload -ContentType 'application/json; charset=utf-8' -TimeoutSec 60
    Write-Host '[ok] Release 说明已更新' -ForegroundColor Green
}

$token = $null
Write-Host ''
Write-Host '全部完成喵～' -ForegroundColor Cyan
Read-Host '按回车关闭'
