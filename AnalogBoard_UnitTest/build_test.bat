@echo off
for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI\"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT_DIR=%%~fI"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
cd /d "%SCRIPT_DIR%"
cl /EHsc /W4 /Zi /std:c++17 /I".." FpgaRegisterLogic_test.cpp /Fe:FpgaRegisterLogic_test.exe /link /DEBUG
if %ERRORLEVEL% EQU 0 (
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
) else (
    echo.
    echo === Build FAILED ===
)
