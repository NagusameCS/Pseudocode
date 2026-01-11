@echo off
REM Build Pseudocode VM for Windows x64
REM Run this on Windows with MSYS2/MinGW installed

echo === Building Pseudocode VM for Windows ===

cd /d "%~dp0\.."

REM Check if we're in MSYS2/MinGW
where gcc >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: GCC not found. Please run this from MSYS2 MinGW64 shell
    echo Or install MinGW-w64 and add to PATH
    pause
    exit /b 1
)

cd cvm

echo Building...
gcc -O3 -DNO_PCRE2 -std=gnu11 ^
    -o pseudo-win32-x64.exe ^
    main.c lexer.c compiler.c vm.c memory.c import.c ^
    jit_trace.c trace_recorder.c trace_regalloc.c trace_codegen.c tensor.c ^
    -lm

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

copy pseudo-win32-x64.exe ..\vscode-extension\bin\
echo.
echo === Done! Binary: vscode-extension\bin\pseudo-win32-x64.exe ===
dir ..\vscode-extension\bin\

pause
