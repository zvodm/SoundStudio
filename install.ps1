<#
.SYNOPSIS
    Installs SoundStudio from its GitHub Releases page.

.DESCRIPTION
    Downloads the newest (or a specific) published release asset, unpacks it
    into a per-user folder, and wires up Start Menu / Desktop shortcuts and an
    entry in "Apps & features". Everything happens under the current user's
    profile, so this never asks for administrator rights.

    Why per-user and not Program Files: SoundStudio writes recent_files.txt and
    imgui.ini next to its own executable, and Program Files is read-only for a
    normal user. Installing under %LOCALAPPDATA% keeps those writable, which is
    also why every shortcut sets its working directory to the install folder.

    Songs, instrument presets, custom waves and plugins live in
    %USERPROFILE%\SoundStudio and are NOT touched by install, upgrade or
    uninstall unless you pass -RemoveUserData.

.PARAMETER Repo
    GitHub repository as "owner/name". Set the $Repo default below once you've
    pushed, and callers won't have to pass it.

.PARAMETER Version
    Release tag to install (e.g. "v1.2.0"), or "latest" for the newest
    published, non-draft release.

.PARAMETER InstallDir
    Where to install. Default: %LOCALAPPDATA%\SoundStudio

.PARAMETER AssetPattern
    Wildcard used to pick the release asset when a release has several
    (e.g. "*win64*.zip"). Left empty, the script prefers a .zip that looks like
    a Windows x64 build, then any .zip, then any .exe.

.PARAMETER DesktopShortcut
    Also drop a shortcut on the Desktop.

.PARAMETER NoShortcut
    Skip shortcut creation entirely.

.PARAMETER AddToPath
    Append the install folder to the user's PATH.

.PARAMETER DefaultContent
    What to do about SoundStudio's ~108 bundled instrument presets and its
    Lua plugins:
      Ask   show a pick-list and install what you tick (default; falls back
            to All when there's no interactive console)
      All   install everything without asking
      None  install nothing, and record that so first launch doesn't either
      Skip  don't touch it at all -- SoundStudio installs its full library
            the first time you run it

.PARAMETER ContentSource
    Where the presets, waves and plugins come from:
      Auto      the repository's Instruments/Waves/Plugins branches, falling
                back to the library built into SoundStudio.exe when GitHub
                can't be reached (default)
      GitHub    the content branches only
      Embedded  the executable's own library only -- no network needed

.PARAMETER Force
    Reinstall even if the requested version is already installed, and close a
    running SoundStudio instead of refusing to overwrite it.

.PARAMETER Uninstall
    Remove SoundStudio (shortcuts, PATH entry, uninstall entry, install folder).

.PARAMETER RemoveUserData
    With -Uninstall, also delete %USERPROFILE%\SoundStudio (your songs and
    presets). Off by default, deliberately.

.EXAMPLE
    .\install.ps1
    Installs the latest release with a Start Menu shortcut.

.EXAMPLE
    .\install.ps1 -Version v1.2.0 -DesktopShortcut -AddToPath

.EXAMPLE
    .\install.ps1 -DefaultContent None
    Installs the program but no presets or plugins.

.EXAMPLE
    .\install.ps1 -Uninstall

.NOTES
    Set $env:GITHUB_TOKEN before running to lift the 60-requests/hour anonymous
    API rate limit, or to install from a private repository.
#>

[CmdletBinding()]
param(
    [string] $Repo         = 'zvodm/SoundStudio',
    [string] $Version      = 'latest',
    [string] $InstallDir   = (Join-Path $env:LOCALAPPDATA 'SoundStudio'),
    [string] $AssetPattern = '',
    [switch] $DesktopShortcut,
    [switch] $NoShortcut,
    [switch] $AddToPath,
    [ValidateSet('Ask', 'All', 'None', 'Skip')]
    [string] $DefaultContent = 'Ask',
    [ValidateSet('Auto', 'GitHub', 'Embedded')]
    [string] $ContentSource = 'Auto',
    [switch] $Force,
    [switch] $Uninstall,
    [switch] $RemoveUserData
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Windows PowerShell 5.1 still defaults to TLS 1.0, which api.github.com
# refuses outright. PowerShell 7 already negotiates TLS 1.2+ on its own.
try {
    [Net.ServicePointManager]::SecurityProtocol =
        [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch { }

$AppName       = 'SoundStudio'
$ExeName       = 'SoundStudio.exe'
$UninstallKey  = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SoundStudio'
$StartMenuDir  = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$StartMenuLnk  = Join-Path $StartMenuDir "$AppName.lnk"
# GetFolderPath follows a redirected Desktop (OneDrive, roaming profiles).
# It can come back empty on a locked-down profile, hence the fallback -- an
# empty path here would otherwise blow up before the installer does anything.
$DesktopDir    = [Environment]::GetFolderPath('Desktop')
if (-not $DesktopDir) { $DesktopDir = Join-Path $env:USERPROFILE 'Desktop' }
$DesktopLnk    = Join-Path $DesktopDir "$AppName.lnk"
$ManifestPath  = Join-Path $InstallDir 'install.json'
$UserDataDir   = Join-Path $env:USERPROFILE 'SoundStudio'

# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

function Write-Step { param([string] $Message) Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Ok   { param([string] $Message) Write-Host "    $Message" -ForegroundColor Green }
function Write-Info { param([string] $Message) Write-Host "    $Message" -ForegroundColor DarkGray }
function Write-Note { param([string] $Message) Write-Host "    $Message" -ForegroundColor Yellow }

function Stop-WithError {
    param([string] $Message)
    Write-Host ''
    Write-Host "ERROR: $Message" -ForegroundColor Red
    Write-Host ''
    exit 1
}

# ---------------------------------------------------------------------------
# Shortcuts, PATH and the Apps & features entry
# ---------------------------------------------------------------------------

function New-AppShortcut {
    param([string] $LinkPath, [string] $TargetExe, [string] $WorkDir)

    $shell    = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($LinkPath)
    $shortcut.TargetPath = $TargetExe
    # SoundStudio writes imgui.ini and recent_files.txt relative to wherever it
    # was launched from, so the working directory has to be the install folder
    # or those land in random places (and the docked panel layout won't stick).
    $shortcut.WorkingDirectory = $WorkDir
    $shortcut.IconLocation     = "$TargetExe,0"
    $shortcut.Description      = 'SoundStudio - chiptune tracker and piano-roll editor'
    $shortcut.Save()
}

function Add-ToUserPath {
    param([string] $Directory)

    $current = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (-not $current) { $current = '' }

    $entries = $current -split ';' | Where-Object { $_ -ne '' }
    if ($entries -contains $Directory) { return $false }

    $updated = (@($entries) + $Directory) -join ';'
    [Environment]::SetEnvironmentVariable('Path', $updated, 'User')
    return $true
}

function Remove-FromUserPath {
    param([string] $Directory)

    $current = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (-not $current) { return $false }

    $entries = $current -split ';' | Where-Object { $_ -ne '' -and $_ -ne $Directory }
    [Environment]::SetEnvironmentVariable('Path', ($entries -join ';'), 'User')
    return $true
}

function Register-UninstallEntry {
    param([string] $Tag, [string] $Directory)

    New-Item -Path $UninstallKey -Force | Out-Null
    $uninstallCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $Directory 'uninstall.ps1')`""

    Set-ItemProperty -Path $UninstallKey -Name 'DisplayName'     -Value $AppName
    Set-ItemProperty -Path $UninstallKey -Name 'DisplayVersion'  -Value ($Tag -replace '^v', '')
    Set-ItemProperty -Path $UninstallKey -Name 'Publisher'       -Value $AppName
    Set-ItemProperty -Path $UninstallKey -Name 'InstallLocation' -Value $Directory
    Set-ItemProperty -Path $UninstallKey -Name 'DisplayIcon'     -Value (Join-Path $Directory $ExeName)
    Set-ItemProperty -Path $UninstallKey -Name 'UninstallString' -Value $uninstallCmd
    Set-ItemProperty -Path $UninstallKey -Name 'NoModify'        -Value 1 -Type DWord
    Set-ItemProperty -Path $UninstallKey -Name 'NoRepair'        -Value 1 -Type DWord
}

# Written into the install folder so uninstalling works later even if this
# script is long gone -- including when it was piped straight from the web and
# never existed on disk in the first place.
function Write-UninstallScript {
    param([string] $Directory)

    # Note the relaunch dance at the top: a script cannot delete the folder it
    # is running from, so the copy that does the deleting runs out of %TEMP%.
    $script = @"
param([switch] `$RemoveUserData)

# Auto-generated by the SoundStudio installer. Removes exactly what it added.
`$ErrorActionPreference = 'SilentlyContinue'

`$installDir = '$Directory'
`$userData   = '$UserDataDir'

# Running from inside the folder we're about to delete? Re-launch a copy of
# this script from %TEMP% and let that one finish the job.
if (`$PSCommandPath -and `$PSCommandPath.StartsWith(`$installDir, [StringComparison]::OrdinalIgnoreCase)) {
    `$relay = Join-Path ([IO.Path]::GetTempPath()) ('soundstudio-uninstall-' + [Guid]::NewGuid().ToString('N') + '.ps1')
    Copy-Item -LiteralPath `$PSCommandPath -Destination `$relay -Force
    `$relayArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', `$relay)
    if (`$RemoveUserData) { `$relayArgs += '-RemoveUserData' }

    # Re-use whichever PowerShell is running this, rather than assuming
    # powershell.exe -- works the same under Windows PowerShell 5.1 and pwsh 7.
    `$psExe = 'powershell.exe'
    try { `$psExe = [Diagnostics.Process]::GetCurrentProcess().MainModule.FileName } catch { }

    Start-Process -FilePath `$psExe -ArgumentList `$relayArgs -Wait
    return
}

Get-Process -Name 'SoundStudio' | Stop-Process -Force
Start-Sleep -Milliseconds 400

Remove-Item -LiteralPath '$StartMenuLnk' -Force
Remove-Item -LiteralPath '$DesktopLnk'   -Force
Remove-Item -LiteralPath '$UninstallKey' -Recurse -Force

`$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (`$userPath) {
    `$kept = `$userPath -split ';' | Where-Object { `$_ -ne '' -and `$_ -ne `$installDir }
    [Environment]::SetEnvironmentVariable('Path', (`$kept -join ';'), 'User')
}

