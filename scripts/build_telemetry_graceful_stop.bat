@echo off
setlocal

set "DEFAULT_BUILD_ID=r7-driver-telemetry-graceful-stop-20260716T1314JST"
set "DEFAULT_READY_NAME=TELEMETRY_CSV_READY_1314"
set "BUILD_ID=%~1"
if not defined BUILD_ID set "BUILD_ID=%DEFAULT_BUILD_ID%"
set "READY_NAME=%~2"
if not defined READY_NAME set "READY_NAME=%DEFAULT_READY_NAME%"

for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI\"
for %%I in ("%SCRIPT_DIR%..") do set "SOURCE_ROOT=%%~fI"
for %%I in ("%SOURCE_ROOT%\..") do set "PACKAGE_ROOT=%%~fI"

set "RUN_WITH_VSDEVCMD=%SCRIPT_DIR%run_with_vsdevcmd.bat"
set "OUTPUT_ROOT=%SOURCE_ROOT%\diagnostic_builds\%BUILD_ID%"
set "READY_ROOT=%PACKAGE_ROOT%\%READY_NAME%"
set "BIN_DIR=%OUTPUT_ROOT%\bin"
set "OBJ_DIR=%OUTPUT_ROOT%\obj"
set "MANIFEST_DIR=%OUTPUT_ROOT%\manifest"
set "SOURCE_DIFF_DIR=%OUTPUT_ROOT%\source_diff"
set "VERIFICATION_DIR=%OUTPUT_ROOT%\verification"
set "PREP_DIR=%SOURCE_ROOT%\diagnostic_prep\telemetry_graceful_stop"
set "DLL_PROJECT=%SOURCE_ROOT%\AnalogBoard_Dll\AnalogBoard_Dll.vcxproj"
set "APP_PROJECT=%SOURCE_ROOT%\AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj"
set "UNIT_TEST_SCRIPT=%SOURCE_ROOT%\AnalogBoard_UnitTest\build_telemetry_graceful_stop_test.bat"
set "FIELD_VERIFY=%PACKAGE_ROOT%\field_package\tools\verify_package.ps1"
set "OVERLAY_VERIFY=%PREP_DIR%\verify_source_overlay.ps1"
set "DIAGNOSTIC_PATCH=%SOURCE_ROOT%\diagnostic_prep\ep4_failure_status\source_diff\diagnostic_changes.patch"
set "DIAGNOSTIC_MANIFEST=%SOURCE_ROOT%\diagnostic_builds\r7-driver-ep4-failure-status-20260716T0105JST\manifest\build_manifest.json"
set "KNOWN_BASELINE_DEVIATIONS=%PREP_DIR%\KNOWN_BASELINE_DEVIATIONS.txt"

if exist "%OUTPUT_ROOT%" (
    echo ERROR: Output directory already exists. Use a new build ID.
    echo        %OUTPUT_ROOT%
    exit /b 1
)
if exist "%READY_ROOT%" (
    echo ERROR: Ready directory already exists. Use a new ready name.
    echo        %READY_ROOT%
    exit /b 1
)

