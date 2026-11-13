@echo off
setlocal
if not exist build mkdir build
python tools\write-qol-version.py
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /c vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde32.c /Fo:build\
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /LD /MT src\main.cpp version.lib user32.lib /Fe:build\GTAIV-QoL.asi /link /MACHINE:X86 /DLL
if errorlevel 1 exit /b 1
echo Built build\GTAIV-QoL.asi

cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tools\compile-pause-blur.cpp d3dcompiler.lib /Fe:build\compile-pause-blur.exe
if errorlevel 1 exit /b 1
build\compile-pause-blur.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tools\compile-road-line-depth.cpp d3dcompiler.lib /Fe:build\compile-road-line-depth.exe
if errorlevel 1 exit /b 1
build\compile-road-line-depth.exe
if errorlevel 1 exit /b 1

python tools\embed-pause-label.py
if errorlevel 1 exit /b 1

if not defined GITHUB_SHA set GITHUB_SHA=local
>src\pause_build.hpp echo #define QOL_BUILD_ID "%GITHUB_SHA%"
>vendor\roadnav\src\build_info.hpp echo #pragma once
>>vendor\roadnav\src\build_info.hpp echo #define ROADNAV_BUILD_ID "%GITHUB_SHA%-bundled"
cl /nologo /std:c++17 /EHsc /W3 /O2 /MT /c vendor\roadnav\src\roadnav.cpp /Fo:build\roadnav.obj
if errorlevel 1 exit /b 1
lib /nologo /machine:x86 /def:scripthook.def /out:build\ScriptHook-Pause.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /LD /MT src\cutscene_pause.cpp build\roadnav.obj dxguid.lib gdi32.lib build\buffer.obj build\hook.obj build\trampoline.obj build\hde32.obj version.lib user32.lib build\ScriptHook-Pause.lib /Fe:build\GTAIV-QoL-Pause.asi /link /MACHINE:X86 /DLL
if errorlevel 1 exit /b 1

