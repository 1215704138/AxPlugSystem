@echo off
setlocal

echo ========================================
echo AxPlug DEBUG Build (NO Tests)
echo ========================================

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build
set PUBLISH_DIR=%PROJECT_ROOT%\publish

echo.
echo [1/3] Configuring (Tests OFF)...
cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -DAXPLUG_BUILD_TESTS=OFF
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b %errorlevel%
)

echo.
echo [2/3] Building (Debug)...
cmake --build "%BUILD_DIR%" --config Debug
if %errorlevel% neq 0 (
    echo [ERROR] Build -Debug- failed.
    pause
    exit /b %errorlevel%
)

echo.
echo [3/3] Publishing (Debug) to %PUBLISH_DIR%...
cmake --install "%BUILD_DIR%" --config Debug --prefix "%PUBLISH_DIR%"
if %errorlevel% neq 0 (
    echo [ERROR] Install/Publish -Debug- failed.
    pause
    exit /b %errorlevel%
)

echo.
echo ========================================
echo [SUCCESS] DEBUG Build (NO Tests) Complete!
echo Artifacts: %PUBLISH_DIR%
echo ========================================
echo.
pause
