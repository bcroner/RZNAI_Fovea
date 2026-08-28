@echo off
REM RZN AI foveal stereo input engine -- MSVC build.
REM Run from a Developer Command Prompt, or let this script locate vcvars.
REM
REM   build_msvc.bat            build demo + tests
REM   build_msvc.bat test       build, then run the tests
REM   set DISPARITY=1 & build_msvc.bat    include the disparity stage
REM   set PROFILE=16 & build_msvc.bat     narrow packing for a model with in_sz=16

setlocal

if not defined VCINSTALLDIR (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do (
    call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
  )
)

if not defined PROFILE set PROFILE=32
set CFLAGS=/nologo /std:c11 /W4 /O2 /Isrc /DRZN_PACK_PROFILE=%PROFILE%
if "%DISPARITY%"=="1" (
  set CFLAGS=%CFLAGS% /DRZN_ENABLE_DISPARITY=1
  set EXTRA=src\rzn_disparity.c
) else (
  set EXTRA=
)

set CORE=src\rzn_spiral.c src\rzn_pack.c src\rzn_frame.c src\rzn_fovea.c src\rzn_agi_sink.c

if not exist build mkdir build

echo building demo...
cl %CFLAGS% %CORE% %EXTRA% src\demo_main.c /Fe:build\rzn_demo.exe /Fo:build\ || exit /b 1

echo building tests...
cl %CFLAGS% %CORE% %EXTRA% test\test_rzn.c /Fe:build\rzn_test.exe /Fo:build\ || exit /b 1

echo ok -^> build\rzn_demo.exe, build\rzn_test.exe

if "%1"=="test" build\rzn_test.exe

endlocal