Remove-Item -LiteralPath `$installDir -Recurse -Force
if (`$RemoveUserData) {
    Remove-Item -LiteralPath `$userData -Recurse -Force
} else {
    # The manifest is installer bookkeeping, not the user's work. Leaving it
    # behind would mean a later reinstall silently skips the pick-list and
    # installs nothing.
    Remove-Item -LiteralPath (Join-Path `$userData 'defaults.manifest') -Force
}

Write-Host 'SoundStudio has been uninstalled.' -ForegroundColor Green
if (-not `$RemoveUserData) {
    Write-Host "Your songs and presets are still in `$userData" -ForegroundColor DarkGray
}

# If this is the %TEMP% relay copy, tidy it away. PowerShell reads a script
# fully before running it, so a script deleting its own file is fine.
if (`$PSCommandPath -and (Split-Path `$PSCommandPath -Leaf) -like 'soundstudio-uninstall-*') {
    Remove-Item -LiteralPath `$PSCommandPath -Force
}
"@

    Set-Content -LiteralPath (Join-Path $Directory 'uninstall.ps1') -Value $script -Encoding UTF8
}

function Invoke-Uninstall {
    Write-Step "Uninstalling $AppName"

    $running = @(Get-Process -Name 'SoundStudio' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        Write-Info 'Closing running instance...'
        $running | Stop-Process -Force
        Start-Sleep -Milliseconds 400
    }

    foreach ($lnk in @($StartMenuLnk, $DesktopLnk)) {
        if (Test-Path -LiteralPath $lnk) {
            Remove-Item -LiteralPath $lnk -Force
            Write-Ok "Removed shortcut $lnk"
        }
    }

    if (Test-Path -LiteralPath $UninstallKey) {
        Remove-Item -LiteralPath $UninstallKey -Recurse -Force
        Write-Ok 'Removed the Apps & features entry'
    }

    if (Remove-FromUserPath -Directory $InstallDir) { Write-Ok 'Cleaned up PATH' }

    if (Test-Path -LiteralPath $InstallDir) {
        Remove-Item -LiteralPath $InstallDir -Recurse -Force
        Write-Ok "Removed $InstallDir"
    }

    if ($RemoveUserData) {
        if (Test-Path -LiteralPath $UserDataDir) {
            Remove-Item -LiteralPath $UserDataDir -Recurse -Force
            Write-Ok "Removed $UserDataDir (songs, presets, waves, plugins)"
        }
    } elseif (Test-Path -LiteralPath $UserDataDir) {
        Write-Info "Left your songs and presets in $UserDataDir (pass -RemoveUserData to delete them too)"
    }

    Write-Host ''
    Write-Host "$AppName has been uninstalled." -ForegroundColor Green
    exit 0
}

# ---------------------------------------------------------------------------
# GitHub release lookup
# ---------------------------------------------------------------------------

function Get-GitHubHeaders {
    $headers = @{
        'User-Agent' = 'SoundStudio-Installer'
        'Accept'     = 'application/vnd.github+json'
    }
    if ($env:GITHUB_TOKEN) { $headers['Authorization'] = "Bearer $($env:GITHUB_TOKEN)" }
    return $headers
}

function Get-Release {
    param([string] $Repository, [string] $Tag)

    $url = if ($Tag -eq 'latest') {
        "https://api.github.com/repos/$Repository/releases/latest"
    } else {
        "https://api.github.com/repos/$Repository/releases/tags/$Tag"
    }

    try {
        return Invoke-RestMethod -Uri $url -Headers (Get-GitHubHeaders) -Method Get
    } catch {
        $status = $null
        try { $status = [int] $_.Exception.Response.StatusCode } catch { }

        switch ($status) {
            404 {
                if ($Tag -eq 'latest') {
                    Stop-WithError "No published release found for '$Repository'. Check the repository name, and make sure at least one release exists and is not a draft."
                }
                Stop-WithError "Release tag '$Tag' does not exist in '$Repository'."
            }
            403 { Stop-WithError "GitHub refused the request (rate limit, or the repository is private). Set `$env:GITHUB_TOKEN and try again." }
            401 { Stop-WithError 'GitHub rejected $env:GITHUB_TOKEN. Check that the token is valid and has repo access.' }
            default { Stop-WithError "Could not reach the GitHub API: $($_.Exception.Message)" }
        }
    }
}

