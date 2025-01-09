@echo off
setlocal enabledelayedexpansion

:: audit codebase
python scripts/help.py audit

:: create build directory if it does not exist
if NOT exist "build\" (
    mkdir build
)

:: initialize vars for building
set SRC_DIR=src
set INCLUDES=
set SOURCES=
set LIBS=
set LINKS=

:: production build flags
set PROD=
if "%1"=="prod" (
    echo Optimizing for production build...
    set PROD=-O3 -DPROD_BUILD
)

:: get src includes and sources
for /r %SRC_DIR% %%d in (.) do (
    set INCLUDES=!INCLUDES! -I"%%d"
)
for /r %SRC_DIR% %%f in (*.c) do (
    set SOURCES=!SOURCES! "%%f"
)

:: add raylib vendor
set INCLUDES=!INCLUDES! -I"vendor/raylib/include"
set LINKS=!LINKS! -l:win_x64_libraylib.a
set LIBS=!LIBS! -L"vendor/raylib/lib"

:: add EasyObjects vendor
set INCLUDES=!INCLUDES! -I"vendor/EasyObjects/include"
set SOURCES=!SOURCES! "vendor/EasyObjects/include/easymemory.c"

:: add glfw vendor
set LINKS=!LINKS! -lshell32
set LINKS=!LINKS! -luser32
set LINKS=!LINKS! -lopengl32
set LINKS=!LINKS! -lgdi32
set LINKS=!LINKS! -lwinmm
set LINKS=!LINKS! -lwinpthread
set LINKS=!LINKS! -lws2_32

:: add cglm vendor
set INCLUDES=!INCLUDES! -I"vendor/cglm/include"

:: compile
echo Building prism...
set "startTime=%time: =0%"
gcc -Wall -Wextra%SOURCES%%INCLUDES%%LIBS%%LINKS% -o build/prism.exe %PROD%
if %ERRORLEVEL% NEQ 0 (
    echo Build [31mFailed[0m with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
set "endTime=%time: =0%"
set "end=!endTime:%time:~8,1%=%%100)*100+1!"  &  set "start=!startTime:%time:~8,1%=%%100)*100+1!"
set /A "elap=((((10!end:%time:~2,1%=%%100)*60+1!%%100)-((((10!start:%time:~2,1%=%%100)*60+1!%%100), elap-=(elap>>31)*24*60*60*100"
set /A "cc=elap%%100+100,elap/=100,ss=elap%%60+100,elap/=60,mm=elap%%60+100,hh=elap/60+100"
echo [32mFinished[0m building in %hh:~1%%time:~2,1%%mm:~1%%time:~2,1%%ss:~1%%time:~8,1%%cc:~1%