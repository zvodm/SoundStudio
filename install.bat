@echo off
setlocal EnableExtensions

REM ---------------------------------------------------------------------------
REM  SoundStudio installer (double-clickable wrapper)
REM
REM  All the actual work happens in install.ps1 -- this exists so the thing can
REM  be run by double-clicking without anyone having to know about PowerShell
REM  execution policy. Any arguments are passed straight through, so
REM
REM      install.bat -DesktopShortcut -AddToPath
REM      install.bat -Version v1.2.0
REM      install.bat -Uninstall
REM
REM  all behave exactly like the equivalent install.ps1 call.
REM ---------------------------------------------------------------------------

REM TODO: set this to your repository once it's pushed, e.g. maksim/SoundStudio.
set "REPO=OWNER/REPO"
set "BRANCH=main"

set "PS1=%~dp0install.ps1"

REM Ship install.ps1 next to this file and it is used directly. If it's missing
REM (someone downloaded only the .bat), pull it from the repo into %TEMP%.
if not exist "%PS1%" (
    echo install.ps1 not found next to this file - fetching it from GitHub...
    set "PS1=%TEMP%\soundstudio-install.ps1"
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12; try { Invoke-WebRequest -UseBasicParsing -Uri 'https://raw.githubusercontent.com/%REPO%/%BRANCH%/install.ps1' -OutFile '%TEMP%\soundstudio-install.ps1' } catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }"
    if errorlevel 1 (
        echo.
        echo Could not download install.ps1 from https://github.com/%REPO%
        echo Check the REPO setting at the top of this file, and your connection.
        set "RC=1"
        goto :finish
    )
)

REM PowerShell 7+ if it's there, Windows PowerShell 5.1 otherwise. Both work.
where pwsh.exe >nul 2>&1 && (set "PSEXE=pwsh.exe") || (set "PSEXE=powershell.exe")

REM Only pass -Repo if it's actually been filled in here -- otherwise leave the
REM decision to install.ps1's own $Repo default, so setting it in one place is
REM enough and the two can't disagree.
if /i "%REPO%"=="OWNER/REPO" (
    "%PSEXE%" -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %*
) else (
    "%PSEXE%" -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -Repo "%REPO%" %*
)
set "RC=%ERRORLEVEL%"

:finish
REM Only pause when launched from Explorer (double-clicked) -- pausing inside a
REM terminal or a CI job would just hang. %cmdcmdline% contains this script's
REM own path exactly when cmd was started to run it.
echo "%cmdcmdline%" | find /i "%~nx0" >nul
if not errorlevel 1 (
    echo.
    pause
)

exit /b %RC%
