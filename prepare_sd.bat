@echo off
REM ============================================================
REM  HopFog Node - Prepare SD Card
REM  Usage: prepare_sd.bat [DRIVE_LETTER]
REM
REM  Creates the /hopfog/ directory and empty JSON data files
REM  on the SD card so the ESP32-CAM is ready to go.
REM
REM  The firmware creates these files automatically on boot,
REM  but pre-staging them avoids first-boot delays and lets
REM  you pre-seed user data if desired.
REM
REM  Examples:
REM    prepare_sd.bat E       (SD card mounted as E:\)
REM    prepare_sd.bat D       (SD card mounted as D:\)
REM ============================================================
setlocal

set DRIVE=%1

if "%DRIVE%"=="" (
    echo.
    echo  Usage: prepare_sd.bat DRIVE_LETTER
    echo.
    echo  Example: prepare_sd.bat E
    echo           (where E:\ is your SD card^)
    echo.
    echo  Available drives:
    wmic logicaldisk get name,description 2>nul || echo    (could not list drives^)
    echo.
    exit /b 1
)

REM Remove trailing colon/slash if user typed E: or E:\
set DRIVE=%DRIVE::=%
set DRIVE=%DRIVE:\=%

echo.
echo ============================================
echo  HopFog Node - Preparing SD Card (%DRIVE%:\)
echo ============================================
echo.

if not exist %DRIVE%:\ (
    echo [ERROR] Drive %DRIVE%:\ not found.
    echo         Insert the SD card and check the drive letter.
    exit /b 1
)

echo [1/3] Creating directory %DRIVE%:\hopfog\ ...
if not exist %DRIVE%:\hopfog mkdir %DRIVE%:\hopfog
if errorlevel 1 (
    echo [ERROR] Failed to create directory. Is the card write-protected?
    exit /b 1
)

echo [2/3] Creating empty JSON data files ...

REM Only create files if they don't already exist (preserve existing data)
if not exist %DRIVE%:\hopfog\fog_nodes.json     echo []> %DRIVE%:\hopfog\fog_nodes.json
if not exist %DRIVE%:\hopfog\messages.json       echo []> %DRIVE%:\hopfog\messages.json
if not exist %DRIVE%:\hopfog\stats.json          echo {}> %DRIVE%:\hopfog\stats.json
if not exist %DRIVE%:\hopfog\users.json          echo []> %DRIVE%:\hopfog\users.json
if not exist %DRIVE%:\hopfog\conversations.json  echo []> %DRIVE%:\hopfog\conversations.json
if not exist %DRIVE%:\hopfog\chat_messages.json  echo []> %DRIVE%:\hopfog\chat_messages.json
if not exist %DRIVE%:\hopfog\announcements.json  echo []> %DRIVE%:\hopfog\announcements.json

echo [3/3] Verifying ...
echo.
echo  Files on %DRIVE%:\hopfog\:
dir /b %DRIVE%:\hopfog\
echo.

echo [OK] SD card is ready!
echo      Insert it into the ESP32-CAM and power on.
echo.
