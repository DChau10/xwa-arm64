@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo   xwa-arm64 Build Script (Windows on ARM64)
echo =======================================================

set "VCVARS="

:: Check standard Visual Studio / Build Tools locations
for %%D in (
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
    "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise"
) do (
    if not defined VCVARS (
        if exist "%%~D\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARS=%%~D\VC\Auxiliary\Build\vcvarsall.bat"
        )
    )
)

:: If not found in standard paths, query vswhere
if not defined VCVARS (
    set "VSWHERE_BIN=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE_BIN!" (
        for /f "tokens=*" %%i in ('"!VSWHERE_BIN!" -latest -products * -property installationPath') do (
            if exist "%%i\VC\Auxiliary\Build\vcvarsall.bat" (
                set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
            )
        )
    )
)

if not defined VCVARS (
    echo [ERROR] Could not find Visual Studio 2022 or Build Tools vcvarsall.bat.
    echo Please install Visual Studio 2022 Build Tools with "Desktop development with C++".
    exit /b 1
)

echo [*] Using MSVC environment from:
echo     %VCVARS%
echo.
echo [*] Initializing MSVC ARM64 developer environment...
call "%VCVARS%" arm64
if errorlevel 1 (
    echo [ERROR] Failed to initialize ARM64 developer environment.
    exit /b 1
)

echo.
echo [*] Configuring CMake project (ARM64 Release)...
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

echo.
echo [*] Compiling xwa-arm64 (Release ARM64)...
cmake --build build --config Release
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo =======================================================
echo [SUCCESS] Build completed successfully!
echo Executable: bin\xwa-arm64.exe
echo =======================================================
