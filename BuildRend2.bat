@echo off
setlocal

set "BUILD_DIR=%~dp0build\msvc"
set "CACHE=%BUILD_DIR%\CMakeCache.txt"

if not exist "%CACHE%" (
    echo CMake configuration not found: "%CACHE%"
    echo Configure the project in build\msvc first.
    set "RESULT=1"
    goto finish
)

for %%I in (cmake.exe) do set "CMAKE_EXE=%%~$PATH:I"
if not defined CMAKE_EXE (
    for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_COMMAND:INTERNAL=" "%CACHE%"') do set "CMAKE_EXE=%%B"
)

if not exist "%CMAKE_EXE%" (
    echo CMake not found. Add cmake.exe to PATH or install the CMake version used to configure build\msvc.
    set "RESULT=1"
    goto finish
)

rem Character/cinematic render flags are emitted by cgame (inside jagame for SP).
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release --target rd-rend2_x86_64 rdsp-rend2_x86_64 jagamex86_64 cgamex86_64 --parallel 4
set "RESULT=%ERRORLEVEL%"

:finish
if "%RESULT%"=="0" (
    echo.
    echo Rend2 build completed successfully.
) else (
    echo.
    echo Rend2 build failed with exit code %RESULT%.
)
pause
exit /b %RESULT%
