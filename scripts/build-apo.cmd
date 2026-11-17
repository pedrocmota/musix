@echo off
setlocal
rem Compila o APO (C/C++) e copia a DLL para src-tauri\resources, de onde o
rem Tauri a empacota. Tambem gera em apo\build:
rem   apo_host.exe  - carrega o APO fora do audiodg e confere bit a bit
rem   musix_cli.exe - aplica o efeito a um WAV (testes de nulo)
rem   play_tone.exe - toca um tom num endpoint e grava o loopback
rem Opcional: set VCVARS=caminho\para\vcvars64.bat antes de rodar.

if defined VCVARS goto :found
set "PF86=%ProgramFiles(x86)%"
if "%PF86%"=="" set "PF86=C:\Program Files (x86)"
for %%r in ("%PF86%" "%ProgramFiles%") do for %%e in (18 2022 2019) do for %%p in (BuildTools Community Professional Enterprise) do (
    if not defined VCVARS if exist "%%~r\Microsoft Visual Studio\%%e\%%p\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%~r\Microsoft Visual Studio\%%e\%%p\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS (
    echo MSVC nao encontrado. Instale as Ferramentas de Build do Visual Studio ou defina VCVARS.
    exit /b 1
)
:found
call "%VCVARS%" >nul 2>nul
where cl >nul 2>nul
if errorlevel 1 ( echo cl.exe nao disponivel apos vcvars64. & exit /b 1 )

cd /d "%~dp0..\apo"
if not exist build mkdir build

rem /fp:strict: sem contracao (FMA), sem reassociacao -> mesma aritmetica do binario Realtek.
set "CFLAGS=/nologo /O2 /W4 /fp:strict /MT /D_CRT_SECURE_NO_WARNINGS"

echo [1/5] dsp
cl %CFLAGS% /c dsp\musix_dsp.c /Fobuild\musix_dsp.obj
if errorlevel 1 exit /b 1

echo [2/5] APO
cl %CFLAGS% /EHsc /DUNICODE /D_UNICODE /c src\MusixAPO.cpp /Fobuild\MusixAPO.obj
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\MusixAPO.dll /DEF:src\MusixAPO.def build\MusixAPO.obj build\musix_dsp.obj ole32.lib advapi32.lib kernel32.lib
if errorlevel 1 exit /b 1

echo [3/5] apo_host (teste do APO fora do audiodg)
cl %CFLAGS% /EHsc /DUNICODE /D_UNICODE /c tests\apo_host.cpp /Fobuild\apo_host.obj
if errorlevel 1 exit /b 1
link /nologo /OUT:build\apo_host.exe build\apo_host.obj build\musix_dsp.obj ole32.lib kernel32.lib
if errorlevel 1 exit /b 1

echo [4/5] musix_cli (WAV)
cl %CFLAGS% /c cli\musix_cli.c /Fobuild\musix_cli.obj
if errorlevel 1 exit /b 1
link /nologo /OUT:build\musix_cli.exe build\musix_cli.obj build\musix_dsp.obj
if errorlevel 1 exit /b 1

echo [5/5] play_tone (tom + loopback num endpoint)
cl %CFLAGS% /W3 /EHsc /DUNICODE /D_UNICODE /c tests\play_tone.cpp /Fobuild\play_tone.obj
if errorlevel 1 exit /b 1
link /nologo /OUT:build\play_tone.exe build\play_tone.obj ole32.lib
if errorlevel 1 exit /b 1

if not exist "..\src-tauri\resources" mkdir "..\src-tauri\resources"
copy /y build\MusixAPO.dll "..\src-tauri\resources\MusixAPO.dll" >nul
echo OK: src-tauri\resources\MusixAPO.dll
echo     apo\build\apo_host.exe apo\build\MusixAPO.dll   (ciclo COM + bit-exato)
echo     python apo\tests\ref_f32.py apo\build\musix_cli.exe   (referencia float32, precisa de numpy)
