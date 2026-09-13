@echo off
setlocal

SET BLD_DIR=build

if not exist %BLD_DIR%\SoundStudio.exe (
    ECHO No exe found, build first.
    exit /b 1
)

%BLD_DIR%\SoundStudio.exe