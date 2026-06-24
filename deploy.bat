@echo off
echo Copying comRS485Qt.exe to Release folder...

REM %1 = full path to the freshly built comRS485Qt.exe (passed from CMake)
REM %2 = Qt bin directory containing windeployqt.exe (passed from CMake)
set "EXE_PATH=%~1"
set "QT_BIN_DIR=%~2"

REM Fallback to the 6.10.2 Debug build path if no argument was provided
if "%EXE_PATH%"=="" set "EXE_PATH=build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug\comRS485Qt.exe"
if "%QT_BIN_DIR%"=="" set "QT_BIN_DIR=C:\Qt\6.10.2\mingw_64\bin"

REM CMake passes forward-slash paths; cmd copy/windeployqt need backslashes
set "EXE_PATH=%EXE_PATH:/=\%"
set "QT_BIN_DIR=%QT_BIN_DIR:/=\%"

if not exist "%EXE_PATH%" (
    echo Error: built exe not found at "%EXE_PATH%"
    exit /b 1
)

REM Create Release directory if it doesn't exist
if not exist "Release" mkdir Release

REM Copy the executable
copy /Y "%EXE_PATH%" "Release\comRS485Qt.exe"

if %errorlevel% neq 0 (
    echo Error copying file!
    exit /b 1
)

echo File copied successfully!

REM Always run windeployqt so DLLs stay in sync with the active Qt kit.
REM windeployqt is incremental (it only updates changed files), so this is
REM cheap and stays correct when adding modules (e.g. Network) or switching
REM Qt versions.
echo Running windeployqt...
cd Release
"%QT_BIN_DIR%\windeployqt.exe" --no-translations comRS485Qt.exe
if errorlevel 1 (
    echo Error running windeployqt!
    cd ..
    exit /b 1
)
cd ..

echo Deployment completed successfully!
