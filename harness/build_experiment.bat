@echo off
REM Builds the initialisation experiment against the real RZNAI_AGI model.
REM
REM   build_experiment.bat [path-to-RZNAI_AGI]
REM
REM Needs the model at a revision where cycle() runs (main at 90f299b or later).

setlocal

if not defined VCINSTALLDIR (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
    call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
  )
)

set HERE=%~dp0
set MODEL=%~1
if "%MODEL%"=="" set MODEL=%HERE%..\..\RZNAI_AGI
if not defined PROFILE set PROFILE=16

if not exist "%MODEL%\RZNAI_AGI.cpp" (
  echo error: no RZNAI_AGI.cpp under "%MODEL%"
  exit /b 1
)

set OUT=%HERE%..\build
if not exist "%OUT%" mkdir "%OUT%"

REM The experiment calls perform_iann() directly and never reaches
REM read_sensory(), so the model keeps its own in_0 / in_1 stubs.
set KNOBS=/DRZNAI_AGI_NO_MAIN
set CFLAGS=/nologo /O2 /I"%HERE%..\src" /I"%MODEL%" /DRZN_PACK_PROFILE=%PROFILE%

cl %CFLAGS% /W4 /std:c11 /c ^
  "%HERE%..\src\rzn_spiral.c" "%HERE%..\src\rzn_pack.c" "%HERE%..\src\rzn_frame.c" ^
  "%HERE%..\src\rzn_fovea.c" "%HERE%..\src\rzn_agi_sink.c" "%HERE%..\src\rzn_agi_bridge.c" ^
  /Fo:"%OUT%\\" || exit /b 1

cl %CFLAGS% %KNOBS% /W3 /EHsc /c ^
  "%MODEL%\RZNAI_AGI.cpp" /Fo:"%OUT%\exp_RZNAI_AGI.obj" || exit /b 1

cl %CFLAGS% %KNOBS% /W3 /EHsc /c ^
  "%HERE%init_experiment.cpp" /Fo:"%OUT%\\" || exit /b 1

link /nologo /OUT:"%OUT%\rzn_experiment.exe" ^
  "%OUT%\rzn_spiral.obj" "%OUT%\rzn_pack.obj" "%OUT%\rzn_frame.obj" ^
  "%OUT%\rzn_fovea.obj" "%OUT%\rzn_agi_sink.obj" "%OUT%\rzn_agi_bridge.obj" ^
  "%OUT%\exp_RZNAI_AGI.obj" "%OUT%\init_experiment.obj" || exit /b 1

echo ok -^> %OUT%\rzn_experiment.exe

endlocal
