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

:: download simp
if NOT exist "build\simp_windows.exe" (
    echo Downloading simp importer...
    cd build
    PowerShell -Command "Invoke-WebRequest -Uri 'https://github.com/JHeflinger/simp/raw/refs/heads/main/bin/simp_windows.exe' -OutFile 'simp_windows.exe'"
    cd ..
)
if "%1"=="-u" (
    if exist "build\simp_windows.exe" (
        del \f \q build\simp_windows.exe
    )
    echo Updating simp importer...
    cd build
    PowerShell -Command "Invoke-WebRequest -Uri 'https://github.com/JHeflinger/simp/raw/refs/heads/main/bin/simp_windows.exe' -OutFile 'simp_windows.exe'"
    cd ..
)

:: expand shaders
"./build/simp_windows.exe" shaders build/expanded

:: download glslc
if NOT exist "build\glslc.exe" (
    echo Downloading glslc importer...
    cd build
    PowerShell -Command "Invoke-WebRequest -Uri 'https://github.com/JHeflinger/minis/blob/main/vulkan/windows/glslc/glslc.exe' -OutFile 'glslc.exe'"
    cd ..
)
if "%1"=="-u" (
    if exist "build\glslc.exe" (
        del \f \q build\glslc.exe
    )
    echo Updating glslc importer...
    cd build
    PowerShell -Command "Invoke-WebRequest -Uri 'https://github.com/JHeflinger/minis/blob/main/vulkan/windows/glslc/glslc.exe' -OutFile 'glslc.exe'"
    cd ..
)

:: compile shaders
echo Building shaders...
set SHADERS_DIR="build/expanded"
set "startTime=%time: =0%"
set SHADERS_UP_TO_DATE="true"
for /r %SHADERS_DIR% %%f in (*.vert *.frag *.comp) do (
    if NOT exist "build/cache/shaders/%%~nxf" (
        set SHADERS_UP_TO_DATE="false"
        echo - [%%~nxf] [33m^(compiling...^)[0m
        "build/glslc.exe" %%f -o "build/shaders/%%~nxf.spv"
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
            "build/glslc.exe" %%f -o "build/shaders/%%~nxf.spv"
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
