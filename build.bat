@echo off
REM ============================================================
REM  HopFog Node - Build firmware
REM  Usage: build.bat [esp32cam|d1_mini]
REM  Default: esp32cam
REM ============================================================
setlocal

set ENV=%1
if "%ENV%"=="" set ENV=esp32cam

echo.
echo ============================================
echo  HopFog Node - Building for %ENV%
echo ============================================
echo.

where pio >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PlatformIO CLI not found.
    echo         Install it from https://platformio.org/install
    echo         or run: pip install platformio
    exit /b 1
)

pio run -e %ENV%

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    exit /b 1
)

echo.
echo [OK] Build succeeded for %ENV%
echo      Firmware: .pio\build\%ENV%\firmware.bin
echo.
