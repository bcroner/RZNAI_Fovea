@echo off
REM Builds the foveal engine against the real RZNAI_AGI model.
REM
REM   build_harness.bat [path-to-RZNAI_AGI] [cycles]
REM
REM Defaults to ..\..\RZNAI_AGI and 2000 cycles.  The model source must have
REM both patches applied (see ..\patches\) or it will not compile or run.

setlocal

if not defined VCINSTALLDIR (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
    call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
  )
)

set HERE=%~dp0
set MODEL=%~1
if "%MODEL%"=="" set MODEL=%HERE%..\..\RZNAI_AGI
set CYCLES=%~2
if "%CYCLES%"=="" set CYCLES=2000
if not defined PROFILE set PROFILE=16
if not defined INSZ set INSZ=16

if not exist "%MODEL%\RZNAI_AGI.cpp" (
  echo error: no RZNAI_AGI.cpp under "%MODEL%"
  exit /b 1
)

set OUT=%HERE%..\build
if not exist "%OUT%" mkdir "%OUT%"

set KNOBS=/DRZNAI_AGI_EXTERNAL_SENSORS /DRZNAI_AGI_NO_MAIN /DRZNAI_AGI_MAX_CYCLES=%CYCLES%
set CFLAGS=/nologo /O2 /I"%HERE%..\src" /I"%MODEL%" /DRZN_PACK_PROFILE=%PROFILE% /DRZNAI_AGI_IN_SZ=%INSZ%

echo building the engine (C)...
cl %CFLAGS% /W4 /std:c11 /c ^
  "%HERE%..\src\rzn_spiral.c" "%HERE%..\src\rzn_pack.c" "%HERE%..\src\rzn_frame.c" ^
  "%HERE%..\src\rzn_fovea.c" "%HERE%..\src\rzn_agi_sink.c" "%HERE%..\src\rzn_agi_bridge.c" ^
  /Fo:"%OUT%\\" || exit /b 1

echo building the model and harness (C++)...
cl %CFLAGS% %KNOBS% /W3 /EHsc /c ^
  "%MODEL%\RZNAI_AGI.cpp" "%HERE%harness_main.cpp" /Fo:"%OUT%\\" || exit /b 1

echo linking...
link /nologo /OUT:"%OUT%\rzn_harness.exe" ^
  "%OUT%\rzn_spiral.obj" "%OUT%\rzn_pack.obj" "%OUT%\rzn_frame.obj" ^
  "%OUT%\rzn_fovea.obj" "%OUT%\rzn_agi_sink.obj" "%OUT%\rzn_agi_bridge.obj" ^
  "%OUT%\RZNAI_AGI.obj" "%OUT%\harness_main.obj" || exit /b 1

echo ok -^> %OUT%\rzn_harness.exe  (profile %PROFILE%, %CYCLES% cycles)

endlocal
