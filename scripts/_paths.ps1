# 所有测试脚本共用的路径解析（可由环境变量覆盖）
$repo  = Split-Path $PSScriptRoot -Parent
$root  = Join-Path (Split-Path $repo -Parent) ''
$src   = if ($env:FA2SRC)   { $env:FA2SRC }   else { Join-Path $root 'fa2sp_src' }
$build = if ($env:FA2BUILD) { $env:FA2BUILD } else { 'C:\FA2Build' }
$test  = if ($env:FA2TEST)  { $env:FA2TEST }  else { Join-Path $root 'fa2sp_test' }
$A     = if ($env:FA2A) { $env:FA2A } elseif (Test-Path (Join-Path $root 'fa2sp_test_ours')) { Join-Path $root 'fa2sp_test_ours' } else { Join-Path $test 'ours' }
$B     = if ($env:FA2B) { $env:FA2B } elseif (Test-Path (Join-Path $root 'fa2sp_test_beta')) { Join-Path $root 'fa2sp_test_beta' } else { Join-Path $test 'beta' }
$proj  = Join-Path $repo 'kernel'
$ev    = Join-Path $repo 'evidence'
$dll   = Join-Path $build 'Release\FA2sp.dll'
$msb   = if ($env:MSBUILD)  { $env:MSBUILD }  else { 'D:\VSBuildTools\MSBuild\Current\Bin\MSBuild.exe' }
