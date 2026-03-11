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

call "%RUN_WITH_VSDEVCMD%" cl /EHsc /W4 /Zi /std:c++17 /I".." FpgaRegisterLogic_test.cpp /Fe:FpgaRegisterLogic_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(FpgaRegisterLogic_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /EHsc /W4 /Zi /std:c++17 /I".." WaveDataFileIO_test.cpp /Fe:WaveDataFileIO_test.exe /link /DEBUG bcrypt.lib
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(WaveDataFileIO_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /EHsc /W4 /Zi /std:c++17 /I".." SavePathValidation_test.cpp /Fe:SavePathValidation_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(SavePathValidation_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /EHsc /W4 /Zi /std:c++17 /I".." AcquisitionPerfMetrics_test.cpp /Fe:AcquisitionPerfMetrics_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(AcquisitionPerfMetrics_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /EHsc /W4 /Zi /std:c++17 /DUNICODE /D_UNICODE /I".." FileLogger_test.cpp /Fe:FileLogger_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(FileLogger_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /EHsc /W4 /Zi /std:c++17 /I".." WaveAcquisitionEngine_test.cpp ..\AnalogBoard_TestApp\WaveAcquisitionEngine.cpp /Fe:WaveAcquisitionEngine_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(WaveAcquisitionEngine_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /EHsc /W4 /Zi /std:c++17 /I".." SimulationRunnerIntegration_test.cpp ..\AnalogBoard_TestApp\WaveAcquisitionEngine.cpp ..\AnalogBoard_SimRunner\SimulationRunnerCore.cpp ..\AnalogBoard_SimRunner\SimulationScenario.cpp /Fe:SimulationRunnerIntegration_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(SimulationRunnerIntegration_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /EHsc /W4 /Zi /std:c++17 /I".." UsbTransferHelpers_test.cpp /Fe:UsbTransferHelpers_test.exe /link /DEBUG
if errorlevel 1 (
    echo.
    echo === Build FAILED ^(UsbTransferHelpers_test^) ===
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

"%SCRIPT_DIR%FileLogger_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(FileLogger_test^) ===
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

"%SCRIPT_DIR%SimulationRunnerIntegration_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(SimulationRunnerIntegration_test^) ===
    call :CleanupIntermediate
    exit /b 1
)

"%SCRIPT_DIR%UsbTransferHelpers_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED ^(UsbTransferHelpers_test^) ===
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
