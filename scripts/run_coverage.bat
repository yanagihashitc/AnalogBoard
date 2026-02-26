@echo off
setlocal enabledelayedexpansion

REM Run from repository root.
pushd "%~dp0\.." >nul

set "CONFIG=Debug"
set "PLATFORM=x64"
set "SOLUTION=AnalogBoard_TestApp.sln"
set "COVERAGE_DIR=coverage"
set "COVERAGE_XML=%COVERAGE_DIR%\coverage.xml"
set "TEST_EXE=x64\%CONFIG%\AnalogBoard_UnitTest.exe"

REM Project files are maintained as *.vcxproj.xml in this repository.
REM This script always regenerates *.vcxproj from XML before building.
echo [1/5] Preparing .vcxproj files from .vcxproj.xml...
copy /Y "AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj.xml" "AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj" >nul
if errorlevel 1 goto :error
copy /Y "AnalogBoard_Dll\AnalogBoard_Dll.vcxproj.xml" "AnalogBoard_Dll\AnalogBoard_Dll.vcxproj" >nul
if errorlevel 1 goto :error
copy /Y "AnalogBoard_UnitTest\AnalogBoard_UnitTest.vcxproj.xml" "AnalogBoard_UnitTest\AnalogBoard_UnitTest.vcxproj" >nul
if errorlevel 1 goto :error

echo [2/5] Checking required tools...
where msbuild >nul 2>&1
if errorlevel 1 (
  echo ERROR: msbuild is not found. Use "x64 Native Tools Command Prompt for VS 2022".
  goto :error
)

where OpenCppCoverage >nul 2>&1
if errorlevel 1 (
  echo ERROR: OpenCppCoverage is not found in PATH.
  echo        Install: https://github.com/OpenCppCoverage/OpenCppCoverage/releases
  goto :error
)

echo [3/5] Building DLL and UnitTest (%CONFIG%/%PLATFORM%)...
msbuild "%SOLUTION%" /t:AnalogBoard_Dll:Rebuild /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m:1
if errorlevel 1 goto :error
msbuild "%SOLUTION%" /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m:1
if errorlevel 1 goto :error

if not exist "%TEST_EXE%" (
  echo ERROR: Test executable was not generated: %TEST_EXE%
  goto :error
)

if not exist "%COVERAGE_DIR%" mkdir "%COVERAGE_DIR%"

echo [4/5] Running coverage...
OpenCppCoverage ^
  --sources "%CD%\AnalogBoard_Dll" ^
  --sources "%CD%\AnalogBoard_TestApp" ^
  --excluded_sources "%CD%\AnalogBoard_UnitTest" ^
  --export_type cobertura:"%CD%\%COVERAGE_XML%" ^
  -- "%CD%\%TEST_EXE%"
if errorlevel 1 goto :error

echo [5/5] Coverage summary...
powershell -NoProfile -Command ^
  "$p='%COVERAGE_XML%'; if (-not (Test-Path $p)) { throw 'coverage.xml not found.' }; [xml]$x=Get-Content $p; $valid=[int]$x.coverage.'lines-valid'; if ($valid -le 0) { throw 'No valid lines were collected. Check --sources/--excluded_sources.' }; '{0:N2}%% ({1}/{2} lines)' -f ([double]$x.coverage.'line-rate'*100), [int]$x.coverage.'lines-covered', $valid"
if errorlevel 1 goto :error

echo.
echo Completed. Coverage XML: %COVERAGE_XML%
echo Optional HTML report:
echo   reportgenerator -reports:%COVERAGE_XML% -targetdir:%COVERAGE_DIR%\html -reporttypes:HtmlSummary

popd >nul
exit /b 0

:error
echo.
echo Failed.
popd >nul
exit /b 1
