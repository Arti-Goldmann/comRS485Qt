@echo off
echo Copying comRS485Qt.exe to Release folder...

REM Create Release directory if it doesn't exist
if not exist "Release" mkdir Release

REM Copy the executable (prefer Release build, fall back to Debug)
if exist "build\Release\comRS485Qt.exe" (
    copy /Y "build\Release\comRS485Qt.exe" "Release\comRS485Qt.exe"
) else (
    copy /Y "build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug\comRS485Qt.exe" "Release\comRS485Qt.exe"
)

if %errorlevel% neq 0 (
    echo Error copying file!
    pause
    exit /b 1
)

echo File copied successfully!

REM Check if necessary Qt DLLs already exist in Release directory
echo Checking for existing Qt DLLs...
if exist "Release\Qt6Core.dll" if exist "Release\Qt6Gui.dll" if exist "Release\Qt6Widgets.dll" if exist "Release\Qt6SerialPort.dll" (
    echo Qt DLLs already exist, skipping windeployqt...
) else (
    echo Running windeployqt...
    cd Release
    "C:\Qt\6.10.2\mingw_64\bin\windeployqt.exe" comRS485Qt.exe

    if %errorlevel% neq 0 (
        echo Error running windeployqt!
        pause
        exit /b 1
    )
    cd ..
)

echo Deployment completed successfully!
pause
