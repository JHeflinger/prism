@echo off
call build.bat %1 %2 %3
if %ERRORLEVEL% NEQ 0 (
    exit /b %ERRORLEVEL%
)
"build/tiny_windows.exe -r"
