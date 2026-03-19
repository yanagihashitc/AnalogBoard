@echo off
setlocal

for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI\"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT_DIR=%%~fI"
set "RUN_WITH_VSDEVCMD=%ROOT_DIR%\scripts\run_with_vsdevcmd.bat"

if not exist "%RUN_WITH_VSDEVCMD%" (
    echo.
    echo === Build FAILED: scripts\run_with_vsdevcmd.bat not found ===
    exit /b 1
)

cd /d "%SCRIPT_DIR%"

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:FpgaRegisterLogic_test.pdb FpgaRegisterLogic_test.cpp /Fe:FpgaRegisterLogic_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(FpgaRegisterLogic_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:WaveDataFileIO_test.pdb WaveDataFileIO_test.cpp /Fe:WaveDataFileIO_test.exe /link /DEBUG bcrypt.lib
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(WaveDataFileIO_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:SavePathValidation_test.pdb SavePathValidation_test.cpp /Fe:SavePathValidation_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(SavePathValidation_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:AcquisitionPerfMetrics_test.pdb AcquisitionPerfMetrics_test.cpp /Fe:AcquisitionPerfMetrics_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(AcquisitionPerfMetrics_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:AcquisitionCompletionLogic_test.pdb AcquisitionCompletionLogic_test.cpp /Fe:AcquisitionCompletionLogic_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(AcquisitionCompletionLogic_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:BlockingQueue_test.pdb BlockingQueue_test.cpp ..\AnalogBoard_TestApp\WaveAcquisitionEngine.cpp /Fe:BlockingQueue_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(BlockingQueue_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:WaveAcquisitionEngine_test.pdb WaveAcquisitionEngine_test.cpp ..\AnalogBoard_TestApp\WaveAcquisitionEngine.cpp /Fe:WaveAcquisitionEngine_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(WaveAcquisitionEngine_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:AcquisitionLogMessageFormatter_test.pdb AcquisitionLogMessageFormatter_test.cpp ..\AnalogBoard_TestApp\WaveAcquisitionEngine.cpp /Fe:AcquisitionLogMessageFormatter_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(AcquisitionLogMessageFormatter_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:AcquisitionRunMetadata_test.pdb AcquisitionRunMetadata_test.cpp ..\AnalogBoard_TestApp\WaveAcquisitionEngine.cpp /Fe:AcquisitionRunMetadata_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(AcquisitionRunMetadata_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:Ep6TransferRetryPolicy_test.pdb Ep6TransferRetryPolicy_test.cpp /Fe:Ep6TransferRetryPolicy_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(Ep6TransferRetryPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:ReadRequestBurstPolicy_test.pdb ReadRequestBurstPolicy_test.cpp /Fe:ReadRequestBurstPolicy_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(ReadRequestBurstPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /DUNICODE /D_UNICODE /I".." /Fd:FileLogger_test.pdb FileLogger_test.cpp /Fe:FileLogger_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(FileLogger_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:FileIoLoggingPolicy_test.pdb FileIoLoggingPolicy_test.cpp /Fe:FileIoLoggingPolicy_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(FileIoLoggingPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:WavePairPublishPolicy_test.pdb WavePairPublishPolicy_test.cpp /Fe:WavePairPublishPolicy_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(WavePairPublishPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:DialogMainBindingPolicy_test.pdb DialogMainBindingPolicy_test.cpp /Fe:DialogMainBindingPolicy_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(DialogMainBindingPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /FS /EHsc /W4 /Zi /std:c++17 /I".." /Fd:AcquisitionCycleRecoveryPolicy_test.pdb AcquisitionCycleRecoveryPolicy_test.cpp /Fe:AcquisitionCycleRecoveryPolicy_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(AcquisitionCycleRecoveryPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

if not exist "%SCRIPT_DIR%x64\Debug" mkdir "%SCRIPT_DIR%x64\Debug"
if not exist "%ROOT_DIR%\x64\Debug" mkdir "%ROOT_DIR%\x64\Debug"
copy /Y "%SCRIPT_DIR%FpgaRegisterLogic_test.exe" "%SCRIPT_DIR%x64\Debug\AnalogBoard_UnitTest.exe" > nul
copy /Y "%SCRIPT_DIR%FpgaRegisterLogic_test.exe" "%ROOT_DIR%\x64\Debug\AnalogBoard_UnitTest.exe" > nul
if exist "%SCRIPT_DIR%FpgaRegisterLogic_test.pdb" copy /Y "%SCRIPT_DIR%FpgaRegisterLogic_test.pdb" "%SCRIPT_DIR%x64\Debug\AnalogBoard_UnitTest.pdb" > nul
if exist "%SCRIPT_DIR%FpgaRegisterLogic_test.pdb" copy /Y "%SCRIPT_DIR%FpgaRegisterLogic_test.pdb" "%ROOT_DIR%\x64\Debug\AnalogBoard_UnitTest.pdb" > nul

echo.
echo === Build succeeded. Running tests... ===
echo.
"%SCRIPT_DIR%FpgaRegisterLogic_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(FpgaRegisterLogic_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%WaveDataFileIO_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(WaveDataFileIO_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%SavePathValidation_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(SavePathValidation_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%AcquisitionPerfMetrics_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(AcquisitionPerfMetrics_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%AcquisitionCompletionLogic_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(AcquisitionCompletionLogic_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%BlockingQueue_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(BlockingQueue_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%WaveAcquisitionEngine_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(WaveAcquisitionEngine_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%AcquisitionLogMessageFormatter_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(AcquisitionLogMessageFormatter_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%AcquisitionRunMetadata_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(AcquisitionRunMetadata_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%Ep6TransferRetryPolicy_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(Ep6TransferRetryPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%ReadRequestBurstPolicy_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(ReadRequestBurstPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%FileLogger_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(FileLogger_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%FileIoLoggingPolicy_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(FileIoLoggingPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%WavePairPublishPolicy_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(WavePairPublishPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%DialogMainBindingPolicy_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(DialogMainBindingPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%AcquisitionCycleRecoveryPolicy_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(AcquisitionCycleRecoveryPolicy_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call :CleanupIntermediate
exit /b 0

:CleanupIntermediate
del /q "%SCRIPT_DIR%*.obj" > nul 2>&1
del /q "%SCRIPT_DIR%*.ilk" > nul 2>&1
del /q "%SCRIPT_DIR%*.pdb" > nul 2>&1
del /q "%SCRIPT_DIR%vc140.pdb" > nul 2>&1
for /d %%D in ("%SCRIPT_DIR%tmp_wave_data_io_*") do rd /s /q "%%~fD" > nul 2>&1
for /d %%D in ("%ROOT_DIR%\tmp_wave_data_io_*") do rd /s /q "%%~fD" > nul 2>&1
for /d %%D in ("%TEMP%\fl_*") do rd /s /q "%%~fD" > nul 2>&1
exit /b 0
