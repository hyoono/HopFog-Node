@echo off
REM ============================================================
REM  HopFog Node - Serial Monitor
REM  Usage: monitor.bat [COM_PORT]
REM  Baud rate: 115200 (matches firmware Serial.begin)
REM  Press Ctrl+C to exit.
REM ============================================================
setlocal

set PORT=%1

echo.
echo ============================================
echo  HopFog Node - Serial Monitor (115200 baud)
echo ============================================
echo  Press Ctrl+C to exit.
echo.

where pio >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PlatformIO CLI not found.
    echo         Install it from https://platformio.org/install
    echo         or run: pip install platformio
    exit /b 1
)

if "%PORT%"=="" (
    pio device monitor -b 115200
) else (
    pio device monitor -b 115200 -p %PORT%
)
