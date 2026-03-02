@echo off
setlocal EnableExtensions

if "%~1"=="" (
  echo Usage: %~nx0 ^<command^> [args...]
  echo Example: %~nx0 msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1
  exit /b 2
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDEVCMD="

if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find Common7\Tools\VsDevCmd.bat`) do (
    if not defined VSDEVCMD set "VSDEVCMD=%%I"
  )
)

if not defined VSDEVCMD (
  for %%I in (
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
  ) do (
    if exist "%%~I" if not defined VSDEVCMD set "VSDEVCMD=%%~I"
  )
)

if not defined VSDEVCMD (
  echo ERROR: VsDevCmd.bat not found.
  echo        Install Visual Studio 2022 or Build Tools with C++ workload.
  exit /b 1
)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 (
  echo ERROR: Failed to initialize Visual Studio developer environment.
  exit /b 1
)

%*
set "EXIT_CODE=%ERRORLEVEL%"
exit /b %EXIT_CODE%
