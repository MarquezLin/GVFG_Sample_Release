@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

for /f "tokens=2" %%I in ('findstr /B /C:"set(GVFG_SAMPLE_VERSION " "%ROOT%\CMakeLists.txt"') do set "PACKAGE_VERSION=%%I"
set "PACKAGE_VERSION=!PACKAGE_VERSION:"=!"
set "PACKAGE_VERSION=!PACKAGE_VERSION:)=!"
if not defined PACKAGE_VERSION (
    echo ERROR: GVFG_SAMPLE_VERSION was not found in CMakeLists.txt.
    exit /b 1
)

if "%~1"=="" (
    set "APP_EXE=%ROOT%\build\Desktop_Qt_6_10_2_MSVC2022_64bit-Release\bin\gvfg_qt_preview.exe"
) else (
    set "APP_EXE=%~f1"
)

if not exist "%APP_EXE%" (
    echo ERROR: Application executable was not found: "%APP_EXE%"
    exit /b 1
)

if "%~2"=="" (
    for %%I in (windeployqt.exe) do (
        set "WINDEPLOYQT=%%~$PATH:I"
        if defined WINDEPLOYQT set "QT_BIN=%%~dpI"
    )
    if not defined WINDEPLOYQT if exist "C:\Qt" (
        for /d %%Q in ("C:\Qt\*") do (
            if not defined WINDEPLOYQT if exist "%%~fQ\msvc2022_64\bin\windeployqt.exe" (
                set "WINDEPLOYQT=%%~fQ\msvc2022_64\bin\windeployqt.exe"
                set "QT_BIN=%%~fQ\msvc2022_64\bin\"
            )
        )
    )
) else (
    set "QT_BIN=%~f2"
    set "WINDEPLOYQT=%~f2\windeployqt.exe"
)

if not defined WINDEPLOYQT (
    echo ERROR: windeployqt.exe was not found.
    echo Pass the matching Qt bin directory as the second argument.
    exit /b 1
)
if not exist "%WINDEPLOYQT%" (
    echo ERROR: windeployqt.exe was not found: "%WINDEPLOYQT%"
    exit /b 1
)

echo %QT_BIN% | findstr /I "\msvc" >nul
if not errorlevel 1 if not defined VCINSTALLDIR (
    set "MSVC_QT=1"
    set "VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    if not exist "!VSDEVCMD!" (
        echo ERROR: Visual Studio environment was not found for the MSVC Qt package.
        exit /b 1
    )
    call "!VSDEVCMD!" -arch=x64 >nul || exit /b 1
)
if not errorlevel 1 set "MSVC_QT=1"

if "%~3"=="" (
    set "OUTPUT_DIR=%ROOT%\packages"
) else (
    set "OUTPUT_DIR=%~f3"
)

if not exist "%ROOT%\bin\gvfg.dll" (
    echo ERROR: GVFG runtime was not found: "%ROOT%\bin\gvfg.dll"
    exit /b 1
)
if not exist "%ROOT%\bin\gvfg_preview.dll" (
    echo ERROR: GVFG preview runtime was not found: "%ROOT%\bin\gvfg_preview.dll"
    exit /b 1
)
if not exist "%ROOT%\bin\gvfg_audio_playback.dll" (
    echo ERROR: GVFG audio playback runtime was not found: "%ROOT%\bin\gvfg_audio_playback.dll"
    exit /b 1
)

rem Shared SDK may export diagnostics; the customer application must not use them.
where dumpbin >nul 2>nul
if errorlevel 1 (
    echo ERROR: dumpbin was not found. Run from an x64 Visual Studio command prompt.
    exit /b 1
)
dumpbin /nologo /imports "%APP_EXE%" >nul 2>nul
if errorlevel 1 exit /b 1
dumpbin /nologo /imports "%APP_EXE%" | findstr /C:"gvfg_debug_" >nul
if not errorlevel 1 (
    echo ERROR: Internal diagnostic imports found in customer application.
    exit /b 1
)

for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd"') do set "STAMP=%%I"
set "PACKAGE_NAME=GVFG_Customer_Sample_Release_%PACKAGE_VERSION%_%STAMP%"
set "STAGE_ROOT=%TEMP%\%PACKAGE_NAME%_%RANDOM%"
set "STAGE_DIR=%STAGE_ROOT%\%PACKAGE_NAME%"
set "ZIP_PATH=%OUTPUT_DIR%\%PACKAGE_NAME%.zip"

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%" || goto fail
if exist "%STAGE_ROOT%" rmdir /S /Q "%STAGE_ROOT%" || goto fail
mkdir "%STAGE_DIR%" || goto fail

copy /Y "%APP_EXE%" "%STAGE_DIR%\gvfg_qt_preview.exe" >nul || goto fail
copy /Y "%ROOT%\bin\gvfg.dll" "%STAGE_DIR%\gvfg.dll" >nul || goto fail
copy /Y "%ROOT%\bin\gvfg_preview.dll" "%STAGE_DIR%\gvfg_preview.dll" >nul || goto fail
copy /Y "%ROOT%\bin\gvfg_audio_playback.dll" "%STAGE_DIR%\gvfg_audio_playback.dll" >nul || goto fail

"%WINDEPLOYQT%" --release --compiler-runtime --force --dir "%STAGE_DIR%" "%STAGE_DIR%\gvfg_qt_preview.exe"
if errorlevel 1 goto fail

if defined MSVC_QT (
    set "VC_RUNTIME_DIR=!VCToolsRedistDir!x64\Microsoft.VC143.CRT"
    if not exist "!VC_RUNTIME_DIR!\msvcp140.dll" (
        echo ERROR: MSVC runtime DLLs were not found.
        goto fail
    )
    copy /Y "!VC_RUNTIME_DIR!\*.dll" "%STAGE_DIR%\" >nul || goto fail
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Compress-Archive -LiteralPath '%STAGE_DIR%' -DestinationPath '%ZIP_PATH%' -Force"
if errorlevel 1 goto fail

rmdir /S /Q "%STAGE_ROOT%"
echo Package created:
echo "%ZIP_PATH%"
exit /b 0

:fail
echo ERROR: Release packaging failed.
if exist "%STAGE_ROOT%" rmdir /S /Q "%STAGE_ROOT%" >nul 2>nul
exit /b 1
