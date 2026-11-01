@echo off
rem Launch two FA2sp instances on one PC for LAN-collab testing.
rem Usage: drag a .map file onto this file, or:
rem        start_two_instances.cmd "D:\path\to\map.map"
setlocal
set MAP=%~1
if "%MAP%"=="" (
  echo Usage: start_two_instances.cmd "path\to\your.map"
  exit /b 2
)
if not exist "%MAP%" (
  echo [ERR] map not found: %MAP%
  exit /b 2
)
set A=%~dp0..\..\fa2sp_test_ours
set B=%~dp0..\..\fa2sp_test_beta

echo [1/3] starting HOST (Alpha) ...
pushd "%A%"
start "FA2-Alpha" Syringe.exe "FinalAlert2YR.dat" -o "%MAP%"
popd
timeout /t 14 /nobreak >nul

echo [2/3] starting CLIENT (Beta) ...
pushd "%B%"
start "FA2-Beta" Syringe.exe "FinalAlert2YR.dat" -o "%MAP%"
popd
timeout /t 9 /nobreak >nul

echo [3/3] dismissing "FA2sp Init Checker" dialog ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0click_yes.ps1"
timeout /t 2 /nobreak >nul

echo Done. Two editor windows should be open on the same map.
echo   HOST log: %A%\FA2sp.log
echo   CLIENT log: %B%\FA2sp.log
exit /b 0
