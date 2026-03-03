@echo off
REM ============================================================
REM  HopFog Node - Full Deployment (Build + Flash + SD Card)
REM  Usage: deploy_all.bat [SD_DRIVE] [COM_PORT] [esp32cam|d1_mini]
REM
REM  Examples:
REM    deploy_all.bat E              (SD=E:\, auto-detect COM, ESP32-CAM)
REM    deploy_all.bat E COM3         (SD=E:\, COM3, ESP32-CAM)
REM    deploy_all.bat E COM5 d1_mini (SD=E:\, COM5, D1 Mini)
REM    deploy_all.bat - COM3         (skip SD, COM3, ESP32-CAM)
REM ============================================================
setlocal

set SD_DRIVE=%1
set PORT=%2
set ENV=%3
if "%ENV%"=="" set ENV=esp32cam

echo.
echo ============================================
echo  HopFog Node - Full Deployment
echo  Board: %ENV%
echo ============================================
echo.

REM ---- Step 1: Build ----
echo [Step 1/3] Building firmware ...
echo.

call build.bat %ENV%
if errorlevel 1 (
    echo [DEPLOY] Aborted - build failed.
    exit /b 1
)

REM ---- Step 2: Prepare SD Card ----
if "%SD_DRIVE%"=="-" (
    echo [Step 2/3] Skipping SD card preparation (use "-" to skip^)
    echo.
) else if "%SD_DRIVE%"=="" (
    echo [Step 2/3] Skipping SD card preparation (no drive specified^)
    echo.
) else (
    echo [Step 2/3] Preparing SD card on %SD_DRIVE%:\ ...
    echo.
    call prepare_sd.bat %SD_DRIVE%
    if errorlevel 1 (
        echo [DEPLOY] Warning - SD card preparation failed, continuing ...
        echo.
    )
)

REM ---- Step 3: Flash ----
echo [Step 3/3] Flashing firmware ...
echo.

if "%PORT%"=="" (
    call flash.bat "" %ENV%
) else (
    call flash.bat %PORT% %ENV%
)

if errorlevel 1 (
    echo [DEPLOY] Aborted - flash failed.
    exit /b 1
)

echo.
echo ============================================
echo  Deployment complete!
echo ============================================
echo.
echo  Next steps:
echo    1. Disconnect IO0 from GND (ESP32-CAM only)
echo    2. Insert the SD card into the ESP32-CAM
echo    3. Power on / press RESET
echo    4. Run: monitor.bat     to see serial output
echo    5. Connect to WiFi "HopFog-Network" (password: hopfog123)
echo    6. Open http://hopfog.com/api/health in your browser
echo.
