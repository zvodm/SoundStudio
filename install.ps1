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
    .\install.ps1 -Uninstall

.NOTES
    Set $env:GITHUB_TOKEN before running to lift the 60-requests/hour anonymous
    API rate limit, or to install from a private repository.
#>

[CmdletBinding()]
param(
    # TODO: replace with your repository once it's pushed, e.g. 'maksim/SoundStudio'.
    [string] $Repo         = 'OWNER/REPO',
    [string] $Version      = 'latest',
    [string] $InstallDir   = (Join-Path $env:LOCALAPPDATA 'SoundStudio'),
    [string] $AssetPattern = '',
    [switch] $DesktopShortcut,
    [switch] $NoShortcut,
    [switch] $AddToPath,
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
if (`$RemoveUserData) { Remove-Item -LiteralPath `$userData -Recurse -Force }

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
