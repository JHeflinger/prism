@echo off
setlocal enabledelayedexpansion

:: create all build directories if it does not exist
if NOT exist "build\" (
    mkdir build
)
cd build
if NOT exist "shaders\" (
    mkdir shaders
)
if NOT exist "cache\" (
    mkdir cache
)
if NOT exist "expanded\" (
    mkdir expanded
)
cd cache
if NOT exist "shaders\" (
    mkdir shaders
)
cd ..
cd ..

:: download builder
if NOT exist "build\tiny_windows.exe" (
    echo Downloading tiny builder...
    cd build
    PowerShell -Command "Invoke-WebRequest -Uri 'https://github.com/JHeflinger/tiny/raw/refs/heads/main/bin/tiny_windows.exe' -OutFile 'tiny_windows.exe'"
    cd ..
)
if "%1"=="-u" (
    if exist "build\tiny_windows.exe" (
        del \f \q build\tiny_windows.exe
    )
    echo Updating tiny builder...
    cd build
    PowerShell -Command "Invoke-WebRequest -Uri 'https://github.com/JHeflinger/tiny/raw/refs/heads/main/bin/tiny_windows.exe' -OutFile 'tiny_windows.exe'"
    cd ..
)

:: set prod flags
set PROD=
if "%1"=="-p" (
    set PROD=-prod
)
if "%2"=="-p" (
    set PROD=-prod
)
if "%3"=="-p" (
    set PROD=-prod
)

:: run builder
"./build/tiny_windows.exe" -a -f %PROD%
if !ERRORLEVEL! NEQ 0 (
    exit /b !ERRORLEVEL!
)
