@echo off
REM Komorebi: Protocol Zero-Seven — Windows Win64 (DX12 / Vulkan SM5) Build & Packaging Script
REM Targets: Win64 Client + Win64 Dedicated Server
REM
REM Environment variables (all optional):
REM   UE_ROOT       Path to Unreal Engine 5.4/5.5 root (auto-detected if unset)
REM   BLENDER       Path to Blender 4.2 LTS blender.exe (auto-detected if unset)
REM   CONFIG        UE build configuration: Development | Shipping | DebugGame (default: Development)
REM   ARCHIVE_DIR   Output archive directory (default: <repo>\Build\Win64)
REM   SKIP_TESTS    Set to 1 to skip CMake/CTest KomorebiCoreTests
REM   SKIP_ASSETS   Set to 1 to skip Blender validation/FBX export
REM   SKIP_IMPORT   Set to 1 to skip UnrealEditor-Cmd Python import & map build
REM   SKIP_PACKAGE  Set to 1 to compile binaries only without RunUAT BuildCookRun

setlocal EnableExtensions EnableDelayedExpansion

for %%I in ("%~dp0..") do set "PROJECT_ROOT=%%~fI"
set "PROJECT_FILE=%PROJECT_ROOT%\Komorebi.uproject"
if "%CONFIG%"=="" set "CONFIG=Development"
if "%ARCHIVE_DIR%"=="" set "ARCHIVE_DIR=%PROJECT_ROOT%\Build\Win64"
if "%SKIP_TESTS%"=="" set "SKIP_TESTS=0"
if "%SKIP_ASSETS%"=="" set "SKIP_ASSETS=0"
if "%SKIP_IMPORT%"=="" set "SKIP_IMPORT=0"
if "%SKIP_PACKAGE%"=="" set "SKIP_PACKAGE=0"

REM ---------------------------------------------------------------------------
REM Step 1: Build & run pure C++20 KomorebiCoreTests via CMake/CTest
REM ---------------------------------------------------------------------------
if not "%SKIP_TESTS%"=="1" (
    set "CMAKE_SRC="
    if exist "%PROJECT_ROOT%\CMakeLists.txt" (
        set "CMAKE_SRC=%PROJECT_ROOT%"
    ) else if exist "%PROJECT_ROOT%\Tests\CMakeLists.txt" (
        set "CMAKE_SRC=%PROJECT_ROOT%\Tests"
    )

    if defined CMAKE_SRC (
        echo [Komorebi Win64] Step 1/5: Building ^& running KomorebiCoreTests ^(CMake/CTest^)...
        set "CORE_BUILD_DIR=%PROJECT_ROOT%\Build\CoreTests"
        cmake -S "!CMAKE_SRC!" -B "!CORE_BUILD_DIR!"
        if errorlevel 1 goto :fail
        cmake --build "!CORE_BUILD_DIR!" --config Release --parallel
        if errorlevel 1 goto :fail
        ctest --test-dir "!CORE_BUILD_DIR!" -C Release --output-on-failure
        if errorlevel 1 goto :fail
    ) else (
        echo [Komorebi Win64] Step 1/5: No CMakeLists.txt found yet; skipping standalone KomorebiCoreTests.
    )
) else (
    echo [Komorebi Win64] Step 1/5: Skipped KomorebiCoreTests ^(SKIP_TESTS=1^).
)

REM ---------------------------------------------------------------------------
REM Step 2: Validate & export UE5-calibrated FBX + portrait PNGs via Blender 4.2
REM ---------------------------------------------------------------------------
if not "%SKIP_ASSETS%"=="1" (
    set "BLENDER_BIN="
    if defined BLENDER if exist "%BLENDER%" set "BLENDER_BIN=%BLENDER%"
    if not defined BLENDER_BIN if exist "%PROJECT_ROOT%\.test\blender\blender.exe" set "BLENDER_BIN=%PROJECT_ROOT%\.test\blender\blender.exe"
    if not defined BLENDER_BIN if exist "C:\Program Files\Blender Foundation\Blender 4.2\blender.exe" set "BLENDER_BIN=C:\Program Files\Blender Foundation\Blender 4.2\blender.exe"
    if not defined BLENDER_BIN (
        for /f "delims=" %%B in ('where blender 2^>nul') do (
            if not defined BLENDER_BIN set "BLENDER_BIN=%%B"
        )
    )

    if defined BLENDER_BIN (
        echo [Komorebi Win64] Step 2/5: Validating ^& exporting RawAssets via "!BLENDER_BIN!"...
        "!BLENDER_BIN!" --background --factory-startup --disable-autoexec --python-exit-code 1 --python "%PROJECT_ROOT%\Scripts\export_fbx_from_blender.py" -- --export
        if errorlevel 1 goto :fail
    ) else (
        echo [Komorebi Win64] Step 2/5: Blender not found ^(set BLENDER=...^); skipping FBX export.
    )
) else (
    echo [Komorebi Win64] Step 2/5: Skipped Blender asset pipeline ^(SKIP_ASSETS=1^).
)

