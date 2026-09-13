<#
.SYNOPSIS
    Packages a SoundStudio build into a release zip, and optionally publishes it
    to GitHub as a release.

.DESCRIPTION
    install.ps1 installs from a GitHub Release asset, so something has to put an
    asset there. This is that something: it zips build\SoundStudio.exe (plus any
    sibling DLLs), writes a checksums.txt the installer knows how to verify
    against, and can hand the result to `gh release create`.

.PARAMETER Version
    Release tag, e.g. "v1.0.0". Required.

.PARAMETER Build
    Run build.bat first instead of using whatever is already in build\.

.PARAMETER Publish
    Create the GitHub release and upload the assets, using the GitHub CLI (gh).
    Without this the files are just left in dist\ for you to upload by hand.

.PARAMETER Notes
    Release notes body. Defaults to a one-liner pointing at the install command.

.EXAMPLE
    .\package.ps1 -Version v1.0.0 -Build

.EXAMPLE
    .\package.ps1 -Version v1.0.0 -Publish
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v?\d+\.\d+(\.\d+)?([-.].+)?$')]
    [string] $Version,
    [switch] $Build,
    [switch] $Publish,
    [string] $Notes = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Version -notmatch '^v') { $Version = "v$Version" }

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildIn = Join-Path $root 'build'
$distDir = Join-Path $root 'dist'
$exePath = Join-Path $buildIn 'SoundStudio.exe'
$zipName = "SoundStudio-$Version-win64.zip"
$zipPath = Join-Path $distDir $zipName

function Write-Step { param([string] $m) Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok   { param([string] $m) Write-Host "    $m" -ForegroundColor Green }

if ($Build) {
    Write-Step 'Building'
    & (Join-Path $root 'build.bat')
    if ($LASTEXITCODE -ne 0) { throw "build.bat failed with exit code $LASTEXITCODE" }
}

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "$exePath not found. Run build.bat first, or pass -Build."
}

Write-Step "Packaging $Version"

if (Test-Path -LiteralPath $distDir) { Remove-Item -LiteralPath $distDir -Recurse -Force }
New-Item -ItemType Directory -Path $distDir -Force | Out-Null

# Stage the exe and anything else it needs at runtime. build.bat links raylib
# statically, so in practice this is just the one file -- but if w64devkit ever
# leaves you needing libgcc/libstdc++ DLLs, drop them in build\ and they'll be
# picked up here automatically. (Safer still: add -static to the g++ line in
# build.bat so there's nothing to ship alongside.)
$staging = Join-Path $distDir 'stage'
New-Item -ItemType Directory -Path $staging -Force | Out-Null
Copy-Item -LiteralPath $exePath -Destination $staging
Get-ChildItem -LiteralPath $buildIn -Filter '*.dll' -File |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $staging }

# Deliberately NOT shipping build\imgui.ini: the app writes its own default
# panel layout on first run, and bundling a developer's rearranged one would
# hand every new user someone else's window positions.

if (-not ('System.IO.Compression.ZipFile' -as [type])) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
}
[IO.Compression.ZipFile]::CreateFromDirectory(
    $staging, $zipPath, [IO.Compression.CompressionLevel]::Optimal, $false)
Remove-Item -LiteralPath $staging -Recurse -Force

$sizeMb = [math]::Round((Get-Item -LiteralPath $zipPath).Length / 1MB, 1)
Write-Ok "$zipName ($sizeMb MB)"

# install.ps1 looks for exactly this filename and parses "<sha256>  <file>".
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLower()
Set-Content -LiteralPath (Join-Path $distDir 'checksums.txt') `
            -Value "$hash  $zipName" -Encoding ASCII
Write-Ok "checksums.txt  ($hash)"

if (-not $Publish) {
    Write-Host ''
    Write-Host "  Ready in $distDir" -ForegroundColor Green
    Write-Host '  Upload both files as assets on a new GitHub release, or re-run with -Publish.' -ForegroundColor DarkGray
    Write-Host ''
    return
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw 'The GitHub CLI (gh) is not installed or not on PATH; install it from https://cli.github.com or upload dist\ by hand.'
}

if (-not $Notes) {
    $Notes = @"
SoundStudio $Version

Install on Windows:

    powershell -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/OWNER/REPO/main/install.ps1 -OutFile `$env:TEMP\ss.ps1; & `$env:TEMP\ss.ps1"

or download the zip below and run SoundStudio.exe.
"@
}

Write-Step "Publishing release $Version"
& gh release create $Version (Join-Path $distDir $zipName) (Join-Path $distDir 'checksums.txt') `
    --title "SoundStudio $Version" --notes $Notes
if ($LASTEXITCODE -ne 0) { throw "gh release create failed with exit code $LASTEXITCODE" }

Write-Host ''
Write-Host "  Published $Version" -ForegroundColor Green
Write-Host ''
