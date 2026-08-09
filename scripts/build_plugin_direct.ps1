<#
.SYNOPSIS
    Build KenshiCoop.dll on Windows by driving cl.exe/link.exe directly, without MSBuild.

.DESCRIPTION
    The Windows twin of scripts/linux/build_plugin.sh, and the path to use on a
    machine that does not have a real Visual Studio 2010 installed.

    scripts\build_plugin.cmd goes through MSBuild, which needs two things an
    extracted toolchain cannot supply: the legacy v100 toolset props that a genuine
    VS2010/SDK 7.1 installer writes under MSBuild\Microsoft.Cpp\v4.0, and a Windows
    SDK registered the way Microsoft.Cpp.WindowsSDK.targets expects. Neither is
    needed to compile: the compiler itself only wants INCLUDE, LIB and PATH. This
    script sets those and invokes the toolchain directly, exactly as the Linux build
    does through Wine.

    KenshiCoop.vcxproj remains authoritative for the source list and its
    per-configuration exclusions - they are read out of the project file here, not
    duplicated.

.PARAMETER Config
    Harness (default), Release, or Debug.

.PARAMETER Toolchain
    Root of an extracted v100 toolchain: <root>\VS10\VC and <root>\SDK.
    Defaults to $env:KC_TOOLCHAIN.