if not exist "%RUN_WITH_VSDEVCMD%" (
    echo ERROR: Visual Studio wrapper not found: %RUN_WITH_VSDEVCMD%
    exit /b 1
)
if not exist "%SCRIPT_DIR%finalize_telemetry_graceful_stop_build.ps1" (
    echo ERROR: Build finalizer is missing.
    exit /b 1
)
if not exist "%UNIT_TEST_SCRIPT%" (
    echo ERROR: Native unit-test gate is missing.
    exit /b 1
)
if not exist "%DLL_PROJECT%" (
    echo ERROR: DLL project is missing.
    exit /b 1
)
if not exist "%APP_PROJECT%" (
    echo ERROR: TestApp project is missing.
    exit /b 1
)
if not exist "%FIELD_VERIFY%" (
    echo ERROR: Immutable field-package verifier is missing.
    exit /b 1
)
if not exist "%PACKAGE_ROOT%\field_package\bin\default_config.csv" (
    echo ERROR: Required runtime default_config.csv is missing.
    exit /b 1
)
if not exist "%PREP_DIR%\source_diff\graceful_stop_changes.patch" (
    echo ERROR: Prepared source diff is missing.
    exit /b 1
)
if not exist "%PREP_DIR%\review\claude_review.txt" (
    echo ERROR: Required pre-build Claude review evidence is missing.
    exit /b 1
)
if not exist "%PREP_DIR%\DESIGN.md" (
    echo ERROR: Prepared design note is missing.
    exit /b 1
)
if not exist "%PREP_DIR%\FIELD_CHECKLIST.md" (
    echo ERROR: Prepared field checklist is missing.
    exit /b 1
)
if not exist "%PREP_DIR%\FIELD_TELEMETRY_CSV_PROCEDURE.md" (
    echo ERROR: Prepared Markdown field procedure is missing.
    exit /b 1
)
if not exist "%PREP_DIR%\FIELD_TELEMETRY_CSV_PROCEDURE.html" (
    echo ERROR: Prepared HTML field procedure is missing.
    exit /b 1
)
if not exist "%PREP_DIR%\00_READ_ME_FIRST.txt" (
    echo ERROR: Prepared read-me is missing.
    exit /b 1
)
if not exist "%PREP_DIR%\01_START_APP.bat" (
    echo ERROR: Prepared application launcher is missing.
    exit /b 1
)
if not exist "%OVERLAY_VERIFY%" (
    echo ERROR: Source-overlay verifier is missing.
    exit /b 1
)
if not exist "%DIAGNOSTIC_PATCH%" (
    echo ERROR: Prior EP4 diagnostic patch is missing: %DIAGNOSTIC_PATCH%
    exit /b 1
)
if not exist "%DIAGNOSTIC_MANIFEST%" (
    echo ERROR: Prior EP4 build manifest is missing: %DIAGNOSTIC_MANIFEST%
    exit /b 1
)
if not exist "%KNOWN_BASELINE_DEVIATIONS%" (
    echo ERROR: Known packaging-baseline deviation record is missing.
    exit /b 1
)
where git > nul 2>&1
if errorlevel 1 (
    echo ERROR: git is required for source-overlay replay verification.
    exit /b 1
)

set "OVERLAY_PREFLIGHT_OUTPUT=%TEMP%\analogboard-source-overlay-%RANDOM%-%RANDOM%.json"
if exist "%OVERLAY_PREFLIGHT_OUTPUT%" (
    echo ERROR: Temporary overlay-attestation path already exists: %OVERLAY_PREFLIGHT_OUTPUT%
    exit /b 1
)
echo === Source-overlay replay preflight ===
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%OVERLAY_VERIFY%" -PackageRoot "%PACKAGE_ROOT%" -SourceRoot "%SOURCE_ROOT%" -GracefulPatch "%PREP_DIR%\source_diff\graceful_stop_changes.patch" -OutputPath "%OVERLAY_PREFLIGHT_OUTPUT%"
if errorlevel 1 (
    if exist "%OVERLAY_PREFLIGHT_OUTPUT%" del /q "%OVERLAY_PREFLIGHT_OUTPUT%" > nul 2>&1
    echo ERROR: Source-overlay replay verification failed before build.
    exit /b 1
)
if not exist "%OVERLAY_PREFLIGHT_OUTPUT%" (
    echo ERROR: Source-overlay replay verifier did not produce an attestation.
    exit /b 1
)
del /q "%OVERLAY_PREFLIGHT_OUTPUT%" > nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to remove temporary source-overlay attestation: %OVERLAY_PREFLIGHT_OUTPUT%
    exit /b 1
)

mkdir "%BIN_DIR%" "%OBJ_DIR%\AnalogBoard_Dll" "%OBJ_DIR%\AnalogBoard_TestApp" "%MANIFEST_DIR%" "%SOURCE_DIFF_DIR%" "%VERIFICATION_DIR%"
if errorlevel 1 exit /b 1

