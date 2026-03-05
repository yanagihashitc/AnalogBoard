@echo off
setlocal

if "%~1"=="" (
    echo Usage: %~nx0 command [args...]
    exit /b 1
)

set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
set "USE_VCVARS=0"
if not exist "%VSDEVCMD%" (
    set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    set "USE_VCVARS=1"
)
if not exist "%VSDEVCMD%" (
    echo ERROR: Visual Studio developer command script not found.
    exit /b 1
)

if "%USE_VCVARS%"=="1" (
    call "%VSDEVCMD%" > nul 2>&1
) else (
    call "%VSDEVCMD%" -arch=x64 -host_arch=x64 > nul 2>&1
)
if errorlevel 1 (
    echo ERROR: Failed to initialize Visual Studio developer environment.
    exit /b 1
)

cmd /d /c "%*"
exit /b %ERRORLEVEL%
