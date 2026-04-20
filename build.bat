@echo off
REM build.bat  —  Builds all RFID tools using GCC + MercuryAPI C source
REM
REM ── Configure this path ───────────────────────────────────────────────────
set API=lib\mercuryapi-1.37.2.24\c\src\api
REM ──────────────────────────────────────────────────────────────────────────

REM Verify the API folder exists
if not exist "%API%\tm_reader.h" (
    echo ERROR: Cannot find tm_reader.h at %API%
    echo Check that API= is set correctly in build.bat
    exit /b 1
)

REM Verify GCC is available
where gcc >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: gcc not found on PATH.
    echo Install WinLibs from https://winlibs.com/ and add its bin\ to PATH.
    exit /b 1
)

REM Create output directories
if not exist bin    mkdir bin
if not exist output mkdir output

REM ── MercuryAPI C source files ─────────────────────────────────────────────
set API_SRCS=^
  %API%\tm_reader.c ^
  %API%\tm_reader_async.c ^
  %API%\tmr_param.c ^
  %API%\tmr_utils.c ^
  %API%\tmr_strerror.c ^
  %API%\hex_bytes.c ^
  %API%\serial_reader.c ^
  %API%\serial_reader_l3.c ^
  %API%\serial_transport_win32.c ^
  %API%\osdep_win32.c

REM ── Compiler flags ────────────────────────────────────────────────────────
set CFLAGS=-O2 -Wall -Wno-unused-function -Wno-unused-variable ^
  -Wno-stringop-truncation -Wno-pointer-sign -Wno-array-bounds ^
  -Wno-maybe-uninitialized ^
  -DWIN32 ^
  -DTMR_ENABLE_SERIAL_READER_ONLY ^
  -D_TIMESPEC_DEFINED

set INCLUDES=-I%API% -Isrc
set LIBS=-lpthread -lws2_32

REM ── Build rfid_kit_reader ─────────────────────────────────────────────────
echo Compiling rfid_kit_reader...
gcc %CFLAGS% %INCLUDES% src\rfid_kit_reader.c %API_SRCS% %LIBS% -o bin\rfid_kit_reader.exe
if %ERRORLEVEL% NEQ 0 ( echo FAILED: rfid_kit_reader & exit /b 1 )

REM ── Build rfid_tag_writer ─────────────────────────────────────────────────
echo Compiling rfid_tag_writer...
gcc %CFLAGS% %INCLUDES% src\rfid_tag_writer.c %API_SRCS% %LIBS% -o bin\rfid_tag_writer.exe
if %ERRORLEVEL% NEQ 0 ( echo FAILED: rfid_tag_writer & exit /b 1 )

REM ── Build rfid_server (GUI backend) ──────────────────────────────────────
echo Compiling rfid_server...
gcc %CFLAGS% %INCLUDES% src\rfid_server.c %API_SRCS% %LIBS% -o bin\rfid_server.exe
if %ERRORLEVEL% NEQ 0 ( echo FAILED: rfid_server & exit /b 1 )

REM ── Build rfid_diag ───────────────────────────────────────────────────────
echo Compiling rfid_diag...
gcc %CFLAGS% %INCLUDES% src\rfid_diag.c %API_SRCS% %LIBS% -o bin\rfid_diag.exe
if %ERRORLEVEL% NEQ 0 ( echo FAILED: rfid_diag & exit /b 1 )

REM ── Build generate_test_data (no RFID libs needed) ────────────────────────
echo Compiling generate_test_data...
gcc -O2 -Wall src\generate_test_data.c -o bin\generate_test_data.exe
if %ERRORLEVEL% NEQ 0 ( echo FAILED: generate_test_data & exit /b 1 )

echo.
echo Build successful!  Executables are in bin\
echo.
echo --- GUI (recommended) -------------------------------------------
echo   1. Run bin\rfid_server.exe  (browser opens automatically)
echo   2. Enter COM port and power, click Connect
echo   3. Hold tags near antenna, click Scan Tags
echo   4. Enter part number, click Write Part  (Phase 1)
echo   5. Group parts into kits, enter kit number, click Write Kit (Phase 2)
echo   6. Click Export CSV to save inventory
echo.
echo --- Command line tools ------------------------------------------
echo   Scan tags to CSV:
echo     bin\rfid_kit_reader.exe COM3
echo     bin\rfid_kit_reader.exe COM3 --power 2200
echo.
echo   Write tags interactively:
echo     bin\rfid_tag_writer.exe COM3
echo     bin\rfid_tag_writer.exe COM3 --phase1
echo     bin\rfid_tag_writer.exe COM3 --phase2
echo.
echo   Diagnose power and temperature:
echo     bin\rfid_diag.exe COM3
echo.
echo   Generate test CSVs (no hardware needed):
echo     bin\generate_test_data.exe
echo.
echo --- Notes -------------------------------------------------------
echo   MercuryAPI : mercuryapi-1.37.2.24
echo   Max power  : 2700 cdBm (27.00 dBm) - requires 5V 1A supply
echo   USB safe   : 2200 cdBm (22.00 dBm) - stable on laptop USB
echo   Antenna    : onboard PCB ~0.5m range; uFL external up to 5m
