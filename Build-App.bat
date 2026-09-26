@echo off
setlocal EnableDelayedExpansion
title Building IMEI & Radio Backup Tool...
color 0b

echo ========================================================
echo   IMEI & Radio Backup Tool - Automated Build Engine
echo   Developer: @Shakib_BD [TG]
echo ========================================================
echo.

:: 1. Validate required source and resource files
if not exist "main.cpp" (
color 0c
echo [ERROR] 'main.cpp' was not found in the current directory!
echo Please make sure this script is placed in the project folder.
goto :FAIL
)

if not exist "resource.rc" (
color 0c
echo [ERROR] 'resource.rc' was not found in the current directory!
echo Ensure 'resource.rc' exists to package the icon and payload.
goto :FAIL
)

:: 2. Check if g++ and windres are available in PATH
where windres >nul 2>&1
if %ERRORLEVEL% neq 0 (
color 0c
echo [ERROR] 'windres' compiler was not found in PATH!
echo Make sure your MinGW/UCRT64 bin directory is in your system PATH.
goto :FAIL
)

where g++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
color 0c
echo [ERROR] 'g++' compiler was not found in PATH!
echo Make sure your MinGW/UCRT64 bin directory is in your system PATH.
goto :FAIL
)

:: 3. Compile resource.rc into resource.res every time
echo [1/2] Compiling Windows Resources (resource.rc -^> resource.res)...
windres -i "resource.rc" -O coff -o "resource.res"
if %ERRORLEVEL% neq 0 (
color 0c
echo [ERROR] Failed to compile 'resource.rc'!
echo Check if icon.ico or payload.bin referenced in resource.rc exist.
goto :FAIL
)
echo       -- Resource compiled successfully!
echo.

:: 4. Compile C++ binary with static runtime linking
echo [2/2] Compiling standalone executable (ImeiBackupTool.exe)...
echo       Applying static linkage: [-static -static-libgcc -static-libstdc++]
g++ -std=c++17 main.cpp resource.res -o ImeiBackupTool.exe -mwindows -static -static-libgcc -static-libstdc++ -lcomctl32 -lshlwapi -lshell32
if %ERRORLEVEL% neq 0 (
color 0c
echo [ERROR] Compilation failed! Review the compiler errors above.
goto :FAIL
)

color 0a
echo.
echo ========================================================
echo   [SUCCESS] Build finished successfully!
echo   Output Binary : ImeiBackupTool.exe
echo   Portability   : Standalone (No missing DLL dependencies)
echo ========================================================
echo.
echo Launching application now (or press any key to exit)...
timeout /t 2 >nul
start "" "ImeiBackupTool.exe"
goto :END

:FAIL
echo.
echo ========================================================
echo   [FAILED] Build terminated due to errors.
echo ========================================================
pause
exit /b 1

:END
exit /b 0
