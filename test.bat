@echo off
rem  Builds and runs the console tests of the non-GUI modules.
setlocal
cd /d "%~dp0"
if not exist build mkdir build
g++ -std=c++17 -Wall -Wextra -DUNICODE -D_UNICODE -DNOMINMAX -Iinclude ^
    tests/test_core.cpp src/Common.cpp src/FileSystemManager.cpp src/NavigationManager.cpp ^
    src/FileOperations.cpp src/SearchManager.cpp ^
    -o build/test_core.exe -lshell32 -lshlwapi -ladvapi32 -lole32
if errorlevel 1 exit /b 1
build\test_core.exe
