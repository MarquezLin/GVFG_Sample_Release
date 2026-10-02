@echo off
setlocal
rem Run only the manually built internal Qt Creator Debug application.
set "APP_EXE=%~dp0build\Desktop_Qt_6_10_2_MSVC2022_64bit-Debug\bin\gvfg_qt_preview.exe"
if not exist "%APP_EXE%" (
    echo ERROR: Build the internal Debug application in Qt Creator first.
    exit /b 1
)
start "GVFG Diagnostic" "%APP_EXE%"