function Select-ReleaseAsset {
    param($Release, [string] $Pattern)

    $assets = @($Release.assets)
    if ($assets.Count -eq 0) {
        Stop-WithError "Release '$($Release.tag_name)' has no attached files. Publish a build (a .zip or the .exe) as a release asset first."
    }

    if ($Pattern) {
        $match = $assets | Where-Object { $_.name -like $Pattern } | Select-Object -First 1
        if (-not $match) {
            $names = ($assets | ForEach-Object { $_.name }) -join ', '
            Stop-WithError "No asset matches '$Pattern'. This release has: $names"
        }
        return $match
    }

    # Preference order: a Windows x64 zip, then any zip, then a bare .exe.
    # Checksum sidecars are never install candidates.
    $candidates = $assets | Where-Object { $_.name -notmatch '\.(sha256|sha512|md5|txt|asc|sig)$' }

    foreach ($rule in @('*win*64*.zip', '*windows*.zip', '*x64*.zip', '*.zip', "*$ExeName", '*.exe')) {
        $match = $candidates | Where-Object { $_.name -like $rule } | Select-Object -First 1
        if ($match) { return $match }
    }

    Stop-WithError "Couldn't work out which file to install from release '$($Release.tag_name)'. Re-run with -AssetPattern to name it explicitly."
}

function Get-ChecksumAsset {
    param($Release, [string] $AssetName)

    return $Release.assets |
        Where-Object { $_.name -eq "$AssetName.sha256" -or $_.name -match '^(checksums|SHA256SUMS)(\.txt)?$' } |
        Select-Object -First 1
}

function Save-ReleaseAsset {
    param($Asset, [string] $Destination)

    # A private repo's browser_download_url needs a token anyway, and the API
    # asset endpoint is the documented way to fetch one authenticated, so use
    # that whenever a token is present.
    $headers = Get-GitHubHeaders
    $uri     = $Asset.browser_download_url
    if ($env:GITHUB_TOKEN) {
        $uri = $Asset.url
        $headers['Accept'] = 'application/octet-stream'
    }

    # Invoke-WebRequest's progress bar makes large downloads several times
    # slower on Windows PowerShell 5.1. Suppress it just for the download.
    $previousProgress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    try {
        Invoke-WebRequest -Uri $uri -Headers $headers -OutFile $Destination -UseBasicParsing
    } finally {
        $ProgressPreference = $previousProgress
    }

    if (-not (Test-Path -LiteralPath $Destination)) {
        Stop-WithError "Download produced no file: $($Asset.name)"
    }

    $size = (Get-Item -LiteralPath $Destination).Length
    if (($Asset.PSObject.Properties.Name -contains 'size') -and $Asset.size -and $size -ne $Asset.size) {
        Stop-WithError "Download is incomplete ($size of $($Asset.size) bytes). Try again."
    }
}

function Test-Checksum {
    param([string] $FilePath, [string] $AssetName, $ChecksumAsset)

    $tempSums = Join-Path ([IO.Path]::GetTempPath()) ([IO.Path]::GetRandomFileName())
    try {
        Save-ReleaseAsset -Asset $ChecksumAsset -Destination $tempSums
        $text = Get-Content -LiteralPath $tempSums -Raw

        # Handles both "<hash>" on its own and the usual "<hash>  <filename>"
        # lines of a combined checksums file.
        $expected = $null
        foreach ($line in ($text -split "`n")) {
            $line = $line.Trim()
            if (-not $line) { continue }
            if ($line -match '^([0-9a-fA-F]{64})(\s+\*?(.+))?$') {
                $name = if ($Matches.ContainsKey(3) -and $Matches[3]) { $Matches[3].Trim() } else { $AssetName }
                if ($name -eq $AssetName) { $expected = $Matches[1].ToLower(); break }
            }
        }
        if (-not $expected) { return $null }

        $actual = (Get-FileHash -LiteralPath $FilePath -Algorithm SHA256).Hash.ToLower()
        return ($actual -eq $expected)
    } catch {
        return $null   # couldn't check -- not the same thing as a failed check
    } finally {
        Remove-Item -LiteralPath $tempSums -Force -ErrorAction SilentlyContinue
    }
}

function Expand-ZipTo {
    param([string] $ZipPath, [string] $Destination)

    if (-not ('System.IO.Compression.ZipFile' -as [type])) {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
    }
    [IO.Compression.ZipFile]::ExtractToDirectory($ZipPath, $Destination)
}

