@echo off
setlocal

if "%~1"=="" (
    echo Usage: scripts\run_simulation.bat ^<preset^>
    exit /b 1
)

for %%I in ("%~dp0..") do set "ROOT_DIR=%%~fI"
set "PRESET=%~1"
set "EXE=%ROOT_DIR%\x64\Debug\AnalogBoard_SimRunner.exe"

cmd /d /c "%ROOT_DIR%\scripts\run_with_vsdevcmd.bat msbuild %ROOT_DIR%\AnalogBoard_TestApp.sln /t:AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
if errorlevel 1 exit /b 1

pushd "%ROOT_DIR%"
"%EXE%" "%PRESET%"
set "RUN_EXIT=%ERRORLEVEL%"
popd

exit /b %RUN_EXIT%
