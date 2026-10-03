@echo off
rem Compila el port (Win32, MSVC + Ninja). Uso: build.bat [Debug|Release]
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
rem CMake 3.31 y Ninja instalados con "pip install --user cmake==3.31.10 ninja"
set PATH=%APPDATA%\Python\Python313\site-packages\cmake\data\bin;%APPDATA%\Python\Python313\Scripts;%PATH%

cd /d "%~dp0"
if not exist build\%CONFIG%\build.ninja (
    cmake -S . -B build\%CONFIG% -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
)
cmake --build build\%CONFIG% || exit /b 1
echo.
echo OK: build\%CONFIG%\nascar.exe