# ---------------------------------------------------------------------------
# Bundled content: the instrument-preset and plugin pick-list
#
# SoundStudio ships ~108 instrument presets and 3 Lua plugins inside the
# executable. Rather than keeping a copy of that catalogue here -- which would
# drift the first time a preset is added -- the installer asks the executable
# what it has (--list-defaults) and hands back the chosen ids
# (--install-defaults). Both modes write to a file instead of stdout, because
# SoundStudio is a GUI-subsystem binary with no console to print to.
# ---------------------------------------------------------------------------

# Runs one of the headless content modes. The executable is a GUI binary, so
# `&` would not wait for it -- hence Start-Process -Wait. The timeout matters:
# an older SoundStudio.exe doesn't know these switches and would just open its
# window, leaving the installer waiting on it forever.
function Invoke-ContentMode {
    param([string] $Exe, [string] $Mode, [string] $FilePath, [int] $TimeoutSeconds = 30)

    try {
        # No -WindowStyle: it's a Windows-only parameter, and it would buy
        # nothing here anyway -- these modes return before SoundStudio opens
        # a window, and it's a GUI-subsystem binary, so there's no console
        # flash either.
        $process = Start-Process -FilePath $Exe -ArgumentList @($Mode, $FilePath) `
                                 -PassThru -ErrorAction Stop
    } catch {
        return @{ Ok = $false; Reason = "couldn't start $Exe ($($_.Exception.Message))" }
    }

    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $process.Kill() } catch { }
        return @{ Ok = $false; Reason = "SoundStudio.exe didn't respond to $Mode (an older build that doesn't support it?)" }
    }

    if ($process.ExitCode -ne 0) {
        return @{ Ok = $false; Reason = "$Mode failed with exit code $($process.ExitCode)" }
    }
    return @{ Ok = $true; Reason = '' }
}

# Catalogue JSON -> the grouped tree the picker walks. Instrument families keep
# the order the executable reported them in (which is the order they appear in
# the source table), so the list reads Pianos, Organs, Guitars, ... rather than
# alphabetically.
# Both content sources -- the GitHub branches and the executable's embedded
# catalogue -- reduce to the same shape of row, so the picker only ever has to
# understand one. Group order is first-seen, which keeps the instrument
# families in the order they're defined rather than alphabetical.
function New-TreeFromRows {
    param($Rows, [string[]] $ExpandedGroups = @())

    $groups = [System.Collections.Generic.List[object]]::new()
    $byName = @{}

    foreach ($row in $Rows) {
        if (-not $byName.ContainsKey($row.Group)) {
            $group = [pscustomobject]@{
                Name     = $row.Group
                Expanded = ($ExpandedGroups -contains $row.Group)
                Items    = [System.Collections.Generic.List[object]]::new()
            }
            $byName[$row.Group] = $group
            $groups.Add($group)
        }
        $byName[$row.Group].Items.Add([pscustomobject]@{
            Id       = $row.Id
            Label    = $row.Label
            Note     = $row.Note
            Selected = $true
        })
    }

    return $groups
}

function New-ContentTree {
    param($Catalogue)

    $rows = [System.Collections.Generic.List[object]]::new()
    foreach ($inst in $Catalogue.instruments) {
        $rows.Add(@{ Id = "instrument:$($inst.id)"; Label = $inst.name; Group = $inst.group; Note = '' })
    }
    foreach ($plugin in $Catalogue.plugins) {
        $rows.Add(@{ Id = "plugin:$($plugin.id)"; Label = $plugin.name; Group = 'Plugins'; Note = $plugin.description })
    }
    # Plugins are the handful worth actually reading, so they start open.
    return New-TreeFromRows -Rows $rows -ExpandedGroups @('Plugins')
}

function Get-GroupState {
    param($Group)
    $total = @($Group.Items).Count
    if ($total -eq 0) { return 'none' }
    $selected = @($Group.Items | Where-Object { $_.Selected }).Count
    if ($selected -eq 0)      { return 'none' }
    if ($selected -eq $total) { return 'all' }
    return 'some'
}

function Set-GroupSelection {
    param($Group, [bool] $Selected)
    foreach ($item in $Group.Items) { $item.Selected = $Selected }
}

# Flattens the tree into the rows actually on screen: every group, plus the
# items of the expanded ones.
function Get-VisibleRows {
    param($Tree)

    $rows = [System.Collections.Generic.List[object]]::new()
    foreach ($group in $Tree) {
        $rows.Add([pscustomobject]@{ Kind = 'Group'; Group = $group; Item = $null })
        if ($group.Expanded) {
            foreach ($item in $group.Items) {
                $rows.Add([pscustomobject]@{ Kind = 'Item'; Group = $group; Item = $item })
            }
        }
    }
    return $rows
}

function Get-SelectedIds {
    param($Tree)
    $ids = [System.Collections.Generic.List[string]]::new()
    foreach ($group in $Tree) {
        foreach ($item in $group.Items) {
            if ($item.Selected) { $ids.Add($item.Id) }
        }
    }
    return $ids.ToArray()
}

function Get-SelectionSummary {
    param($Tree)
    $selected = 0; $total = 0
    foreach ($group in $Tree) {
        foreach ($item in $group.Items) {
            $total++
            if ($item.Selected) { $selected++ }
        }
    }
    return @{ Selected = $selected; Total = $total }
}

# True when there is a real console to draw a menu on and read keys from.
function Test-CanPrompt {
    try {
        if ([Console]::IsInputRedirected) { return $false }
        if (-not [Environment]::UserInteractive) { return $false }
        $null = $Host.UI.RawUI.WindowSize
        return $true
    } catch {
        return $false
    }
}

# The one impure step of the picker loop, behind its own function so the
# selection logic can be driven from a test with a scripted key sequence
# instead of a keyboard.
function Read-PickerKey {
    return [Console]::ReadKey($true)
}

function Show-ContentPicker {
    param($Tree)

    $cursor = 0
    $scroll = 0

    while ($true) {
        $rows = @(Get-VisibleRows -Tree $Tree)
        if ($cursor -ge $rows.Count) { $cursor = $rows.Count - 1 }
        if ($cursor -lt 0) { $cursor = 0 }

        # Full redraw each keystroke. Cheaper approaches need cursor
        # positioning, which throws in hosts that don't own a real screen
        # buffer -- and this list is short enough that it doesn't matter.
        $height = 20
        try { $height = [Math]::Max(8, $Host.UI.RawUI.WindowSize.Height - 10) } catch { }

        if ($cursor -lt $scroll) { $scroll = $cursor }
        if ($cursor -ge $scroll + $height) { $scroll = $cursor - $height + 1 }
        if ($scroll -lt 0) { $scroll = 0 }

        Clear-Host
        Write-Host ''
        Write-Host '  Choose what to install' -ForegroundColor White
        Write-Host '  Presets go in your SoundStudio\Instruments folder, plugins in Plugins\.' -ForegroundColor DarkGray
        Write-Host ''

        $last = [Math]::Min($rows.Count - 1, $scroll + $height - 1)
        for ($i = $scroll; $i -le $last; $i++) {
            $row       = $rows[$i]
            $isCursor  = ($i -eq $cursor)
            $pointer   = if ($isCursor) { '>' } else { ' ' }

            if ($row.Kind -eq 'Group') {
                $state = Get-GroupState -Group $row.Group
                $box   = switch ($state) { 'all' { '[x]' } 'some' { '[-]' } default { '[ ]' } }
                $arrow = if ($row.Group.Expanded) { '-' } else { '+' }
                $count = @($row.Group.Items).Count
                $text  = "  $pointer $box $arrow $($row.Group.Name) ($count)"
                $color = if ($isCursor) { 'Cyan' } else { 'White' }
            }
            else {
                $box   = if ($row.Item.Selected) { '[x]' } else { '[ ]' }
                $text  = "  $pointer       $box $($row.Item.Label)"
                if ($row.Item.Note) { $text += "  -- $($row.Item.Note)" }
                $color = if ($isCursor) { 'Cyan' } else { 'Gray' }
            }

            # Trim rather than wrap: a wrapped row would break the one-row-per-
            # entry arithmetic the cursor and scrolling depend on.
            $width = 100
            try { $width = [Math]::Max(40, $Host.UI.RawUI.WindowSize.Width - 2) } catch { }
            if ($text.Length -gt $width) { $text = $text.Substring(0, $width - 1) + [char]0x2026 }

            Write-Host $text -ForegroundColor $color
        }

        if ($rows.Count -gt $height) {
            Write-Host "  ... $($rows.Count - $height) more (scroll with the arrow keys)" -ForegroundColor DarkGray
        }

        $summary = Get-SelectionSummary -Tree $Tree
        Write-Host ''
        Write-Host "  $($summary.Selected) of $($summary.Total) selected" -ForegroundColor Green
        Write-Host '  Up/Down move   Space toggle   Right/Left expand/collapse' -ForegroundColor DarkGray
        Write-Host '  A all   N none   Enter install   Esc skip' -ForegroundColor DarkGray

        $key = Read-PickerKey
        switch ($key.Key) {
            'UpArrow'    { $cursor-- }
            'DownArrow'  { $cursor++ }
            'PageUp'     { $cursor -= $height }
            'PageDown'   { $cursor += $height }
            'Home'       { $cursor = 0 }
            'End'        { $cursor = $rows.Count - 1 }

            'RightArrow' {
                if ($rows[$cursor].Kind -eq 'Group') { $rows[$cursor].Group.Expanded = $true }
            }
            'LeftArrow'  {
                # From an item, collapsing jumps back up to its own group --
                # otherwise the row under the cursor vanishes from under it.
                if ($rows[$cursor].Kind -eq 'Item') {
                    $group = $rows[$cursor].Group
                    $group.Expanded = $false
                    $newRows = @(Get-VisibleRows -Tree $Tree)
                    for ($j = 0; $j -lt $newRows.Count; $j++) {
                        if ($newRows[$j].Kind -eq 'Group' -and $newRows[$j].Group -eq $group) { $cursor = $j; break }
                    }
                } else {
                    $rows[$cursor].Group.Expanded = $false
                }
            }

            'Spacebar' {
                $row = $rows[$cursor]
                if ($row.Kind -eq 'Group') {
                    # A part-selected group fills in rather than emptying --
                    # the same way a tri-state checkbox behaves everywhere else.
                    $turnOn = (Get-GroupState -Group $row.Group) -ne 'all'
                    Set-GroupSelection -Group $row.Group -Selected $turnOn
                } else {
                    $row.Item.Selected = -not $row.Item.Selected
                }
            }

            'Enter'  { Clear-Host; return $true }
            'Escape' { Clear-Host; return $false }

            default {
                switch ("$($key.KeyChar)".ToUpper()) {
                    'A' { foreach ($g in $Tree) { Set-GroupSelection -Group $g -Selected $true } }
                    'N' { foreach ($g in $Tree) { Set-GroupSelection -Group $g -Selected $false } }
                }
            }
        }
    }
}

# ---------------------------------------------------------------------------
# Content branches
#
# The instrument presets, custom waves and plugins live on their own branches
# of the repository -- Instruments, Waves and Plugins -- rather than in the
# source tree. Reading them from there rather than from the executable means
# the library can grow by pushing a file, with no new release; the embedded
# copy inside SoundStudio.exe stays as the offline fallback.
# ---------------------------------------------------------------------------

$ContentBranches = @(
    [pscustomobject]@{ Branch = 'Instruments'; Extension = '.ssip';   Folder = 'Instruments'; Label = 'Instruments' }
    [pscustomobject]@{ Branch = 'Waves';       Extension = '.sswave'; Folder = 'Waves';       Label = 'Waves' }
    [pscustomobject]@{ Branch = 'Plugins';     Extension = '.lua';    Folder = 'Plugins';     Label = 'Plugins' }
)

# One API call per branch: the recursive trees endpoint returns every blob at
# once, so this works whether the files sit flat in the branch root or are
# sorted into folders. Returns $null when the branch simply isn't there --
# which is a normal state, not an error, for a repo that only uses some of them.
function Get-BranchFiles {
    param([string] $Repository, $Spec)

    $uri = "https://api.github.com/repos/$Repository/git/trees/$($Spec.Branch)?recursive=1"
    try {
        $tree = Invoke-RestMethod -Uri $uri -Headers (Get-GitHubHeaders) -Method Get
    } catch {
        $status = $null
        try { $status = [int] $_.Exception.Response.StatusCode } catch { }
        if ($status -eq 404) { return $null }
        throw
    }

    if ($tree.PSObject.Properties.Name -contains 'truncated' -and $tree.truncated) {
        Write-Note "The $($Spec.Branch) branch is too large for one listing; some files won't be offered."
    }

    $files = [System.Collections.Generic.List[object]]::new()
    foreach ($entry in $tree.tree) {
        if ($entry.type -ne 'blob') { continue }
        if (-not $entry.path.EndsWith($Spec.Extension, [StringComparison]::OrdinalIgnoreCase)) { continue }

        $leaf = Split-Path $entry.path -Leaf
        $files.Add([pscustomobject]@{
            Branch = $Spec.Branch
            Folder = $Spec.Folder
            Path   = $entry.path
            Name   = $leaf
            Stem   = [IO.Path]::GetFileNameWithoutExtension($leaf)
            Dir    = (((Split-Path $entry.path -Parent) -replace '\\', '/'))
        })
    }
    return $files
}

# Asks the installed executable what families its own presets belong to, purely
# so a flat Instruments branch can still be grouped as Pianos / Organs /
# Guitars rather than one 108-entry list. Entirely optional: without it the
# files just group under the branch name.
function Get-EmbeddedFamilyMap {
    param([string] $Exe)

    $map = @{}
    if (-not (Test-Path -LiteralPath $Exe)) { return $map }

    $temp = Join-Path ([IO.Path]::GetTempPath()) ("ss-catalogue-" + [Guid]::NewGuid().ToString('N') + '.json')
    try {
        $listed = Invoke-ContentMode -Exe $Exe -Mode '--list-defaults' -FilePath $temp
        if (-not $listed.Ok) { return $map }

        $catalogue = Get-Content -LiteralPath $temp -Raw | ConvertFrom-Json
        foreach ($inst in $catalogue.instruments) { $map[$inst.id] = $inst.group }
    } catch {
        # A best-effort nicety; never worth failing the install over.
    } finally {
        Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
    }
    return $map
}

function New-GitHubContentTree {
    param($Files, $FamilyMap)

    $rows = [System.Collections.Generic.List[object]]::new()
    foreach ($file in $Files) {
        # Grouping, best information first: a folder inside the branch is the
        # author's own grouping, so it wins; failing that the family the
        # executable knows this preset by; failing that, the branch itself.
        $group =
            if ($file.Dir)                        { "$($file.Branch)/$($file.Dir)" }
            elseif ($FamilyMap.ContainsKey($file.Stem)) { $FamilyMap[$file.Stem] }
            else                                  { $file.Branch }

        $rows.Add(@{
            Id    = "github:$($file.Branch)|$($file.Path)"
            Label = $file.Stem
            Group = $group
            Note  = ''
        })
    }
    # Waves and Plugins are short lists; instrument families stay collapsed.
    return New-TreeFromRows -Rows $rows -ExpandedGroups @('Waves', 'Plugins')
}

# Downloads the ticked files into the user's SoundStudio folders. raw.
# githubusercontent.com serves these, which (unlike the API) has no meaningful
# rate limit, so a hundred-odd small files is fine.
function Install-GitHubContent {
    param([string] $Repository, [string[]] $Ids, [string] $Root)

    $downloaded = 0
    $skipped    = 0
    $failed     = 0

    $previousProgress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    try {
        $index = 0
        foreach ($id in $Ids) {
            $index++
            if ($id -notmatch '^github:([^|]+)\|(.+)$') { continue }
            $branch = $Matches[1]
            $path   = $Matches[2]

            $spec = $ContentBranches | Where-Object { $_.Branch -eq $branch } | Select-Object -First 1
            if (-not $spec) { continue }

            $destDir = Join-Path $Root $spec.Folder
            if (-not (Test-Path -LiteralPath $destDir)) {
                New-Item -ItemType Directory -Path $destDir -Force | Out-Null
            }

            $dest = Join-Path $destDir (Split-Path $path -Leaf)
            if (Test-Path -LiteralPath $dest) { $skipped++; continue }  # never clobber a file already there

            # The path has to survive as a URL: raw.githubusercontent.com wants
            # each segment escaped, but the slashes between them left alone.
            $encoded = ($path -split '/' | ForEach-Object { [Uri]::EscapeDataString($_) }) -join '/'
            $uri = "https://raw.githubusercontent.com/$Repository/$branch/$encoded"

            try {
                $headers = @{ 'User-Agent' = 'SoundStudio-Installer' }
                if ($env:GITHUB_TOKEN) { $headers['Authorization'] = "Bearer $($env:GITHUB_TOKEN)" }
                Invoke-WebRequest -Uri $uri -Headers $headers -OutFile $dest -UseBasicParsing
                $downloaded++
            } catch {
                $failed++
            }

            if ($index % 20 -eq 0) { Write-Info "  $index of $($Ids.Count)..." }
        }
    } finally {
        $ProgressPreference = $previousProgress
    }

    return @{ Downloaded = $downloaded; Skipped = $skipped; Failed = $failed }
}

# Stamps <SoundStudio>/defaults.manifest so the app's first launch doesn't then
# install its whole embedded library on top of what was just downloaded. Only
# the file's existence carries meaning (see app/DefaultContent.h); the lines
# inside it are for whoever opens it.
function Write-ContentManifest {
    param([string] $Root, [string[]] $Ids, [string] $SourceNote)

    if (-not (Test-Path -LiteralPath $Root)) {
        New-Item -ItemType Directory -Path $Root -Force | Out-Null
    }

    $lines = @(
        '# SoundStudio bundled-content manifest',
        '#',
        "# Written by the installer ($SourceNote).",
        '# Its existence is what stops SoundStudio re-creating presets and',
        '# plugins you have since deleted. Delete it to be offered the whole',
        '# bundled library again on the next launch.',
        'version 1'
    ) + $Ids

    Set-Content -LiteralPath (Join-Path $Root 'defaults.manifest') -Value $lines -Encoding ASCII
}

function Invoke-DefaultContentSetup {
    param([string] $Directory, [string] $Mode, [string] $Repository, [string] $Source)

    if ($Mode -eq 'Skip') {
        Write-Info 'Skipping the preset/plugin step; SoundStudio will install its full library on first launch.'
        return
    }

    $exe = Join-Path $Directory $ExeName
    Write-Step 'Instrument presets, waves and plugins'

    # ---- where the catalogue comes from ----
    $tree = $null
    $kind = ''

    if ($Source -ne 'Embedded') {
        $files = [System.Collections.Generic.List[object]]::new()
        $missingBranches = [System.Collections.Generic.List[string]]::new()
        $reachable = $true

        foreach ($spec in $ContentBranches) {
            try {
                $branchFiles = Get-BranchFiles -Repository $Repository -Spec $spec
            } catch {
                Write-Note "Couldn't read the $($spec.Branch) branch: $($_.Exception.Message)"
                $reachable = $false
                break
            }
            if ($null -eq $branchFiles) { $missingBranches.Add($spec.Branch); continue }
            foreach ($f in $branchFiles) { $files.Add($f) }
        }

        if ($reachable -and $files.Count -gt 0) {
            foreach ($b in $missingBranches) { Write-Info "No $b branch in $Repository; skipping it." }
            $familyMap = Get-EmbeddedFamilyMap -Exe $exe
            $tree = New-GitHubContentTree -Files $files -FamilyMap $familyMap
            $kind = 'GitHub'
            Write-Ok "$($files.Count) file(s) on the content branches of $Repository"
        }
        elseif ($Source -eq 'GitHub') {
            Write-Note "Nothing available from the content branches of $Repository, and -ContentSource GitHub rules out the built-in library."
            return
        }
        else {
            Write-Info 'Content branches unavailable; using the library built into SoundStudio.exe instead.'
        }
    }

    if (-not $tree) {
        if (-not (Test-Path -LiteralPath $exe)) { return }

        $temp = Join-Path ([IO.Path]::GetTempPath()) ("ss-catalogue-" + [Guid]::NewGuid().ToString('N') + '.json')
        try {
            $listed = Invoke-ContentMode -Exe $exe -Mode '--list-defaults' -FilePath $temp
            if (-not $listed.Ok) {
                Write-Note "Couldn't read the bundled library: $($listed.Reason)"
                Write-Info 'SoundStudio will install its full library on first launch instead.'
                return
            }
            $catalogue = Get-Content -LiteralPath $temp -Raw | ConvertFrom-Json
        } catch {
            Write-Note "The bundled library listing was unreadable: $($_.Exception.Message)"
            return
        } finally {
            Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
        }

        $tree = New-ContentTree -Catalogue $catalogue
        $kind = 'Embedded'
        Write-Ok "$((Get-SelectionSummary -Tree $tree).Total) presets and plugins built into SoundStudio.exe"
    }

    # ---- what to install ----
    $effectiveMode = $Mode
    if ($Mode -eq 'Ask' -and -not (Test-CanPrompt)) {
        Write-Info 'No interactive console; installing everything. Use -DefaultContent None to skip.'
        $effectiveMode = 'All'
    }

    switch ($effectiveMode) {
        'None' { foreach ($g in $tree) { Set-GroupSelection -Group $g -Selected $false } }
        'All'  { foreach ($g in $tree) { Set-GroupSelection -Group $g -Selected $true } }
        'Ask'  {
            if (-not (Show-ContentPicker -Tree $tree)) {
                Write-Info 'Skipped; SoundStudio will install its full library on first launch.'
                return
            }
        }
    }

    $selected = @(Get-SelectedIds -Tree $tree)

    # ---- install it ----
    if ($kind -eq 'GitHub') {
        if ($selected.Count -gt 0) {
            Write-Info "Downloading $($selected.Count) file(s)..."
            $result = Install-GitHubContent -Repository $Repository -Ids $selected -Root $UserDataDir

            $parts = @("$($result.Downloaded) downloaded")
            if ($result.Skipped -gt 0) { $parts += "$($result.Skipped) already there" }
            if ($result.Failed  -gt 0) { $parts += "$($result.Failed) failed" }
            Write-Ok ($parts -join ', ')
        } else {
            Write-Ok 'Nothing installed, as chosen.'
        }

        # Stamped whatever was chosen, including nothing -- otherwise first
        # launch would install all 108 embedded presets over the top.
        Write-ContentManifest -Root $UserDataDir -Ids $selected -SourceNote "from the content branches of $Repository"
        return
    }

    $work = Join-Path ([IO.Path]::GetTempPath()) ("SoundStudio-content-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work -Force | Out-Null
    try {
        $selectionPath = Join-Path $work 'selection.txt'
        $header = @('# Written by the SoundStudio installer.', "# $($selected.Count) item(s) chosen.")
        Set-Content -LiteralPath $selectionPath -Value ($header + $selected) -Encoding ASCII

        $installed = Invoke-ContentMode -Exe $exe -Mode '--install-defaults' -FilePath $selectionPath
        if ($installed.Ok) {
            if ($selected.Count -eq 0) {
                Write-Ok 'Nothing installed, as chosen. Your Instruments and Plugins folders are left empty.'
            } else {
                $instruments = @($selected | Where-Object { $_ -like 'instrument:*' }).Count
                $plugins     = @($selected | Where-Object { $_ -like 'plugin:*' }).Count
                Write-Ok "Installed $instruments preset(s) and $plugins plugin(s)"
            }
        } else {
            Write-Note "Couldn't install the chosen presets: $($installed.Reason)"
        }
    }
    finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if ($Uninstall) { Invoke-Uninstall }

Write-Host ''
Write-Host "  $AppName installer" -ForegroundColor White
Write-Host ''

if ($Repo -eq 'OWNER/REPO') {
    Stop-WithError @'
No repository configured.

Either edit the $Repo default at the top of this script, or pass it in:

    .\install.ps1 -Repo yourname/SoundStudio
'@
}

if ($Repo -match '^https?://github\.com/([^/]+/[^/]+?)(\.git)?/?$') { $Repo = $Matches[1] }
if ($Repo -notmatch '^[\w.-]+/[\w.-]+$') {
    Stop-WithError "-Repo should look like 'owner/name' (got '$Repo')."
}

Write-Step "Looking up $Repo ($Version)"
$release = Get-Release -Repository $Repo -Tag $Version
$tag     = $release.tag_name
Write-Ok "Found release $tag"

# Already up to date?
if ((Test-Path -LiteralPath $ManifestPath) -and -not $Force) {
    try {
        $installed = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
        if ($installed.version -eq $tag) {
            Write-Host ''
            Write-Host "$AppName $tag is already installed in $InstallDir." -ForegroundColor Green
            Write-Info 'Pass -Force to reinstall it anyway.'
            Write-Host ''
            exit 0
        }
        Write-Info "Upgrading from $($installed.version)"
    } catch {
        Write-Info 'Existing install manifest is unreadable; reinstalling.'
    }
}

# Refuse to overwrite a running copy -- Windows will not let us, and a
# half-replaced install folder is worse than no change at all.
$running = @(Get-Process -Name 'SoundStudio' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    if (-not $Force) {
        Stop-WithError "$AppName is currently running. Close it and re-run, or pass -Force to close it automatically."
    }
    Write-Info 'Closing the running instance (-Force)...'
    $running | Stop-Process -Force
    Start-Sleep -Milliseconds 400
}

$asset = Select-ReleaseAsset -Release $release -Pattern $AssetPattern
$sizeMb = [math]::Round($asset.size / 1MB, 1)
Write-Step "Downloading $($asset.name) ($sizeMb MB)"

$workDir  = Join-Path ([IO.Path]::GetTempPath()) ("SoundStudio-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workDir -Force | Out-Null

try {
    $downloaded = Join-Path $workDir $asset.name
    Save-ReleaseAsset -Asset $asset -Destination $downloaded
    Write-Ok 'Download complete'

    # Clear the mark-of-the-web, or Windows SmartScreen blocks the extracted
    # executable with "this file came from another computer". Wrapped rather
    # than -ErrorAction'd: this cmdlet throws a *terminating* error on some
    # hosts, which SilentlyContinue won't swallow under $ErrorActionPreference
    # = 'Stop', and failing to unblock is never worth aborting an install over.
    try { Unblock-File -LiteralPath $downloaded } catch { }

    $checksumAsset = Get-ChecksumAsset -Release $release -AssetName $asset.name
    if ($checksumAsset) {
        Write-Step 'Verifying SHA-256'
        $result = Test-Checksum -FilePath $downloaded -AssetName $asset.name -ChecksumAsset $checksumAsset
        if ($result -eq $false) {
            Stop-WithError "Checksum mismatch on $($asset.name). The download is corrupt or has been tampered with; nothing was installed."
        } elseif ($null -eq $result) {
            Write-Note 'Could not read a checksum for this file; continuing without verification.'
        } else {
            Write-Ok 'Checksum matches'
        }
    } else {
        Write-Info 'No checksum published for this release; skipping verification.'
    }

    # ---- stage the payload ----
    $staging = Join-Path $workDir 'staged'
    New-Item -ItemType Directory -Path $staging -Force | Out-Null

    if ($asset.name -like '*.zip') {
        Write-Step 'Extracting'
        Expand-ZipTo -ZipPath $downloaded -Destination $staging

        # Zips built with "zip -r name.zip SoundStudio/" nest everything one
        # level down. Flatten that so the exe always lands at the top.
        $entries = @(Get-ChildItem -LiteralPath $staging -Force)
        if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) {
            $inner = $entries[0].FullName
            Get-ChildItem -LiteralPath $inner -Force | Move-Item -Destination $staging -Force
            Remove-Item -LiteralPath $inner -Recurse -Force
        }
    } else {
        Copy-Item -LiteralPath $downloaded -Destination (Join-Path $staging $ExeName) -Force
    }

    $exe = Get-ChildItem -LiteralPath $staging -Filter $ExeName -Recurse -File |
           Select-Object -First 1
    if (-not $exe) {
        Stop-WithError "$ExeName isn't in $($asset.name). Use -AssetPattern to pick the right release file."
    }

    # If the exe sat in a subfolder, install from that subfolder so its
    # sibling files (assets, DLLs) come along with it.
    $payloadRoot = $exe.Directory.FullName

    # ---- swap it into place ----
    Write-Step "Installing to $InstallDir"

    if (Test-Path -LiteralPath $InstallDir) {
        # Keep the old copy until the new one is in place, so a failure here
        # leaves the previous install intact rather than a gutted folder.
        $backup = "$InstallDir.old-$([DateTime]::Now.ToString('yyyyMMddHHmmss'))"
        Rename-Item -LiteralPath $InstallDir -NewName (Split-Path $backup -Leaf) -Force
    } else {
        $backup = $null
    }

    try {
        New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
        Get-ChildItem -LiteralPath $payloadRoot -Force | Copy-Item -Destination $InstallDir -Recurse -Force
    } catch {
        if ($backup -and (Test-Path -LiteralPath $backup)) {
            Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
            Rename-Item -LiteralPath $backup -NewName (Split-Path $InstallDir -Leaf) -Force
            Write-Note 'Install failed; the previous version has been restored.'
        }
        throw
    }

    if ($backup -and (Test-Path -LiteralPath $backup)) {
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    }

    $installedExe = Join-Path $InstallDir $ExeName
    try { Unblock-File -LiteralPath $installedExe } catch { }
    Write-Ok "Installed $AppName $tag"

    # ---- manifest, uninstaller, shortcuts, PATH ----
    @{
        name        = $AppName
        version     = $tag
        asset       = $asset.name
        repo        = $Repo
        installedAt = (Get-Date).ToString('o')
        installDir  = $InstallDir
    } | ConvertTo-Json | Set-Content -LiteralPath $ManifestPath -Encoding UTF8

    Write-UninstallScript -Directory $InstallDir
    Register-UninstallEntry -Tag $tag -Directory $InstallDir

    if (-not $NoShortcut) {
        New-AppShortcut -LinkPath $StartMenuLnk -TargetExe $installedExe -WorkDir $InstallDir
        Write-Ok 'Start Menu shortcut created'

        if ($DesktopShortcut) {
            New-AppShortcut -LinkPath $DesktopLnk -TargetExe $installedExe -WorkDir $InstallDir
            Write-Ok 'Desktop shortcut created'
        }
    }

    if ($AddToPath) {
        if (Add-ToUserPath -Directory $InstallDir) {
            Write-Ok 'Added to your PATH (open a new terminal to pick it up)'
        } else {
            Write-Info 'Already on your PATH'
        }
    }

    # Last, because it's the only step that can want the screen to itself.
    Invoke-DefaultContentSetup -Directory $InstallDir -Mode $DefaultContent `
                               -Repository $Repo -Source $ContentSource

    Write-Host ''
    Write-Host "  $AppName $tag is installed." -ForegroundColor Green
    Write-Host "  Program:   $InstallDir" -ForegroundColor DarkGray
    Write-Host "  Your work: $UserDataDir  (songs, presets, waves, plugins)" -ForegroundColor DarkGray
    Write-Host "  Uninstall: powershell -ExecutionPolicy Bypass -File `"$(Join-Path $InstallDir 'uninstall.ps1')`"" -ForegroundColor DarkGray
    Write-Host ''
}
finally {
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}
