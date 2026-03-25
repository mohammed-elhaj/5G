@echo off
REM ============================================================
REM run_profiling.bat — Automated Profiling Suite (Windows)
REM
REM Runs the 5G Layer 2 simulator with multiple combinations of:
REM   - TB sizes: 256, 512, 1024, 2048, 4096, 8192 bytes
REM   - Packet sizes: 100, 500, 1000, 1400, 3000 bytes
REM
REM All results are appended to profiling_results.csv
REM ============================================================

setlocal enabledelayedexpansion

REM Configuration
set BINARY=5g_layer2.exe
set OUTPUT_CSV=profiling_results.csv
set NUM_PACKETS=10

REM Check if binary exists
if not exist "%BINARY%" (
    echo ERROR: Binary %BINARY% not found!
    echo Please build the project first.
    exit /b 1
)

echo ========================================
echo   5G Layer 2 Profiling Suite
echo ========================================
echo.

REM Remove old CSV if it exists
if exist "%OUTPUT_CSV%" (
    echo Removing old %OUTPUT_CSV%
    del "%OUTPUT_CSV%"
)

REM Define arrays (using space-separated values)
set TB_SIZES=256 512 1024 2048 4096 8192
set PACKET_SIZES=100 500 1000 1400 3000

set CURRENT_RUN=0
set TOTAL_RUNS=30

echo Running %TOTAL_RUNS% profiling configurations...
echo.

REM Create temp directory
set TEMP_DIR=%TEMP%\profiling_%RANDOM%
mkdir "%TEMP_DIR%"

REM Run all combinations
for %%T in (%TB_SIZES%) do (
    for %%P in (%PACKET_SIZES%) do (
        set /a CURRENT_RUN+=1
        
        REM Skip configurations where packet + overhead exceeds TB
        REM Overhead estimate: PDCP (7-12) + RLC (2-3) + MAC (2-3) = ~20 bytes
        set /a MIN_TB_SIZE=%%P + 20
        if %%T LSS !MIN_TB_SIZE! (
            echo [!CURRENT_RUN!/%TOTAL_RUNS%] TB=%%T, Packet=%%P [SKIPPED - TB too small]
        ) else (
            echo [!CURRENT_RUN!/%TOTAL_RUNS%] TB=%%T, Packet=%%P
            
            REM Run the simulator (suppress output)
            %BINARY% --tb-size %%T --packet-size %%P --num-packets %NUM_PACKETS% > nul 2>&1
            
            REM Move generated CSV to temp location
            if exist "%OUTPUT_CSV%" (
                move /y "%OUTPUT_CSV%" "%TEMP_DIR%\run_!CURRENT_RUN!.csv" > nul
            )
        )
    )
)

echo.
echo Merging results...

REM Merge all CSVs
set FIRST=1
for %%F in ("%TEMP_DIR%\run_*.csv") do (
    if !FIRST!==1 (
        type "%%F" >> "%OUTPUT_CSV%"
        set FIRST=0
    ) else (
        REM Skip first line (header) for subsequent files
        more +1 "%%F" >> "%OUTPUT_CSV%"
    )
)

REM Count records
set TOTAL_RECORDS=0
for /f %%A in ('type "%OUTPUT_CSV%" ^| find /c /v ""') do set TOTAL_RECORDS=%%A
set /a TOTAL_RECORDS-=1

REM Cleanup
rmdir /s /q "%TEMP_DIR%"

echo.
echo ========================================
echo Profiling Complete!
echo ========================================
echo   Total configurations: %TOTAL_RUNS%
echo   Total packets tested: %TOTAL_RECORDS%
echo   Output file: %OUTPUT_CSV%
echo.
echo Next steps:
echo   1. Generate charts: python scripts\generate_charts.py
echo   2. Or use: cmake --build . --target profile
echo.

endlocal
