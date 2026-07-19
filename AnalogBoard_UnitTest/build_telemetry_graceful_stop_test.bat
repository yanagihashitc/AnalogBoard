@echo off
setlocal

if "%~1"=="" (
    echo Usage: %~nx0 isolated-output-directory
    exit /b 1
)

for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI\"
for %%I in ("%SCRIPT_DIR%..") do set "SOURCE_ROOT=%%~fI"
for %%I in ("%~1") do set "OUTPUT_DIR=%%~fI"
set "RUN_WITH_VSDEVCMD=%SOURCE_ROOT%\scripts\run_with_vsdevcmd.bat"

if not exist "%RUN_WITH_VSDEVCMD%" (
    echo ERROR: Visual Studio wrapper not found: %RUN_WITH_VSDEVCMD%
    exit /b 1
)

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if errorlevel 1 exit /b 1

call :BuildAndRun AcquisitionShutdownCoordinator_test.cpp AcquisitionShutdownCoordinator_test
if errorlevel 1 exit /b 1

call :BuildAndRun RearmTelemetry_test.cpp RearmTelemetry_test
if errorlevel 1 exit /b 1

call :BuildAndRun RearmTelemetryReplay_test.cpp RearmTelemetryReplay_test
if errorlevel 1 exit /b 1

call :BuildAndRun ExternalTriggerPollingPolicy_test.cpp ExternalTriggerPollingPolicy_test
if errorlevel 1 exit /b 1

call :BuildAndRun AcquisitionCompletionLogic_test.cpp AcquisitionCompletionLogic_test
if errorlevel 1 exit /b 1

call :BuildAndRun Ep4FailureDiagnostic_test.cpp Ep4FailureDiagnostic_test
if errorlevel 1 exit /b 1

call :BuildAndRun Ep6TransferRetryPolicy_test.cpp Ep6TransferRetryPolicy_test
if errorlevel 1 exit /b 1

call :BuildAndRun WavePairPublishPolicy_test.cpp WavePairPublishPolicy_test
if errorlevel 1 exit /b 1

echo All telemetry graceful-stop gate tests passed.
exit /b 0

:BuildAndRun
set "TEST_SOURCE=%~1"
set "TEST_NAME=%~2"

echo Building %TEST_NAME%...
call "%RUN_WITH_VSDEVCMD%" cl /nologo /EHsc /W4 /Zi /std:c++17 /utf-8 /I"%SOURCE_ROOT%" "%SCRIPT_DIR%%TEST_SOURCE%" "/Fo:%OUTPUT_DIR%\%TEST_NAME%.obj" "/Fe:%OUTPUT_DIR%\%TEST_NAME%.exe" "/Fd:%OUTPUT_DIR%\%TEST_NAME%.pdb" /link /DEBUG
if errorlevel 1 (
    echo ERROR: Build failed ^(%TEST_NAME%^).
    exit /b 1
)

echo Running %TEST_NAME%...
"%OUTPUT_DIR%\%TEST_NAME%.exe"
if errorlevel 1 (
    echo ERROR: Tests failed ^(%TEST_NAME%^).
    exit /b 1
)
exit /b 0
