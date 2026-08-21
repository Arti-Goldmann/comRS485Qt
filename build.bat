@echo off
chcp 65001 >nul
setlocal

pushd "%~dp0"

rem Paths can be overridden before launching the script by setting these variables.
if not defined QT_ROOT set "QT_ROOT=C:\Qt\6.11.1\mingw_64"
if not defined CMAKE_EXE set "CMAKE_EXE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
if not defined NINJA_EXE set "NINJA_EXE=C:\Qt\Tools\Ninja\ninja.exe"
if not defined BUILD_DIR set "BUILD_DIR=%~dp0build-script"

if not exist "%CMAKE_EXE%" (
    echo ОШИБКА: CMake не найден: "%CMAKE_EXE%"
    goto :failure
)
if not exist "%NINJA_EXE%" (
    echo ОШИБКА: Ninja не найден: "%NINJA_EXE%"
    goto :failure
)
if not exist "%QT_ROOT%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo ОШИБКА: Qt не найден: "%QT_ROOT%"
    goto :failure
)
if not exist "%QT_ROOT%\lib\cmake\Qt6SerialPort\Qt6SerialPortConfig.cmake" (
    echo ОШИБКА: в комплекте Qt отсутствует компонент Qt Serial Port:
    echo "%QT_ROOT%"
    goto :failure
)

set "CLEAN_REPLY="
set /p "CLEAN_REPLY=Выполнить clean? (написать что угодно, чтобы выполнить): "

echo.
echo Конфигурация CMake...
"%CMAKE_EXE%" -S "." -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%QT_ROOT%" ^
    -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%"
if errorlevel 1 goto :failure

if defined CLEAN_REPLY (
    echo.
    echo Очистка предыдущей сборки...
    "%CMAKE_EXE%" --build "%BUILD_DIR%" --target clean
    if errorlevel 1 goto :failure
) else (
    echo Incremental build: очистка пропущена.
)

echo.
echo Компиляция comRS485Qt...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --target comRS485Qt --parallel
if errorlevel 1 goto :failure

echo.
echo Сборка успешно завершена.
echo EXE: "%BUILD_DIR%\comRS485Qt.exe"
echo.

set "SKIP_RELEASE_REPLY="
set /p "SKIP_RELEASE_REPLY=Сделать Release? (написать что угодно, чтобы не делать): "
if defined SKIP_RELEASE_REPLY goto :skip_release

echo.
echo Развёртывание в папку Release...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --target deploy_release --parallel
if errorlevel 1 goto :failure

echo.
echo Release успешно подготовлен:
echo "%~dp0Release\comRS485Qt.exe"
goto :success

:skip_release
echo.
echo Создание Release пропущено.
goto :success

:failure
echo.
echo СБОРКА ЗАВЕРШИЛАСЬ С ОШИБКОЙ.
popd
pause
exit /b 1

:success
echo.
popd
pause
exit /b 0
