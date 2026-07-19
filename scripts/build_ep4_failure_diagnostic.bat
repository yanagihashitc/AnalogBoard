@echo off
setlocal

set "DEFAULT_BUILD_ID=r7-driver-ep4-failure-status-20260715T2358JST"
set "BUILD_ID=%~1"
if not defined BUILD_ID set "BUILD_ID=%DEFAULT_BUILD_ID%"

for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI\"
for %%I in ("%SCRIPT_DIR%..") do set "SOURCE_ROOT=%%~fI"
for %%I in ("%SOURCE_ROOT%\..") do set "PACKAGE_ROOT=%%~fI"

set "RUN_WITH_VSDEVCMD=%SCRIPT_DIR%run_with_vsdevcmd.bat"
set "OUTPUT_ROOT=%SOURCE_ROOT%\diagnostic_builds\%BUILD_ID%"
set "BIN_DIR=%OUTPUT_ROOT%\bin"
set "OBJ_DIR=%OUTPUT_ROOT%\obj"
set "MANIFEST_DIR=%OUTPUT_ROOT%\manifest"
set "SOURCE_DIFF_DIR=%OUTPUT_ROOT%\source_diff"
set "VERIFICATION_DIR=%OUTPUT_ROOT%\verification"
set "PREP_DIR=%SOURCE_ROOT%\diagnostic_prep\ep4_failure_status"
set "DLL_PROJECT=%SOURCE_ROOT%\AnalogBoard_Dll\AnalogBoard_Dll.vcxproj"
set "APP_PROJECT=%SOURCE_ROOT%\AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj"
set "UNIT_TEST_SCRIPT=%SOURCE_ROOT%\AnalogBoard_UnitTest\build_ep4_failure_diagnostic_test.bat"

if exist "%OUTPUT_ROOT%" (
    echo ERROR: Output directory already exists. Use a new build ID.
    echo        %OUTPUT_ROOT%
    exit /b 1
)

if not exist "%RUN_WITH_VSDEVCMD%" (
    echo ERROR: Visual Studio wrapper not found: %RUN_WITH_VSDEVCMD%
    exit /b 1
)

if not exist "%PREP_DIR%\source_diff\diagnostic_changes.patch" (
    echo ERROR: Prepared diagnostic source diff is missing.
    exit /b 1
)
if not exist "%PREP_DIR%\FIELD_PROCEDURE.html" (
    echo ERROR: Prepared HTML field procedure is missing.
    exit /b 1
)

mkdir "%BIN_DIR%" "%OBJ_DIR%\AnalogBoard_Dll" "%OBJ_DIR%\AnalogBoard_TestApp" "%MANIFEST_DIR%" "%SOURCE_DIFF_DIR%" "%VERIFICATION_DIR%"
if errorlevel 1 exit /b 1

copy /y "%PREP_DIR%\source_diff\diagnostic_changes.patch" "%SOURCE_DIFF_DIR%\diagnostic_changes.patch" > nul
copy /y "%PREP_DIR%\FIELD_CHECKLIST.md" "%OUTPUT_ROOT%\FIELD_CHECKLIST.md" > nul
copy /y "%PREP_DIR%\FIELD_PROCEDURE.html" "%OUTPUT_ROOT%\FIELD_PROCEDURE.html" > nul

echo %BUILD_ID%> "%OUTPUT_ROOT%\BUILD_ID.txt"

echo === Unit tests ===
call "%UNIT_TEST_SCRIPT%" "%VERIFICATION_DIR%\test_bin" > "%VERIFICATION_DIR%\unit_tests.txt" 2>&1
if errorlevel 1 (
    type "%VERIFICATION_DIR%\unit_tests.txt"
    echo ERROR: Unit tests failed. Build stopped.
    exit /b 1
)

echo === Release x64 DLL clean build ===
call "%RUN_WITH_VSDEVCMD%" MSBuild.exe "%DLL_PROJECT%" /t:Rebuild /m:1 /p:Configuration=Release /p:Platform=x64 "/p:OutDir=%BIN_DIR%\\" "/p:IntDir=%OBJ_DIR%\AnalogBoard_Dll\\"
if errorlevel 1 (
    echo ERROR: DLL build failed. Build stopped.
    exit /b 1
)

if not exist "%BIN_DIR%\AnalogBoard_Dll.lib" (
    echo ERROR: DLL import library was not produced in the isolated output directory.
    exit /b 1
)

echo === Release x64 TestApp clean build ===
call "%RUN_WITH_VSDEVCMD%" MSBuild.exe "%APP_PROJECT%" /t:Rebuild /m:1 /p:Configuration=Release /p:Platform=x64 "/p:OutDir=%BIN_DIR%\\" "/p:IntDir=%OBJ_DIR%\AnalogBoard_TestApp\\" "/p:AnalogBoardDllImportLibrary=%BIN_DIR%\AnalogBoard_Dll.lib"
if errorlevel 1 (
    echo ERROR: TestApp build failed. Build stopped.
    exit /b 1
)

if not exist "%BIN_DIR%\AnalogBoard_Dll.dll" (
    echo ERROR: AnalogBoard_Dll.dll was not produced.
    exit /b 1
)
if not exist "%BIN_DIR%\AnalogBoard_TestApp.exe" (
    echo ERROR: AnalogBoard_TestApp.exe was not produced.
    exit /b 1
)

copy /y "%PACKAGE_ROOT%\field_package\bin\default_config.csv" "%BIN_DIR%\default_config.csv" > nul

echo === Build manifest and hashes ===
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%finalize_ep4_failure_diagnostic_build.ps1" -BuildId "%BUILD_ID%" -OutputRoot "%OUTPUT_ROOT%" -SourceRoot "%SOURCE_ROOT%" -PackageManifest "%PACKAGE_ROOT%\PACKAGE_MANIFEST.json"
if errorlevel 1 (
    echo ERROR: Build finalization failed.
    exit /b 1
)

echo.
echo Build complete: %OUTPUT_ROOT%
echo Hardware use is NOT authorized by this script. Follow FIELD_PROCEDURE.html.
exit /b 0
