@echo off
REM Build desktop (SDL2) Windows binaries into build\win\<game>.exe
REM Requires MSYS2 MinGW64: pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 make
REM Prefer running from "MSYS2 MinGW 64-bit" shell:  ./build_windows.sh
REM This .bat finds bash and forwards the same arguments.

setlocal
cd /d "%~dp0"

where bash >nul 2>&1
if errorlevel 1 (
  if exist "C:\msys64\usr\bin\bash.exe" (
    set "BASH=C:\msys64\usr\bin\bash.exe"
  ) else if exist "C:\msys64\mingw64\bin\bash.exe" (
    set "BASH=C:\msys64\mingw64\bin\bash.exe"
  ) else (
    echo bash not found. Install MSYS2 and open "MSYS2 MinGW 64-bit", then run:
    echo   ./build_windows.sh
    exit /b 1
  )
) else (
  set "BASH=bash"
)

REM Ensure MinGW64 tools are on PATH when launched from cmd.exe
if exist "C:\msys64\mingw64\bin\gcc.exe" set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"

"%BASH%" "%~dp0build_windows.sh" %*
exit /b %ERRORLEVEL%
