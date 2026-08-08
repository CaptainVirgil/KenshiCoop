<#
.SYNOPSIS
    Install or update the KenshiCoop co-op mod (CaptainVirgil fork) on Windows.

.DESCRIPTION
    Finds your Kenshi install, downloads the latest kit, verifies it, and swaps the
    mod folder. Both players must run the same build: a protocol mismatch is a hard
    connection reject, so this prints the build identity for you to compare.

    YOUR SAVES ARE NOT TOUCHED. Kenshi keeps them in
    %LOCALAPPDATA%\kenshi\save, which is nowhere near the mod folder. This script
    only ever writes inside <Kenshi>\mods\KenshiCoop and its own backup folder, and
    it refuses to run if the path it resolved does not look like that. Your
    coop_config.json is preserved across updates as well.

.PARAMETER KenshiPath
    Kenshi install directory. Auto-detected from Steam if omitted.

.PARAMETER Tag
    Release tag to install. Defaults to the latest release.

.PARAMETER FromZip
    Install a local kit zip instead of downloading (for testing a local build).

.PARAMETER Rollback
    Restore the most recent backup and exit.

.EXAMPLE
    .\update-kenshicoop.ps1
.EXAMPLE
    .\update-kenshicoop.ps1 -Rollback
#>
[CmdletBinding()]
param(
    [string]$KenshiPath = "",
    [string]$Tag        = "",
    [string]$FromZip    = "",
    [switch]$Rollback
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$Repo = "CaptainVirgil/KenshiCoop"

function Say  { param([string]$m) Write-Host $m }
function Note { param([string]$m) Write-Host "  $m" -ForegroundColor DarkGray }
function Ok   { param([string]$m) Write-Host "  OK   $m" -ForegroundColor Green }
function Warn { param([string]$m) Write-Host "  WARN $m" -ForegroundColor Yellow }
function Die  { param([string]$m) Write-Host "  STOP $m" -ForegroundColor Red; exit 1 }

# ---- 1. locate Kenshi ---------------------------------------------------------
# Steam can put a game in any library folder, so read the library list rather than
# guessing at Program Files.
function Find-Kenshi {
    $candidates = New-Object System.Collections.ArrayList

    $steamRoot = $null
    foreach ($key in @("HKCU:\Software\Valve\Steam", "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam")) {
        try {
            $p = (Get-ItemProperty -Path $key -ErrorAction Stop).SteamPath
            if ($p) { $steamRoot = $p.Replace("/", "\"); break }
        } catch { }
    }
    if (-not $steamRoot) {
        foreach ($guess in @("${env:ProgramFiles(x86)}\Steam", "$env:ProgramFiles\Steam")) {
            if (Test-Path $guess) { $steamRoot = $guess; break }
        }
    }

    if ($steamRoot) {
        [void]$candidates.Add((Join-Path $steamRoot "steamapps\common\Kenshi"))
        $vdf = Join-Path $steamRoot "steamapps\libraryfolders.vdf"
        if (Test-Path $vdf) {
            foreach ($line in Get-Content $vdf) {
                if ($line -match '"path"\s+"([^"]+)"') {
                    $lib = $matches[1].Replace("\\", "\")
                    [void]$candidates.Add((Join-Path $lib "steamapps\common\Kenshi"))
                }
            }
        }
    }
    # GOG / manual installs.
    [void]$candidates.Add("$env:ProgramFiles\Kenshi")
    [void]$candidates.Add("${env:ProgramFiles(x86)}\Kenshi")

    foreach ($c in $candidates) {
        if ((Test-Path (Join-Path $c "kenshi_x64.exe")) -and (Test-Path (Join-Path $c "mods"))) {
            return $c
        }
    }
    return $null
}

Say ""
Say "KenshiCoop updater  ($Repo)"
Say "============================================"

if (-not $KenshiPath) { $KenshiPath = Find-Kenshi }
if (-not $KenshiPath) {
    Die "could not find Kenshi. Re-run with -KenshiPath 'D:\path\to\Kenshi'"
}
if (-not (Test-Path (Join-Path $KenshiPath "kenshi_x64.exe"))) {
    Die "no kenshi_x64.exe in '$KenshiPath' - that is not a Kenshi install"
}
Ok "Kenshi: $KenshiPath"

$ModsDir   = Join-Path $KenshiPath "mods"
$ModDir    = Join-Path $ModsDir "KenshiCoop"
$BackupDir = Join-Path $KenshiPath "KenshiCoop-backups"

# Guard rail: everything destructive below happens under $ModDir, so make sure
# $ModDir really is what we think it is before anything gets deleted.
if ((Split-Path $ModDir -Leaf) -ne "KenshiCoop" -or
    (Split-Path (Split-Path $ModDir -Parent) -Leaf) -ne "mods") {
    Die "refusing to touch '$ModDir' - expected <Kenshi>\mods\KenshiCoop"
}

# Saves live here. Reported so it is obvious the script has no business with them.
$SaveDir = Join-Path $env:LOCALAPPDATA "kenshi\save"
if (Test-Path $SaveDir) {
    $saveCount = @(Get-ChildItem $SaveDir -Directory -ErrorAction SilentlyContinue).Count
    Note "saves: $SaveDir ($saveCount found) - not touched by this script"
} else {
    Note "saves: $SaveDir (none yet) - not touched by this script"
}

# ---- 2. refuse while the game is running --------------------------------------
# RE_Kenshi holds KenshiCoop.dll open, so replacing it under a live game either
# fails halfway or leaves the old code loaded and the new file on disk.
$running = @(Get-Process -Name "kenshi_x64" -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    Die "Kenshi is running - close it completely, then re-run"
}

# ---- 3. rollback --------------------------------------------------------------
function Restore-Backup {
    if (-not (Test-Path $BackupDir)) { Die "no backups in $BackupDir" }
    $latest = Get-ChildItem $BackupDir -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $latest) { Die "no backups in $BackupDir" }
    if (Test-Path $ModDir) { Remove-Item $ModDir -Recurse -Force }
    Copy-Item $latest.FullName $ModDir -Recurse -Force
    Ok "restored $($latest.Name)"
    exit 0
}
if ($Rollback) { Restore-Backup }

# ---- 4. fetch the kit ---------------------------------------------------------
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("kenshicoop-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $zipPath = Join-Path $tmp "kit.zip"

    if ($FromZip) {
        if (-not (Test-Path $FromZip)) { Die "no such file: $FromZip" }
        Copy-Item $FromZip $zipPath
        Ok "using local kit: $FromZip"
    } else {
        # TLS 1.2 for Windows PowerShell 5.1, whose default is too old for GitHub.
        try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch { }

        $api = if ($Tag) { "https://api.github.com/repos/$Repo/releases/tags/$Tag" }
               else      { "https://api.github.com/repos/$Repo/releases/latest" }
        Note "checking $api"
        try {
            $rel = Invoke-RestMethod -Uri $api -Headers @{ "User-Agent" = "kenshicoop-updater" }
        } catch {
            Die "could not reach GitHub: $($_.Exception.Message)"
        }
        $asset = $rel.assets | Where-Object { $_.name -like "KenshiCoop-kit*.zip" } | Select-Object -First 1
        if (-not $asset) { Die "release '$($rel.tag_name)' has no KenshiCoop-kit*.zip asset" }

        Note "downloading $($asset.name) from $($rel.tag_name)"
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath -UseBasicParsing
        Ok "downloaded $([math]::Round((Get-Item $zipPath).Length / 1MB, 1)) MB"
    }

    # ---- 5. unpack and verify -------------------------------------------------
    $stage = Join-Path $tmp "stage"
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction Stop
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $stage)
    } catch {
        Die "could not unpack the kit: $($_.Exception.Message)"
    }

    $newMod = Join-Path $stage "KenshiCoop"
    if (-not (Test-Path (Join-Path $newMod "KenshiCoop.dll"))) {
        Die "kit does not contain KenshiCoop\KenshiCoop.dll - refusing to install it"
    }

    # The manifest is what makes "are we on the same build?" answerable.
    $manifestPath = Join-Path $newMod "kit.json"
    $newProto = $null; $newLabel = "unknown"
    if (Test-Path $manifestPath) {
        $man = Get-Content $manifestPath -Raw | ConvertFrom-Json
        $newProto = $man.protocolVersion
        $newLabel = $man.label
        $actual = (Get-FileHash (Join-Path $newMod "KenshiCoop.dll") -Algorithm SHA256).Hash.ToLower()
        if ($man.dllSha256 -and $actual -ne $man.dllSha256.ToLower()) {
            Die "checksum mismatch - the download is corrupt or tampered with. Nothing changed."
        }
        Ok "verified $newLabel (protocol $newProto, commit $($man.commit))"
    } else {
        Warn "kit has no manifest - cannot verify checksum or protocol version"
    }

    # ---- 6. back up, preserving the player's config ---------------------------
    $keepConfig = $null
    if (Test-Path $ModDir) {
        $stamp  = Get-Date -Format "yyyyMMdd-HHmmss"
        $target = Join-Path $BackupDir $stamp
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        Copy-Item (Join-Path $ModDir "*") $target -Recurse -Force
        Ok "backed up current mod to KenshiCoop-backups\$stamp"

        $cfg = Join-Path $ModDir "coop_config.json"
        if (Test-Path $cfg) { $keepConfig = Get-Content $cfg -Raw }

        # Keep the last 5. Backups are cheap but not free, and this folder is
        # outside mods\ precisely so Kenshi never scans it as a mod.
        $old = Get-ChildItem $BackupDir -Directory | Sort-Object Name -Descending | Select-Object -Skip 5
        foreach ($o in $old) { Remove-Item $o.FullName -Recurse -Force }
    }

    # ---- 7. swap ---------------------------------------------------------------
    if (Test-Path $ModDir) { Remove-Item $ModDir -Recurse -Force }
    Copy-Item $newMod $ModDir -Recurse -Force

    $cfgOut     = Join-Path $ModDir "coop_config.json"
    $cfgDefault = Join-Path $ModDir "coop_config.default.json"
    if ($keepConfig) {
        Set-Content -Path $cfgOut -Value $keepConfig -Encoding UTF8
        Ok "kept your existing coop_config.json"
    } elseif (Test-Path $cfgDefault) {
        Copy-Item $cfgDefault $cfgOut
        Ok "wrote a default coop_config.json"
    }

    Ok "installed to $ModDir"
    Say ""
    Say "Done."
    Say "  Build:  $newLabel$(if ($newProto) { "  (protocol $newProto)" })"
    Say "  Both players must be on this same build, or the connection is refused."
    Say "  Saves untouched: $SaveDir"
    Say "  Roll back with:  .\update-kenshicoop.ps1 -Rollback"
    Say ""
    Say "  In game: enable KenshiCoop in the Mods menu, then press F2."
    Say ""
} finally {
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
}
