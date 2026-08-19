@echo off
setlocal

rem === Configuration ===
set "VCVARS=c:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "QTDIR=C:\Qt\6.10.3\msvc2022_64"
set "SRCROOT=%~dp0"
set "OUTDIR=%SRCROOT%out"
set "EXE=%OUTDIR%\src\app\seamly2d\bin\seamly2d.exe"

echo === Setting up MSVC build environment ===
if not exist "%VCVARS%" (
    echo ERROR: vcvars64.bat not found at "%VCVARS%"
    echo Edit build_and_run.bat and fix the VCVARS path for your Visual Studio install.
    exit /b 1
)
call "%VCVARS%"
if errorlevel 1 (
    echo ERROR: Failed to initialize MSVC environment.
    exit /b 1
)

if not exist "%QTDIR%\bin\qmake.exe" (
    echo ERROR: Qt not found at "%QTDIR%"
    echo Edit build_and_run.bat and fix the QTDIR path, or install Qt 6.10.3 msvc2022_64.
    exit /b 1
)
set "PATH=%QTDIR%\bin;%PATH%"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
cd /d "%OUTDIR%"

echo === Running qmake ===
qmake "%SRCROOT%Seamly2D.pro"
if errorlevel 1 (
    echo ERROR: qmake failed.
    exit /b 1
)

echo === Building with nmake ===
nmake
if errorlevel 1 (
    echo ERROR: Build failed. See output above.
    exit /b 1
)

if not exist "%EXE%" (
    echo ERROR: Build succeeded but Seamly2D.exe was not found at "%EXE%"
    exit /b 1
)

echo === Launching Seamly2D ===
start "" "%EXE%"

endlocal
