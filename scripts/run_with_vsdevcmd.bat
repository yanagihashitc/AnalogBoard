@echo off
setlocal

if "%~1"=="" (
    echo Usage: %~nx0 command [args...]
    exit /b 1
)

set "VSDEVCMD="
set "VCVARS="
set "USE_VCVARS=0"

if defined VSINSTALLDIR (
    if exist "%VSINSTALLDIR%Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%VSINSTALLDIR%Common7\Tools\VsDevCmd.bat"
    if exist "%VSINSTALLDIR%VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSINSTALLDIR%VC\Auxiliary\Build\vcvars64.bat"
)

if not defined VSDEVCMD (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" (
        for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALLDIR_FOUND=%%I"
        if defined VSINSTALLDIR_FOUND (
            if exist "%VSINSTALLDIR_FOUND%\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%VSINSTALLDIR_FOUND%\Common7\Tools\VsDevCmd.bat"
            if exist "%VSINSTALLDIR_FOUND%\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSINSTALLDIR_FOUND%\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

if not defined VSDEVCMD (
    for %%E in (Community Professional Enterprise BuildTools) do (
        if not defined VSDEVCMD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat"
        if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
    )
)

if not defined VSDEVCMD if defined VCVARS (
    set "VSDEVCMD=%VCVARS%"
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