#>
[CmdletBinding()]
param(
    [ValidateSet("Harness", "Release", "Debug")]
    [string]$Config    = "Harness",
    [string]$Toolchain = $env:KC_TOOLCHAIN
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

if (-not $Toolchain) {
    # A real VS2010 install works too; the extracted layout is just the common case.
    $vs10 = "C:\Program Files (x86)\Microsoft Visual Studio 10.0"
    $sdk  = "C:\Program Files\Microsoft SDKs\Windows\v7.1"
    if (-not (Test-Path -LiteralPath "$vs10\VC\bin\amd64\cl.exe")) {
        throw "no toolchain: set -Toolchain <root> (expects <root>\VS10\VC and <root>\SDK) or KC_TOOLCHAIN"
    }
} else {
    $vs10 = Join-Path $Toolchain "VS10"
    $sdk  = Join-Path $Toolchain "SDK"
}
$vc = Join-Path $vs10 "VC"
$cl   = Join-Path $vc "bin\amd64\cl.exe"
$link = Join-Path $vc "bin\amd64\link.exe"
if (-not (Test-Path -LiteralPath $cl)) { throw "no cl.exe at $cl" }

$kl   = Join-Path $repo "third_party\KenshiLib_deps"
$enet = Join-Path $repo "third_party\enet\enet\include"

$env:PATH    = "$vc\bin\amd64;$vc\bin;$sdk\Bin\x64;$sdk\Bin;$env:PATH"
$env:INCLUDE = @(
    (Join-Path $vc "include"), (Join-Path $sdk "Include"),
    (Join-Path $repo "third_party\vc10_compat"),
    (Join-Path $kl "KenshiLib\Include"), (Join-Path $kl "KenshiLib\Include\ogre"),
    (Join-Path $kl "boost_1_60_0"), $enet
) -join ";"
$env:LIB     = @(
    (Join-Path $vc "lib\amd64"), (Join-Path $sdk "Lib\x64"),
    (Join-Path $kl "KenshiLib\Libraries")
) -join ";"

switch ($Config) {
    "Release" { $defs = @("/DNDEBUG","/DKENSHICOOP_EXPORTS");                          $opt = @("/O2","/MD")  }
    "Harness" { $defs = @("/DNDEBUG","/DKENSHICOOP_EXPORTS","/DKENSHICOOP_HARNESS");   $opt = @("/O2","/MD")  }
    "Debug"   { $defs = @("/D_DEBUG","/DKENSHICOOP_EXPORTS","/DKENSHICOOP_HARNESS");   $opt = @("/Od","/MDd") }
}
# CharacterSet=Unicode in the vcxproj: KenshiLib headers hand L"" literals to Win32
# calls, so the W variants have to be selected.
$defs += @("/D_WINDOWS","/D_USRDLL","/DWIN32_LEAN_AND_MEAN","/DUNICODE","/D_UNICODE")

# Source list straight out of the project, honouring <ExcludedFromBuild>.
[xml]$proj = Get-Content -LiteralPath (Join-Path $repo "src\plugin\KenshiCoop.vcxproj")
$ns = "http://schemas.microsoft.com/developer/msbuild/2003"

# Whole program optimization, read from the vcxproj rather than assumed.
#
# LOAD-BEARING, not an optimization preference. It lives in a
# Label="Configuration" <PropertyGroup>, not in the <ItemDefinitionGroup> that
# holds every other compiler and linker setting - and this script read only the
# latter, so every DLL it produced was built WITHOUT /GL while the project asked
# for it. Without /GL, KenshiLib's member-function stubs are emitted as ordinary
# local functions, `&GameWorld::_NV_mainLoop_GPUSensitiveStuff` resolves inside
# KenshiCoop.dll, and KenshiLib::GetRealAddress asserts before the plugin
# finishes loading - assertion box, then crash. Its message literally advises
# enabling whole program optimization.
$ltcg = @()
foreach ($g in $proj.GetElementsByTagName("PropertyGroup")) {
    if ($g.GetAttribute("Label") -ne "Configuration") { continue }
    if ($g.GetAttribute("Condition") -notmatch [regex]::Escape("'$Config|x64'")) { continue }
    foreach ($w in $g.ChildNodes) {
        if ($w.LocalName -eq "WholeProgramOptimization" -and
            $w.InnerText.Trim().ToLower() -eq "true") {
            $opt  += "/GL"
            $ltcg  = @("/LTCG")
        }
    }
}
Write-Host ("=== whole program optimization: " + $(if ($ltcg.Count) { "ON (/GL + /LTCG)" } else { "off" }) + " ===")
$sources = @()
foreach ($item in $proj.GetElementsByTagName("ClCompile")) {
    $inc = $item.GetAttribute("Include")
    if (-not $inc) { continue }   # ItemDefinitionGroup entry: settings, not a source
    $excluded = $false
    foreach ($flag in $item.ChildNodes) {
        if ($flag.LocalName -ne "ExcludedFromBuild") { continue }
        $cond = $flag.GetAttribute("Condition")
        if ($cond -and $cond -notmatch [regex]::Escape("'$Config|")) {
            if ($cond -notmatch "'\`$\(Configuration\)'=='$Config'") { continue }
        }
        if ($flag.InnerText.Trim().ToLower() -eq "true") { $excluded = $true }
    }
    if (-not $excluded) { $sources += (Join-Path (Join-Path $repo "src\plugin") $inc) }
}
if ($sources.Count -eq 0) { throw "no sources parsed for $Config" }

$objDir = Join-Path $repo "build\$Config\obj-win"
$outDir = Join-Path $repo "build\$Config"
New-Item -ItemType Directory -Force -Path $objDir, $outDir | Out-Null
# Objects for the TUs we are about to rebuild are deleted individually below, so a
# failed compile cannot pass on a stale one. Untouched objects are kept on purpose.

# Same incremental rule as the Linux script: skip a translation unit whose object
# is newer than its source, and force a full rebuild when ANY header moved. The
# headers here are load-bearing (Replicator.h is included by nearly everything), so
# a source-only check would happily skip a TU a header change had invalidated.
$newestHeader = (Get-ChildItem -LiteralPath (Join-Path $repo "src") -Recurse -Filter *.h |
                 Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)
$headerStamp = if ($newestHeader) { $newestHeader.LastWriteTimeUtc } else { [datetime]::MinValue }

# Force a full rebuild when the FLAGS change, not just when sources do. The
# incremental check is timestamps only, so a flag change would leave every
# existing object in place and the link would silently mix objects built two
# different ways - which for /GL means mixing IL objects with native ones.
$stamp    = Join-Path $objDir ".buildflags"
$stampNow = "OPT=$($opt -join ' ') DEFS=$($defs -join ' ') LTCG=$($ltcg -join ' ')"
if (-not (Test-Path -LiteralPath $stamp) -or
    (Get-Content -LiteralPath $stamp -Raw) -ne $stampNow) {
    if (Test-Path -LiteralPath $stamp) {
        Write-Host "=== build flags changed since the last run - full rebuild ==="
    }
    Remove-Item -LiteralPath $objDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null
}

$toCompile = @()
foreach ($src in $sources) {
    $obj = Join-Path $objDir ([System.IO.Path]::GetFileNameWithoutExtension($src) + ".obj")
    if (-not $env:KC_REBUILD -and (Test-Path -LiteralPath $obj)) {
        $objTime = (Get-Item -LiteralPath $obj).LastWriteTimeUtc
        $srcTime = (Get-Item -LiteralPath $src).LastWriteTimeUtc
        if ($objTime -gt $srcTime -and $objTime -gt $headerStamp) { continue }
        Remove-Item -LiteralPath $obj -Force
    }
    $toCompile += $src
}

Write-Host "=== KenshiCoop.dll ($Config|x64, v100 direct) - $($toCompile.Count) of $($sources.Count) translation units ==="
if ($toCompile.Count -gt 0) {
# /MP fans out across cores, which is why this is one invocation rather than a loop.
& $cl /nologo /c /MP /EHsc /W3 /Gy /Oi @opt @defs "/Fo$objDir\" @toCompile 2>&1 |
    Where-Object { $_ -notmatch '^[A-Za-z0-9_.\\/-]+\.(cpp|c)$' } |
    ForEach-Object { Write-Host "  $_" }
}

# Link in SOURCE order, not directory order. /OPT:ICF folds identical COMDATs and
# the result depends on the order the linker sees them, so an alphabetical object
# list produces a subtly different .text than the Linux build does from the same
# sources - which makes the two artifacts impossible to compare. Same order, same
# layout.
$objs = @()
foreach ($src in $sources) {
    $obj = Join-Path $objDir ([System.IO.Path]::GetFileNameWithoutExtension($src) + ".obj")
    if (-not (Test-Path -LiteralPath $obj)) { throw "no object for $src - build failed" }
    $objs += $obj
}
$produced = @(Get-ChildItem -LiteralPath $objDir -Filter *.obj).Count
if ($produced -ne $sources.Count) {
    throw "compiled $produced of $($sources.Count) translation units - build failed"
}

Write-Host "=== linking $($objs.Count) objects ==="
& $link /nologo /DLL /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /DEBUG @ltcg `
    "/OUT:$outDir\KenshiCoop.dll" "/PDB:$outDir\KenshiCoop.pdb" "/MAP:$outDir\KenshiCoop.map" `
    @objs kenshilib.lib OgreMain_x64.lib MyGUIEngine_x64.lib ws2_32.lib winmm.lib `
    user32.lib kernel32.lib advapi32.lib shell32.lib ole32.lib 2>&1 |
    ForEach-Object { Write-Host "  $_" }

$dll = Join-Path $outDir "KenshiCoop.dll"
if (-not (Test-Path -LiteralPath $dll)) { throw "link produced no DLL" }
Write-Host ("built: {0}  {1} bytes" -f $dll, (Get-Item $dll).Length)
# Stamp only AFTER a successful link, so a failed build cannot convince the next
# run that its objects match these flags.
Set-Content -LiteralPath $stamp -Value $stampNow -NoNewline

Write-Host ("sha256: {0}" -f (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLower())
