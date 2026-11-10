@echo off
rem ============================================================================
rem  build.bat  -  Compiles the NSUT File Explorer
rem  Usage:   build.bat          (just build)
rem           build.bat run      (build, then start the program)
rem ============================================================================
setlocal
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ was not found. Install MSYS2 and add C:\msys64\ucrt64\bin to PATH.
    echo         See README.md, section "Installation".
    exit /b 1
)

if not exist build mkdir build

echo [1/2] Compiling resources ^(icon and manifest^)...
windres assets/app.rc -O coff -o build/app_res.o
if errorlevel 1 goto fail

echo [2/2] Compiling C++ sources...
g++ -std=c++17 -O2 -Wall -Wextra ^
    -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 ^
    -Iinclude ^
    main.cpp ^
    src/Common.cpp ^
    src/FileSystemManager.cpp ^
    src/NavigationManager.cpp ^
    src/FileOperations.cpp ^
    src/SearchManager.cpp ^
    src/MainWindow.cpp ^
    src/MainWindowActions.cpp ^
    build/app_res.o ^
    -o build/FileExplorer.exe ^
    -mwindows -static -static-libgcc -static-libstdc++ ^
    -lcomctl32 -lshell32 -lshlwapi -luxtheme -ladvapi32 -lole32 -lgdi32 -luser32
if errorlevel 1 goto fail

echo.
echo [OK] Build finished:  build\FileExplorer.exe
if /i "%~1"=="run" start "" "build\FileExplorer.exe"
exit /b 0

:fail
echo.
echo [FAILED] The build stopped because of an error. Read the messages above.
exit /b 1