REM ---------------------------------------------------------------------------
REM Step 3: Resolve UE_ROOT & compile KomorebiEditor, Komorebi, KomorebiServer
REM ---------------------------------------------------------------------------
if not defined UE_ROOT (
    if exist "C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" (
        set "UE_ROOT=C:\Program Files\Epic Games\UE_5.5"
    ) else if exist "C:\Program Files\Epic Games\UE_5.4\Engine\Build\BatchFiles\Build.bat" (
        set "UE_ROOT=C:\Program Files\Epic Games\UE_5.4"
    )
)

if not defined UE_ROOT (
    echo [Komorebi Win64][ERROR] UE_ROOT not found. Set UE_ROOT=C:\Program Files\Epic Games\UE_5.5 and re-run. 1>&2
    exit /b 1
)

set "BUILD_BAT=%UE_ROOT%\Engine\Build\BatchFiles\Build.bat"
set "UAT_BAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"
set "EDITOR_CMD=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

if not exist "%BUILD_BAT%" (
    echo [Komorebi Win64][ERROR] Missing Build.bat at "%BUILD_BAT%" 1>&2
    exit /b 1
)

echo [Komorebi Win64] Step 3/5: Compiling UE5 Win64 targets ^(UE_ROOT=%UE_ROOT%, CONFIG=%CONFIG%^)...
call "%BUILD_BAT%" KomorebiEditor Win64 Development -Project="%PROJECT_FILE%" -WaitMutex -NoHotReloadFromIDE
if errorlevel 1 goto :fail
call "%BUILD_BAT%" Komorebi Win64 %CONFIG% -Project="%PROJECT_FILE%" -WaitMutex -NoHotReloadFromIDE
if errorlevel 1 goto :fail
call "%BUILD_BAT%" KomorebiServer Win64 %CONFIG% -Project="%PROJECT_FILE%" -WaitMutex -NoHotReloadFromIDE
if errorlevel 1 goto :fail

REM ---------------------------------------------------------------------------
REM Step 4: Headless asset import, MPC/Material compilation, Input & Map build
REM ---------------------------------------------------------------------------
if not "%SKIP_IMPORT%"=="1" (
    if not exist "%EDITOR_CMD%" (
        echo [Komorebi Win64][ERROR] Missing UnrealEditor-Cmd.exe at "%EDITOR_CMD%" 1>&2
        exit /b 1
    )
    echo [Komorebi Win64] Step 4/5: Running UnrealEditor-Cmd Python asset import ^& level builder...
    "%EDITOR_CMD%" "%PROJECT_FILE%" -ExecutePythonScript="%PROJECT_ROOT%\Scripts\ue_import_and_build.py" -NullRHI -NoSound -Unattended -NoSplash -NoP4 -stdout -FullStdOutLogOutput
    if errorlevel 1 goto :fail
) else (
    echo [Komorebi Win64] Step 4/5: Skipped UnrealEditor-Cmd Python import ^(SKIP_IMPORT=1^).
)

REM ---------------------------------------------------------------------------
REM Step 5: Package Win64 Client + Dedicated Server via RunUAT BuildCookRun
REM ---------------------------------------------------------------------------
if not "%SKIP_PACKAGE%"=="1" (
    if not exist "%ARCHIVE_DIR%\Client" mkdir "%ARCHIVE_DIR%\Client"
    if not exist "%ARCHIVE_DIR%\Server" mkdir "%ARCHIVE_DIR%\Server"

    echo [Komorebi Win64] Step 5/5 ^(Client^): Packaging Win64 DX12/Vulkan Client -^> "%ARCHIVE_DIR%\Client"...
    call "%UAT_BAT%" BuildCookRun -project="%PROJECT_FILE%" -noP4 -platform=Win64 -targetplatform=Win64 -clientconfig=%CONFIG% -cook -build -stage -pak -archive -archivedirectory="%ARCHIVE_DIR%\Client" -map=L_NeoKyoto+L_Snowbound -utf8output
    if errorlevel 1 goto :fail

    echo [Komorebi Win64] Step 5/5 ^(Server^): Packaging Win64 Dedicated Server -^> "%ARCHIVE_DIR%\Server"...
    call "%UAT_BAT%" BuildCookRun -project="%PROJECT_FILE%" -noP4 -platform=Win64 -serverplatform=Win64 -server -noclient -serverconfig=%CONFIG% -cook -build -stage -pak -archive -archivedirectory="%ARCHIVE_DIR%\Server" -map=L_NeoKyoto+L_Snowbound -utf8output
    if errorlevel 1 goto :fail

    echo [Komorebi Win64] Win64 Client ^& Dedicated Server packaging complete!
) else (
    echo [Komorebi Win64] Step 5/5: Skipped RunUAT packaging ^(SKIP_PACKAGE=1^).
)

exit /b 0

:fail
echo [Komorebi Win64][ERROR] Build failed with exit code %errorlevel%. 1>&2
exit /b %errorlevel%
