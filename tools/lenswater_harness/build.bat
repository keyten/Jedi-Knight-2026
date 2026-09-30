@echo off
rem Builds and runs the lens water harness with MSVC.
rem Run from a "x64 Native Tools" prompt, or let it find vcvars64.bat below.
setlocal
set "HERE=%~dp0"
set "SRC=%HERE%..\..\shared\rd-rend2"
where cl >nul 2>nul
if not errorlevel 1 goto :build

rem "(x86)" would end a parenthesised block: expand it outside of one
set "PF86=%ProgramFiles(x86)%"
set "PF=%ProgramFiles%"
set "VCVARS="
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
	for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
)
for %%y in (2022 2019 2017) do for %%e in (Community Professional Enterprise BuildTools) do (
	if not defined VCVARS if exist "%PF86%\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%PF86%\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat"
	if not defined VCVARS if exist "%PF%\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%PF%\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS (
	echo Visual Studio C++ tools not found
	exit /b 1
)
call "%VCVARS%" >nul

:build
pushd "%HERE%"
cl /nologo /O2 /W4 /EHsc /std:c++17 /I"%SRC%" harness.cpp "%SRC%\tr_lenswater.cpp" /Fe:harness.exe /Fo:"%TEMP%\\" || goto :fail
"%HERE%harness.exe"
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
:fail
popd
exit /b 1
