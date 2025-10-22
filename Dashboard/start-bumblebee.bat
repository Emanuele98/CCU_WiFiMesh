@echo off
REM ================================================================
REM BUMBLEBEE WIRELESS POWER TRANSFER MONITORING SYSTEM
REM Windows Docker Compose Management Script
REM ================================================================

echo.
echo ========================================================
echo    BUMBLEBEE MONITORING SYSTEM - LAUNCHER
echo ========================================================
echo.

REM Check if Docker is running
docker info >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Docker is not running!
    echo Please start Docker Desktop and try again.
    pause
    exit /b 1
)

echo [OK] Docker is running
echo.

REM Get the script directory
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

echo Working directory: %CD%
echo.

REM Create necessary directories if they don't exist
if not exist "mosquitto\config" (
    echo [SETUP] Creating directory structure...
    mkdir mosquitto\config 2>nul
    mkdir mosquitto\data 2>nul
    mkdir mosquitto\log 2>nul
    mkdir telegraf 2>nul
    echo [OK] Directories created
    echo.
)

REM Display menu
:MENU
echo ========================================================
echo MAIN MENU
echo ========================================================
echo.
echo 1. Start Bumblebee Stack
echo 2. Stop Bumblebee Stack
echo 3. Restart Bumblebee Stack
echo 4. View Status
echo 5. View Logs (All Services)
echo 6. View Logs (Specific Service)
echo 7. Open Dashboard (Browser)
echo 8. Clean Everything (DANGER: Deletes all data)
echo 9. Exit
echo.
set /p choice="Enter your choice (1-9): "

if "%choice%"=="1" goto START
if "%choice%"=="2" goto STOP
if "%choice%"=="3" goto RESTART
if "%choice%"=="4" goto STATUS
if "%choice%"=="5" goto LOGS_ALL
if "%choice%"=="6" goto LOGS_SPECIFIC
if "%choice%"=="7" goto OPEN_DASHBOARD
if "%choice%"=="8" goto CLEAN
if "%choice%"=="9" goto EXIT

echo Invalid choice! Please try again.
echo.
goto MENU

:START
echo.
echo ========================================================
echo STARTING BUMBLEBEE STACK
echo ========================================================
echo.

REM Check if docker-compose.yml exists
if not exist "docker-compose.yml" (
    echo [ERROR] docker-compose.yml not found!
    echo Please ensure all configuration files are in place.
    pause
    goto MENU
)

echo [STARTING] Launching all services...
echo.
docker compose up -d

if errorlevel 1 (
    echo.
    echo [ERROR] Failed to start containers!
    echo Check the logs for more information.
    pause
    goto MENU
)

echo.
echo [SUCCESS] Bumblebee Stack is running!
echo.
echo ========================================================
echo            ACCESS INFORMATION
echo ========================================================
echo.
echo Node-RED Dashboard:  http://localhost:1880/ui
echo Node-RED Editor:     http://localhost:1880
echo InfluxDB UI:         http://localhost:8086
echo MQTT Broker:         localhost:1883
echo.
echo Default InfluxDB credentials:
echo   Username: admin
echo   Password: bumblebee2024
echo.
echo ========================================================
echo.
echo Next steps:
echo 1. Import the Node-RED flow (see SETUP-GUIDE-WINDOWS.md)
echo 2. Configure your ESP32 devices to connect to this PC's IP
echo 3. Access the dashboard at http://localhost:1880/ui
echo.
pause
goto MENU

:STOP
echo.
echo ========================================================
echo STOPPING BUMBLEBEE STACK
echo ========================================================
echo.
echo [STOPPING] Shutting down all services...
docker compose down
echo.
echo [OK] Stack stopped
echo.
pause
goto MENU

:RESTART
echo.
echo ========================================================
echo RESTARTING BUMBLEBEE STACK
echo ========================================================
echo.
echo [RESTARTING] Restarting all services...
docker compose restart
echo.
echo [OK] Stack restarted
echo.
pause
goto MENU

:STATUS
echo.
echo ========================================================
echo CONTAINER STATUS
echo ========================================================
echo.
docker compose ps
echo.
echo ========================================================
echo RESOURCE USAGE
echo ========================================================
echo.
docker stats --no-stream
echo.
pause
goto MENU

:LOGS_ALL
echo.
echo ========================================================
echo VIEWING LOGS - ALL SERVICES
echo ========================================================
echo Press Ctrl+C to stop viewing logs
echo.
timeout /t 3
docker compose logs -f
goto MENU

:LOGS_SPECIFIC
echo.
echo ========================================================
echo VIEWING LOGS - SPECIFIC SERVICE
echo ========================================================
echo.
echo Available services:
echo 1. mosquitto (MQTT Broker)
echo 2. influxdb (Database)
echo 3. telegraf (Data Bridge)
echo 4. nodered (Dashboard)
echo 5. Back to Main Menu
echo.
set /p service_choice="Choose service (1-5): "

if "%service_choice%"=="1" set SERVICE_NAME=mosquitto
if "%service_choice%"=="2" set SERVICE_NAME=influxdb
if "%service_choice%"=="3" set SERVICE_NAME=telegraf
if "%service_choice%"=="4" set SERVICE_NAME=nodered
if "%service_choice%"=="5" goto MENU

if not defined SERVICE_NAME (
    echo Invalid choice!
    pause
    goto LOGS_SPECIFIC
)

echo.
echo [LOGS] Showing logs for %SERVICE_NAME%
echo Press Ctrl+C to stop viewing logs
echo.
timeout /t 3
docker compose logs -f %SERVICE_NAME%
set SERVICE_NAME=
goto MENU

:OPEN_DASHBOARD
echo.
echo ========================================================
echo OPENING DASHBOARD
echo ========================================================
echo.
echo Opening dashboard in your default browser...
start http://localhost:1880/ui
echo.
echo If the dashboard doesn't open:
echo - Visit http://localhost:1880/ui manually
echo - Ensure the stack is running (Option 1 or 4)
echo.
pause
goto MENU

:CLEAN
echo.
echo ========================================================
echo                    WARNING!
echo ========================================================
echo This will DELETE ALL DATA including:
echo   - All MQTT messages
echo   - All InfluxDB time-series data
echo   - All Node-RED flows (if not backed up)
echo   - All logs
echo.
echo Make sure you have backed up any important data!
echo.
set /p confirm="Type YES to confirm deletion: "

if not "%confirm%"=="YES" (
    echo.
    echo Cancelled. No data was deleted.
    pause
    goto MENU
)

echo.
echo [CLEANING] Stopping and removing all containers and volumes...
docker compose down -v

if errorlevel 1 (
    echo.
    echo [WARNING] Some errors occurred during cleanup.
    pause
    goto MENU
)

echo [OK] All containers and volumes removed
echo.
echo Note: Configuration files were preserved.
echo.
pause
goto MENU

:EXIT
echo.
echo ========================================================
echo Goodbye!
echo ========================================================
echo.
echo Thank you for using Bumblebee Monitoring System
echo.
timeout /t 2
exit /b 0
