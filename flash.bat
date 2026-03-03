@echo off
REM ============================================================
REM  HopFog Node - Flash firmware to ESP32-CAM
REM  Usage: flash.bat [COM_PORT] [esp32cam|d1_mini]
REM
REM  Examples:
REM    flash.bat              (auto-detect port, ESP32-CAM)
REM    flash.bat COM3         (specific port, ESP32-CAM)
REM    flash.bat COM5 d1_mini (specific port, D1 Mini)
REM
REM  NOTE for ESP32-CAM: connect IO0 to GND before powering on,
REM        then press RESET. After flashing, disconnect IO0.
REM ============================================================
setlocal

set PORT=%1
set ENV=%2
if "%ENV%"=="" set ENV=esp32cam

echo.
echo ============================================
echo  HopFog Node - Flashing %ENV%
echo ============================================
echo.

where pio >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PlatformIO CLI not found.
    echo         Install it from https://platformio.org/install
    echo         or run: pip install platformio
    exit /b 1
)

if "%PORT%"=="" (
    echo [INFO] No COM port specified - PlatformIO will auto-detect.
    echo.
    pio run -e %ENV% -t upload
) else (
    echo [INFO] Using port %PORT%
    echo.
    pio run -e %ENV% -t upload --upload-port %PORT%
)

if errorlevel 1 (
    echo.
    echo [ERROR] Flash failed!
    echo.
    echo Troubleshooting:
    echo   ESP32-CAM: Make sure IO0 is connected to GND and press RESET
    echo   D1 Mini:   Check USB cable is data-capable (not charge-only^)
    echo   Both:      Check the correct COM port with: pio device list
    exit /b 1
)

echo.
echo [OK] Firmware flashed successfully!
echo      Remember to disconnect IO0 from GND (ESP32-CAM) and press RESET.
echo.
