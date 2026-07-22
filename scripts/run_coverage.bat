@echo off
setlocal

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "COVERAGE_DIR=%ROOT%\coverage"
set "COVERAGE_XML=%COVERAGE_DIR%\fpga_coverage.xml"
set "TEST_EXE=%ROOT%\AnalogBoard_UnitTest\FpgaRegisterLogic_test.exe"
set "TEST_PDB=%ROOT%\AnalogBoard_UnitTest\FpgaRegisterLogic_test.pdb"
set "COPIED_TEST_PDB=%ROOT%\AnalogBoard_UnitTest\x64\Debug\AnalogBoard_UnitTest.pdb"
set "TESTAPP_SRC=%ROOT%\AnalogBoard_TestApp"
set "UNITTEST_SRC=%ROOT%\AnalogBoard_UnitTest"

if not exist "%COVERAGE_DIR%" mkdir "%COVERAGE_DIR%"

call "%ROOT%\AnalogBoard_UnitTest\build_test.bat"
if errorlevel 1 (
    echo ERROR: Unit test build or execution failed.
    exit /b 1
)

if not exist "%COPIED_TEST_PDB%" (
    echo ERROR: Coverage symbols are missing: %COPIED_TEST_PDB%
    exit /b 1
)
copy /Y "%COPIED_TEST_PDB%" "%TEST_PDB%" > nul
if errorlevel 1 (
    echo ERROR: Failed to restore coverage symbols.
    exit /b 1
)

OpenCppCoverage ^
  --modules "%TEST_EXE%" ^
  --sources "%TESTAPP_SRC%" ^
  --excluded_sources "%UNITTEST_SRC%" ^
  --export_type cobertura:%COVERAGE_XML% ^
  -- "%TEST_EXE%"
if errorlevel 1 (
    echo ERROR: Coverage collection failed.
    call :CleanupCoverageSymbols
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$threshold=80; [xml]$x=Get-Content '%COVERAGE_XML%';" ^
  "$lc=[int]$x.coverage.'lines-covered'; $lv=[int]$x.coverage.'lines-valid';" ^
  "if($lv -eq 0){$rate=0}else{$rate=$lc*100.0/$lv};" ^
  "Write-Host ('Line coverage: {0}/{1} ({2:N2} pct), threshold: {3} pct' -f $lc,$lv,$rate,$threshold);" ^
  "if($lv -le 0 -or $rate -lt $threshold){exit 1}else{exit 0}"
if errorlevel 1 (
    echo ERROR: Coverage is below threshold.
    call :CleanupCoverageSymbols
    exit /b 1
)

call :CleanupCoverageSymbols
echo SUCCESS: Coverage is above threshold.
exit /b 0

:CleanupCoverageSymbols
del /q "%TEST_PDB%" > nul 2>&1
exit /b 0
