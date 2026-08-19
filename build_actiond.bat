@echo off
setlocal

rem === Configuration ===
rem Mirrors build_and_run.bat's setup so both scripts stay consistent; edit these two paths
rem if your Visual Studio or Qt install differs.
set "VCVARS=c:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "QTDIR=C:\Qt\6.10.3\msvc2022_64"
set "SRCROOT=%~dp0"
set "OUTDIR=%SRCROOT%out"
set "EXE=%OUTDIR%\src\app\actiond\bin\actiond.exe"

echo === Setting up MSVC build environment ===
if not exist "%VCVARS%" (
    echo ERROR: vcvars64.bat not found at "%VCVARS%"
    echo Edit build_actiond.bat and fix the VCVARS path for your Visual Studio install.
    exit /b 1
)
call "%VCVARS%"
if errorlevel 1 (
    echo ERROR: Failed to initialize MSVC environment.
    exit /b 1
)

if not exist "%QTDIR%\bin\qmake.exe" (
    echo ERROR: Qt not found at "%QTDIR%"
    echo Edit build_actiond.bat and fix the QTDIR path, or install Qt 6.10.3 msvc2022_64.
    exit /b 1
)
set "PATH=%QTDIR%\bin;%PATH%"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

rem === qmake: regenerate every Makefile from the repo root ===
rem A single top-level qmake run keeps the whole out/ tree (libs, seamly2d, seamlyme, actiond,
rem tests) in sync with any .pro/.pri changes, which matters after editing actiond.pro itself or
rem any lib it depends on. Actual compilation below is still scoped to just what actiond needs.
cd /d "%OUTDIR%"
echo === Running qmake (repo root) ===
qmake "%SRCROOT%Seamly2D.pro"
if errorlevel 1 (
    echo ERROR: qmake failed.
    exit /b 1
)

rem === Build every static library actiond links against ===
rem actiond does not build standalone: it links vpatterndb, ifc, vmisc, vgeometry, vlayout,
rem qmuparser, vpropertyexplorer, tools, vtools, vwidgets, and actionlayer. Building the whole
rem libs/ subtree first guarantees they all exist and are current, the same dependency order
rem app.depends=libs enforces in a full-project build.
echo === Building libs ===
cd /d "%OUTDIR%\src\libs"
nmake
if errorlevel 1 (
    echo ERROR: Building libs failed. See output above.
    exit /b 1
)

rem === Build actiond itself ===
echo === Building actiond ===
cd /d "%OUTDIR%\src\app\actiond"
nmake
if errorlevel 1 (
    echo ERROR: Building actiond failed. See output above.
    exit /b 1
)

if not exist "%EXE%" (
    echo ERROR: Build succeeded but actiond.exe was not found at "%EXE%"
    exit /b 1
)

echo.
echo === Build succeeded ===
echo actiond.exe: %EXE%
echo.
echo Usage:
echo   "%EXE%" --pattern ^<path.val^> --measurements ^<path.smis^|.smms^|.vst^> --actions ^<path.json^>
echo.
echo actiond forces QT_QPA_PLATFORM=offscreen itself and ships its own platforms\qoffscreen.dll
echo next to the exe, so it runs headlessly with no extra setup.

endlocal
