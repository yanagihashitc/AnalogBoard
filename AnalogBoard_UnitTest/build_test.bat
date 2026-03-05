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
if not %ERRORLEVEL% EQU 0 (
    echo.
    echo === Build FAILED (FpgaRegisterLogic_test) ===
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /EHsc /W4 /Zi /std:c++17 /I".." WaveDataFileIO_test.cpp /Fe:WaveDataFileIO_test.exe /link /DEBUG bcrypt.lib
if not %ERRORLEVEL% EQU 0 (
    echo.
    echo === Build FAILED (WaveDataFileIO_test) ===
    exit /b 1
)

call "%RUN_WITH_VSDEVCMD%" cl /EHsc /W4 /Zi /std:c++17 /I".." SavePathValidation_test.cpp /Fe:SavePathValidation_test.exe /link /DEBUG
if not %ERRORLEVEL% EQU 0 (
    echo.
    echo === Build FAILED (SavePathValidation_test) ===
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
    echo === Tests FAILED (FpgaRegisterLogic_test) ===
    exit /b 1
)

"%SCRIPT_DIR%WaveDataFileIO_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED (WaveDataFileIO_test) ===
    exit /b 1
)

"%SCRIPT_DIR%SavePathValidation_test.exe"
if errorlevel 1 (
    echo.
    echo === Tests FAILED (SavePathValidation_test) ===
    exit /b 1
)

exit /b 0
