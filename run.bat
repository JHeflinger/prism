@echo off
call build.bat
if %ERRORLEVEL% NEQ 0 (
    exit /b %ERRORLEVEL%
)
"build/bin.exe" %1 %2