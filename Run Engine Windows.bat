@echo off
setlocal
cd /d "%~dp0"
if errorlevel 1 exit /b 1

where gcc.exe >nul 2>&1
if not errorlevel 1 goto build
if exist "C:\msys64\ucrt64\bin\gcc.exe" goto msys2

echo GCC was not found.
echo Install GCC using MSYS2. See the Windows steps in README.md.
echo If it is installed elsewhere, add its bin folder to PATH.
pause
exit /b 1

:msys2
set "PATH=C:\msys64\ucrt64\bin;%PATH%"

:build
echo Building the stock matching engine...
gcc -std=c17 -O2 -Wall -Wextra -Wpedantic -Iinclude src\main.c src\cli.c src\matching_engine.c src\order_book.c src\price_tree.c src\input.c src\csv_manager.c -o matching-engine.exe
if errorlevel 1 goto build_failed

echo Starting the program. Choose 0 to save and exit.
matching-engine.exe
set "ENGINE_RESULT=%errorlevel%"
echo.
pause
exit /b %ENGINE_RESULT%

:build_failed
echo Build failed. The program was not started. Read the error above.
pause
exit /b 1
