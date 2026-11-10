@echo off
setlocal EnableExtensions
if "%~1"=="" exit /b 2
set "EVIDENCE=%~f1"
set "VSROOT="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

if not exist "%EVIDENCE%" mkdir "%EVIDENCE%" || exit /b 1
(
    echo MSVC %VCToolsVersion%
    echo Windows SDK %WindowsSDKVersion%
    where cl.exe
    where link.exe
) > "%EVIDENCE%\toolchain.txt"
for %%f in (text count streams calls input) do call :fixture %%f || exit /b 1
exit /b 0

:fixture
set "NAME=%~1"
set "WORK=%EVIDENCE%\%NAME%"
if not exist "%WORK%" mkdir "%WORK%" || exit /b 1
for %%b in (guest repeated) do (
    cl.exe /nologo /TC /c /O2 /GS- /Zl /W4 /WX /Fo"%WORK%\guest.obj" "corpus\m1\pe-console-%NAME%.c" || exit /b 1
    link.exe /nologo /WX /Brepro /incremental:no /nodefaultlib /entry:pf_entry /subsystem:console /out:"%WORK%\%%b.exe" "%WORK%\guest.obj" kernel32.lib || exit /b 1
)
fc.exe /b "%WORK%\guest.exe" "%WORK%\repeated.exe" || exit /b 1
del "%WORK%\repeated.exe" "%WORK%\guest.obj" || exit /b 1
python corpus\m1\check_pe_console_output.py "%NAME%" "%WORK%\native" --run-native "%WORK%\guest.exe"
exit /b %ERRORLEVEL%
