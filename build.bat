@echo off
setlocal enabledelayedexpansion

:: total timing
set "t_startTime=%time: =0%"

:: audit codebase
python scripts/help.py audit

:: initialize vars for building
set SRC_DIR=src
set INCLUDES=
set SOURCES=
set OBJECTS=
set LIBS=
set LINKS=

:: production build flags
set PROD=
if "%1"=="prod" (
    echo Optimizing for production build...
    set PROD=-O3 -DPROD_BUILD
)

:: create build directory if it does not exist
if NOT exist "build\" (
    mkdir build
)

:: create all build directories if it does not exist
cd build
if NOT exist "shaders\" (
    mkdir shaders
)
if NOT exist "cache\" (
    mkdir cache
)
if NOT exist "vendor\" (
    mkdir vendor
)
cd cache
if NOT exist "shaders\" (
    mkdir shaders
)
if NOT exist "src\" (
    mkdir src
)
cd ..
cd ..

:: set up cache folders
for /d /r %SRC_DIR% %%D in (*) do (
    set SUBPATH=%%D
    set REL=!SUBPATH:%CD%\%SRC_DIR%=!
    set DESTDIR=build\cache\src!REL!
    if not exist "!DESTDIR!" (
        mkdir !DESTDIR!
    )
)

:: compile shaders
echo Building shaders...
set SHADERS_DIR=shaders
set "startTime=%time: =0%"
set SHADERS_UP_TO_DATE="true"
for /r %SHADERS_DIR% %%f in (*.vert *.frag *.comp) do (
    if NOT exist "build/cache/shaders/%%~nxf" (
        set SHADERS_UP_TO_DATE="false"
        echo - [%%~nxf] [33m^(compiling...^)[0m
        "platform/windows/glslc/glslc.exe" %%f -o "build/shaders/%%~nxf.spv"
        if !ERRORLEVEL! NEQ 0 (
            echo Building shader [31mFailed[0m with error code !ERRORLEVEL!
            exit /b !ERRORLEVEL!
        )
        echo [1A[0K- [%%~nxf] [32mOK[0m
        copy /y %%f "build/cache/shaders/%%~nxf" >nul
    ) else (
        fc %%f "build/cache/shaders/%%~nxf" >nul
        if !ERRORLEVEL! NEQ 0 (
            set SHADERS_UP_TO_DATE="false"
            echo - [%%~nxf] [33m^(compiling...^)[0m
            "platform/windows/glslc/glslc.exe" %%f -o "build/shaders/%%~nxf.spv"
            if !ERRORLEVEL! NEQ 0 (
                echo Building shader [31mFailed[0m with error code !ERRORLEVEL!
                exit /b !ERRORLEVEL!
            )
            echo [1A[0K- [%%~nxf] [32mOK[0m
            copy /y %%f "build/cache/shaders/%%~nxf" >nul
        )
    )
)
set "endTime=%time: =0%"
set "end=!endTime:%time:~8,1%=%%100)*100+1!"  &  set "start=!startTime:%time:~8,1%=%%100)*100+1!"
set /A "elap=((((10!end:%time:~2,1%=%%100)*60+1!%%100)-((((10!start:%time:~2,1%=%%100)*60+1!%%100), elap-=(elap>>31)*24*60*60*100"
set /A "cc=elap%%100+100,elap/=100,ss=elap%%60+100,elap/=60,mm=elap%%60+100,hh=elap/60+100"
if %SHADERS_UP_TO_DATE%=="true" (
    echo [1A[0KShaders are currently [32mup to date[0m
) else (
    echo [32mFinished[0m building shaders in %hh:~1%%time:~2,1%%mm:~1%%time:~2,1%%ss:~1%%time:~8,1%%cc:~1%
)

:: get includes
for /r %SRC_DIR% %%d in (.) do (
    set INCLUDES=!INCLUDES! -I"%%d"
)

:: add raylib vendor
set INCLUDES=!INCLUDES! -I"vendor/raylib/include"
set LINKS=!LINKS! -l:win_x64_libraylib.a
set LIBS=!LIBS! -L"vendor/raylib/lib"

:: add stb_image vendor
set INCLUDES=!INCLUDES! -I"vendor/stb_image/include"

:: add EasyObjects vendor
set INCLUDES=!INCLUDES! -I"vendor/EasyObjects/include"
set SOURCES=!SOURCES! "vendor/EasyObjects/include/easymemory.c"

:: add EasyThreads vendor
set INCLUDES=!INCLUDES! -I"vendor/EasyThreads/include"

:: add EasyLogger vendor
set INCLUDES=!INCLUDES! -I"vendor/EasyLogger/include"

:: add vulkan vendor
set INCLUDES=!INCLUDES! -I"platform/windows/vulkan/include"
set LIBS=!LIBS! -L"platform/windows/vulkan/libs"
set LINKS=!LINKS! -lvulkan-1

:: add glfw vendor
set INCLUDES=!INCLUDES! -I"platform/windows/GLFW/include"
set LIBS=!LIBS! -L"platform/windows/GLFW"
set LINKS=!LINKS! -lglfw3
set LINKS=!LINKS! -lshell32
set LINKS=!LINKS! -luser32
set LINKS=!LINKS! -lopengl32
set LINKS=!LINKS! -lgdi32
set LINKS=!LINKS! -lwinmm
set LINKS=!LINKS! -lwinpthread
set LINKS=!LINKS! -lws2_32

:: add cglm vendor
set INCLUDES=!INCLUDES! -I"vendor/cglm/include"

:: compile vendor
set "startTime=%time: =0%"
if defined SOURCES (
    if NOT exist "build\vendor\vendor.o" (
        echo Compiling vendors...
        gcc -Wall -Wextra -Wno-unused-parameter -c%SOURCES%%INCLUDES%%LIBS%%LINKS% -o build/vendor/vendor.o %PROD%
        if !ERRORLEVEL! NEQ 0 (
            echo Building vendors [31mFailed[0m with error code !ERRORLEVEL!
            exit /b !ERRORLEVEL!
        )
        set "endTime=%time: =0%"
        set "end=!endTime:%time:~8,1%=%%100)*100+1!"  &  set "start=!startTime:%time:~8,1%=%%100)*100+1!"
        set /A "elap=((((10!end:%time:~2,1%=%%100)*60+1!%%100)-((((10!start:%time:~2,1%=%%100)*60+1!%%100), elap-=(elap>>31)*24*60*60*100"
        set /A "cc=elap%%100+100,elap/=100,ss=elap%%60+100,elap/=60,mm=elap%%60+100,hh=elap/60+100"
        echo [32mFinished[0m compiling vendor in !hh:~1!!time:~2,1!!mm:~1!!time:~2,1!!ss:~1!!time:~8,1!!cc:~1!
    )
    set OBJECTS=!OBJECTS! build/vendor/vendor.o
)

:: compile obj files
echo Compiling sources...
set SOURCES_UP_TO_DATE="true"
set FOUND_MAIN="false"
set "startTime=%time: =0%"
for /r %SRC_DIR% %%f in (*.c) do (
    set SUBPATH=%%f
    set REL=!SUBPATH:%CD%\%SRC_DIR%=!
    if "%%~nxf" NEQ "main.c" (
        if NOT exist "build\cache\src!REL!.o" (
            set SOURCES_UP_TO_DATE="false"
            echo - [%%~nxf] [33m^(compiling...^)[0m
            gcc -Wall -Wextra -Wno-unused-parameter -c %%f%INCLUDES%%LIBS%%LINKS% -o build\cache\src!REL!.o
            if !ERRORLEVEL! NEQ 0 (
                echo Building source "%%~nxf" [31mFailed[0m with error code !ERRORLEVEL!
                exit /b !ERRORLEVEL!
            )
            echo [1A[0K- [%%~nxf] [32mOK[0m
            copy /y %%f build\cache\src!REL! >nul
        ) else (
            fc %%f "build\cache\src!REL!" >nul
            if !ERRORLEVEL! NEQ 0 (
                set SOURCES_UP_TO_DATE="false"
                echo - [%%~nxf] [33m^(compiling...^)[0m
                gcc -Wall -Wextra -Wno-unused-parameter -c %%f%INCLUDES%%LIBS%%LINKS% -o build\cache\src!REL!.o
                if !ERRORLEVEL! NEQ 0 (
                    echo Building source "%%~nxf" [31mFailed[0m with error code !ERRORLEVEL!
                    exit /b !ERRORLEVEL!
                )
                echo [1A[0K- [%%~nxf] [32mOK[0m
                copy /y %%f build\cache\src!REL! >nul
            )
        )
        set OBJECTS=!OBJECTS! build\cache\src!REL!.o
    ) else (
        set FOUND_MAIN="true"
    )
)
if %SOURCES_UP_TO_DATE%=="true" (
    echo [1A[0KSources are currently [32mup to date[0m
) else (
    set "endTime=%time: =0%"
    set "end=!endTime:%time:~8,1%=%%100)*100+1!"  &  set "start=!startTime:%time:~8,1%=%%100)*100+1!"
    set /A "elap=((((10!end:%time:~2,1%=%%100)*60+1!%%100)-((((10!start:%time:~2,1%=%%100)*60+1!%%100), elap-=(elap>>31)*24*60*60*100"
    set /A "cc=elap%%100+100,elap/=100,ss=elap%%60+100,elap/=60,mm=elap%%60+100,hh=elap/60+100"
    echo [32mFinished[0m compiling sources in !hh:~1!!time:~2,1!!mm:~1!!time:~2,1!!ss:~1!!time:~8,1!!cc:~1!
)
if %FOUND_MAIN%=="false" (
    echo [31mError[0m: unable to compile without a detected "src/main.c" file!
    exit /b !ERRORLEVEL!
)

:: compile executable
echo Building executable...
set "startTime=%time: =0%"
gcc -Wall -Wextra -Wno-unused-parameter src/main.c%OBJECTS%%INCLUDES%%LIBS%%LINKS% -o build/prism.exe %PROD%
if !ERRORLEVEL! NEQ 0 (
    echo Build [31mFailed[0m with error code !ERRORLEVEL!
    exit /b !ERRORLEVEL!
)
set "endTime=%time: =0%"
set "end=!endTime:%time:~8,1%=%%100)*100+1!"  &  set "start=!startTime:%time:~8,1%=%%100)*100+1!"
set /A "elap=((((10!end:%time:~2,1%=%%100)*60+1!%%100)-((((10!start:%time:~2,1%=%%100)*60+1!%%100), elap-=(elap>>31)*24*60*60*100"
set /A "cc=elap%%100+100,elap/=100,ss=elap%%60+100,elap/=60,mm=elap%%60+100,hh=elap/60+100"
echo [32mFinished[0m building executable in %hh:~1%%time:~2,1%%mm:~1%%time:~2,1%%ss:~1%%time:~8,1%%cc:~1%
set "endTime=%time: =0%"
set "end=!endTime:%time:~8,1%=%%100)*100+1!"  &  set "start=!t_startTime:%time:~8,1%=%%100)*100+1!"
set /A "elap=((((10!end:%time:~2,1%=%%100)*60+1!%%100)-((((10!start:%time:~2,1%=%%100)*60+1!%%100), elap-=(elap>>31)*24*60*60*100"
set /A "cc=elap%%100+100,elap/=100,ss=elap%%60+100,elap/=60,mm=elap%%60+100,hh=elap/60+100"
echo [32mFinished[0m total build in %hh:~1%%time:~2,1%%mm:~1%%time:~2,1%%ss:~1%%time:~8,1%%cc:~1%
