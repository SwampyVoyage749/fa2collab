@echo off
rem 32-bit build for the standalone collab core (unit test). No approval needed.
setlocal
if defined VCVARS goto toolchain_ready
set "VCVARS=D:\VSBuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%VCVARS%" goto toolchain_ready
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto toolchain_ready
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%I\VC\Auxiliary\Build\vcvarsall.bat"
:toolchain_ready
if not exist "%VCVARS%" (
  echo [ERR] vcvarsall not found at %VCVARS%
  exit /b 2
)
cd /d "%~dp0"
if not exist build mkdir build
call "%VCVARS%" x86 >nul || (echo [ERR] vcvarsall failed & exit /b 3)
cl /nologo /EHsc /std:c++17 /MT /utf-8 /W3 /D_CRT_SECURE_NO_WARNINGS ^
   /Fo:build\ tests\test_collab.cpp src\collab_transport.cpp src\collab_session.cpp ^
   /Fe:build\test_collab.exe /link /MACHINE:X86
if errorlevel 1 (echo [ERR] compile failed & exit /b 1)
echo [OK] build\test_collab.exe
build\test_collab.exe
if errorlevel 1 exit /b 1
exit /b 0
