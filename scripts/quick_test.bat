@echo off
REM ============================================================
REM quick_test.bat — Quick Profiling Test (Windows)
REM
REM Runs a minimal profiling test (2 configs) to verify
REM the infrastructure works before running the full suite.
REM ============================================================

set BINARY=5g_layer2.exe
set OUTPUT_CSV=profiling_results.csv

echo ========================================
echo   Quick Profiling Test
echo ========================================
echo.

if not exist "%BINARY%" (
    echo ERROR: Binary %BINARY% not found!
    echo Build the project first.
    exit /b 1
)

if exist "%OUTPUT_CSV%" del "%OUTPUT_CSV%"

echo Running 2 test configurations...
echo.

REM Test 1
echo [1/2] TB=512, Packet=500
%BINARY% --tb-size 512 --packet-size 500 --num-packets 5 > nul 2>&1
move /y "%OUTPUT_CSV%" temp1.csv > nul

REM Test 2
echo [2/2] TB=2048, Packet=1400
%BINARY% --tb-size 2048 --packet-size 1400 --num-packets 5 > nul 2>&1
move /y "%OUTPUT_CSV%" temp2.csv > nul

REM Merge
type temp1.csv > "%OUTPUT_CSV%"
more +1 temp2.csv >> "%OUTPUT_CSV%"
del temp1.csv temp2.csv

echo.
echo Test complete! Generated %OUTPUT_CSV% with 10 records
echo.
echo Next: python scripts\generate_charts.py
echo.