copy /y "%PREP_DIR%\source_diff\graceful_stop_changes.patch" "%SOURCE_DIFF_DIR%\graceful_stop_changes.patch" > nul
if errorlevel 1 (
    echo ERROR: Failed to stage the prepared source diff.
    exit /b 1
)
copy /y "%PREP_DIR%\DESIGN.md" "%OUTPUT_ROOT%\DESIGN.md" > nul
if errorlevel 1 (
    echo ERROR: Failed to stage the design note.
    exit /b 1
)
copy /y "%PREP_DIR%\FIELD_CHECKLIST.md" "%OUTPUT_ROOT%\FIELD_CHECKLIST.md" > nul
if errorlevel 1 (
    echo ERROR: Failed to stage the field checklist.
    exit /b 1
)
copy /y "%PREP_DIR%\FIELD_TELEMETRY_CSV_PROCEDURE.md" "%OUTPUT_ROOT%\FIELD_TELEMETRY_CSV_PROCEDURE.md" > nul
if errorlevel 1 (
    echo ERROR: Failed to stage the Markdown field procedure.
    exit /b 1
)
copy /y "%PREP_DIR%\FIELD_TELEMETRY_CSV_PROCEDURE.html" "%OUTPUT_ROOT%\FIELD_TELEMETRY_CSV_PROCEDURE.html" > nul
if errorlevel 1 (
    echo ERROR: Failed to stage the HTML field procedure.
    exit /b 1
)
copy /y "%KNOWN_BASELINE_DEVIATIONS%" "%OUTPUT_ROOT%\KNOWN_BASELINE_DEVIATIONS.txt" > nul
if errorlevel 1 (
    echo ERROR: Failed to stage the packaging-baseline deviation record.
    exit /b 1
)
copy /y "%PREP_DIR%\review\claude_review.txt" "%VERIFICATION_DIR%\claude_review.txt" > nul
if errorlevel 1 (
    echo ERROR: Failed to stage the Claude review evidence.
    exit /b 1
)
>"%OUTPUT_ROOT%\BUILD_ID.txt" echo %BUILD_ID%

echo === Immutable field package verification ===
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%FIELD_VERIFY%" -PackageRoot "%PACKAGE_ROOT%\field_package" > "%VERIFICATION_DIR%\field_package_integrity.txt" 2>&1
if errorlevel 1 (
    type "%VERIFICATION_DIR%\field_package_integrity.txt"
    echo ERROR: Immutable field package verification failed. Build stopped.
    exit /b 1
)

echo === Native telemetry graceful-stop gate tests ===
call "%UNIT_TEST_SCRIPT%" "%VERIFICATION_DIR%\test_bin" > "%VERIFICATION_DIR%\unit_tests.txt" 2>&1
if errorlevel 1 (
    type "%VERIFICATION_DIR%\unit_tests.txt"
    echo ERROR: Unit tests failed. Build stopped.
    exit /b 1
)

echo === Release x64 DLL clean build ===
call "%RUN_WITH_VSDEVCMD%" MSBuild.exe "%DLL_PROJECT%" /t:Rebuild /m:1 /p:Configuration=Release /p:Platform=x64 "/p:OutDir=%BIN_DIR%\\" "/p:IntDir=%OBJ_DIR%\AnalogBoard_Dll\\" > "%VERIFICATION_DIR%\release_dll_build.txt" 2>&1
if errorlevel 1 (
    type "%VERIFICATION_DIR%\release_dll_build.txt"
    echo ERROR: DLL build failed. Build stopped.
    exit /b 1
)

if not exist "%BIN_DIR%\AnalogBoard_Dll.lib" (
    echo ERROR: DLL import library was not produced in the isolated output directory.
    exit /b 1
)

echo === Release x64 TestApp clean build ===
call "%RUN_WITH_VSDEVCMD%" MSBuild.exe "%APP_PROJECT%" /t:Rebuild /m:1 /p:Configuration=Release /p:Platform=x64 "/p:OutDir=%BIN_DIR%\\" "/p:IntDir=%OBJ_DIR%\AnalogBoard_TestApp\\" "/p:AnalogBoardDllImportLibrary=%BIN_DIR%\AnalogBoard_Dll.lib" > "%VERIFICATION_DIR%\release_app_build.txt" 2>&1
if errorlevel 1 (
    type "%VERIFICATION_DIR%\release_app_build.txt"
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
if errorlevel 1 (
    echo ERROR: Failed to stage default_config.csv.
    exit /b 1
)

echo === Build manifest, hashes, and ready package ===
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%finalize_telemetry_graceful_stop_build.ps1" -BuildId "%BUILD_ID%" -OutputRoot "%OUTPUT_ROOT%" -SourceRoot "%SOURCE_ROOT%" -PackageRoot "%PACKAGE_ROOT%" -ReadyRoot "%READY_ROOT%" -PackageManifest "%PACKAGE_ROOT%\PACKAGE_MANIFEST.json"
if errorlevel 1 (
    echo ERROR: Build finalization failed.
    exit /b 1
)

echo.
echo Build complete: %OUTPUT_ROOT%
echo Ready package:  %READY_ROOT%
echo Hardware use is NOT authorized by this script. Follow FIELD_TELEMETRY_CSV_PROCEDURE.html.
exit /b 0
