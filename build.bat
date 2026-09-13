@echo off
setlocal

SET PATH=%appdata%\..\Local\w64devkit\bin\

SET SRC_DIR=src
SET BLD_DIR=build
SET TMP_DIR=temp
SET AST_DIR=assets
SET LIB_DIR=lib
SET LUA_DIR=Lua
SET THR_DIR=third_party

if exist %BLD_DIR%\ (
    ECHO BUILD DIR EXISTS
    ECHO ================
) else (
    ECHO BUILD DIR DOESN'T EXISTS
    ECHO CREATES ONE
    ECHO ================
    mkdir %BLD_DIR%
)

if exist %TMP_DIR%\ (
    ECHO TEMP DIR EXISTS
    ECHO ================
) else (
    ECHO TEMP DIR DOESN'T EXISTS
    ECHO CREATES ONE
    ECHO ================
    mkdir %TMP_DIR%\%LUA_DIR%
    for %%f in ("%THR_DIR%\%LUA_DIR%\*.c") do (
        echo Compiling %%~nxf...
        gcc -c "%%f" -o "%TMP_DIR%\%LUA_DIR%\%%~nf.o" -I%THR_DIR%\%LUA_DIR%
    )
)

SET CPP_FILES= ^
%SRC_DIR%\main.cpp ^
%SRC_DIR%\song\SongIO.cpp ^
%SRC_DIR%\song\InstrumentIO.cpp ^
%SRC_DIR%\song\DefaultInstruments.cpp ^
%SRC_DIR%\song\DeployKit.cpp ^
%SRC_DIR%\audio\Synth.cpp ^
%SRC_DIR%\audio\DefaultWaveforms.cpp ^
%SRC_DIR%\audio\Sequencer.cpp ^
%SRC_DIR%\audio\WavExport.cpp ^
%SRC_DIR%\ui\PianoRollWidget.cpp ^
%SRC_DIR%\ui\Theme.cpp ^
%SRC_DIR%\ui\DefaultLayout.cpp ^
%SRC_DIR%\app\AppPaths.cpp ^
%SRC_DIR%\plugin\PluginManager.cpp ^
%SRC_DIR%\plugin\PluginHost.cpp ^
%SRC_DIR%\plugin\DefaultPlugins.cpp ^
%SRC_DIR%\imgui\imgui.cpp ^
%SRC_DIR%\imgui\imgui_draw.cpp ^
%SRC_DIR%\imgui\imgui_tables.cpp ^
%SRC_DIR%\imgui\imgui_widgets.cpp ^
%SRC_DIR%\imgui\imgui_demo.cpp ^
%SRC_DIR%\imgui\rlImGui.cpp

g++ -Iinclude -Iinclude\raylib -Isrc -Iinclude\imgui -Iinclude\rlimgui -Iinclude\app -Iinclude\plugin %CPP_FILES% %TMP_DIR%\%LUA_DIR%\*.o -o %BLD_DIR%\SoundStudio.exe -L%LIB_DIR% -I%THR_DIR%\%LUA_DIR% -I%THR_DIR% -lraylib -lopengl32 -lgdi32 -lwinmm -lole32 -std=c++20 -march=x86-64 -mwindows

if errorlevel 1 (
    ECHO ================
    ECHO BUILD FAILED
    ECHO ================
    exit /b 1
)

call run.bat
