@echo off
setlocal

echo ========================================
echo AxPlug Test Build Script
echo ========================================

set PROJECT_ROOT=%~dp0..
set TEST_DIR=%PROJECT_ROOT%\test
set TEST_BUILD_DIR=%TEST_DIR%\build
set PUBLISH_DIR=%PROJECT_ROOT%\publish

if not exist "%PUBLISH_DIR%\bin\AxCore.dll" (
    echo [ERROR] Publish folder not found. Please run 'scripts/build_publish.bat' first.
    pause
    exit /b 1
)

echo.
echo [1/2] Configuring Test Project...
cmake -S "%TEST_DIR%" -B "%TEST_BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b %errorlevel%
)

echo.
echo [2/2] Building Test Project (Debug)...
cmake --build "%TEST_BUILD_DIR%" --config Debug
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    pause
    exit /b %errorlevel%
)

echo.
echo ========================================
echo [SUCCESS] Test Build Complete!
echo Executables are in: %TEST_BUILD_DIR%\bin
echo ========================================
echo.
pause
