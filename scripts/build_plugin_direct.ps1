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
    (Join-Path $kl "KenshiLib\Include"),
    # See vcenv.sh: from KenshiLib 0.4.0 the headers moved into subdirectories but
    # still include siblings relative to kenshi/, so kenshi/ must be on the path.
    (Join-Path $kl "KenshiLib\Include\kenshi"),
    (Join-Path $kl "KenshiLib\Include\ogre"),
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

# Stamp WHICH KenshiLib and ENet checkouts this DLL was compiled against - same
# mechanism as build_plugin.sh: a generated header plus a flag define, never
# __has_include (C++17-only; it silently evaluates to 0 on the v100 compiler,
# and that shipped once). -dirty is the HEALTHY state for both pins:
# patch_vendored_headers.sh mutates the deps and the two ENet patches mutate
# the ENet tree, by design.
function Get-GitPin([string]$dir, [string[]]$describeArgs) {
    if (-not (Test-Path -LiteralPath $dir)) { return "unknown" }
    try {
        $out = & git -C $dir describe @describeArgs 2>&1 | ForEach-Object { "$_" }
        if ($LASTEXITCODE -ne 0 -or -not $out) { return "unknown" }
        return ($out | Select-Object -First 1).Trim()
    } catch { return "unknown" }
}
$depsPin = Get-GitPin (Join-Path $repo "third_party\KenshiLib_deps") @("--tags","--always","--dirty")
$enetPin = Get-GitPin (Join-Path $repo "third_party\enet\enet") @("--always","--dirty")
# The honest build stamp: real time of THIS run plus the repo's own describe.
# Consumed only by BuildStamp.cpp, which the compile loop below recompiles
# unconditionally - a stamp read by an incrementally-skipped TU is exactly the
# lie the old __DATE__/__TIME__ banner told. Keep in lockstep with
# build_plugin.sh.
$repoPin    = Get-GitPin $repo @("--always","--dirty")
$buildStamp = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $repoPin
$genDir = Join-Path $repo "build\generated"
New-Item -ItemType Directory -Force -Path $genDir | Out-Null
@("// Generated by build_plugin_direct.ps1 - do not edit, do not commit.",
  "#define KENSHICOOP_DEPS_PIN `"$depsPin`"",
  "#define KENSHICOOP_ENET_PIN `"$enetPin`"",
  "#define KENSHICOOP_BUILD_STAMP `"$buildStamp`"") -join "`n" |
    Set-Content -LiteralPath (Join-Path $genDir "DepsPin.h") -NoNewline
$defs += "/DKENSHICOOP_HAVE_DEPS_PIN"
$env:INCLUDE += ";" + $genDir

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
# Scan EVERYTHING on the include path - src, vc10_compat, the KenshiLib deps
# (boost included) and ENet - matching the shell script: the old src-only scan
# made every vendored-header change a silent stale-object generator. build\ is
# deliberately excluded: DepsPin.h is regenerated on every run and would force
# a full rebuild every time.
$scanDirs = @("src", "third_party\vc10_compat",
              "third_party\KenshiLib_deps\KenshiLib\Include",
              "third_party\KenshiLib_deps\boost_1_60_0",
              "third_party\enet\enet\include") |
    ForEach-Object { Join-Path $repo $_ } |
    Where-Object { Test-Path -LiteralPath $_ }
$newestHeader = (Get-ChildItem -Path $scanDirs -Recurse -Include *.h,*.hpp -File -ErrorAction SilentlyContinue |
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
    # BuildStamp.cpp is NEVER skipped: it carries KENSHICOOP_BUILD_STAMP for
    # THIS run, and DepsPin.h is deliberately outside the header scan. Taking
    # the incremental skip here would freeze the stamp the way __DATE__ froze
    # the old banner. Keep in lockstep with build_plugin.sh.
    $isStamp = [System.IO.Path]::GetFileName($src) -eq "BuildStamp.cpp"
    # Delete-first for the always-compiled TU too: the post-compile success test
    # is "does the object exist", and a stale BuildStamp.obj surviving a failed
    # compile would pass it (the 2026-08-08 green-gate-for-broken-code lesson).
    if ($isStamp -and (Test-Path -LiteralPath $obj)) {
        Remove-Item -LiteralPath $obj -Force
    }
    if (-not $isStamp -and -not $env:KC_REBUILD -and (Test-Path -LiteralPath $obj)) {
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

# The DLL is loadable only if the main-loop hook resolves through KenshiLib's
# IMPORT table. Without /GL + /LTCG the stub is emitted as a local function,
# KenshiLib::GetRealAddress asserts on it, and the game dies before the plugin
# finishes loading - the fork-1..fork-5 dead-on-arrival class. Mirrors
# build_plugin.sh, where the check was falsified against a synthetic map
# carrying the exact broken line before being trusted.
$map = Join-Path $outDir "KenshiCoop.map"
$hookSym = "_NV_mainLoop_GPUSensitiveStuff"
if (-not (Test-Path -LiteralPath $map)) {
    Remove-Item -LiteralPath $dll -Force
    throw "no KenshiCoop.map produced - cannot verify the DLL is loadable"
}
if (-not (Select-String -Path $map -Pattern "__imp_.*$hookSym" -Quiet)) {
    Remove-Item -LiteralPath $dll -Force
    throw ("FATAL: $hookSym is not an import from KenshiLib in this build - " +
           "KenshiLib::GetRealAddress would assert before the plugin finishes " +
           "loading. Near-certain cause: /GL + /LTCG missing.")
}
if (Select-String -Path $map -Pattern "^ [0-9]{4}:[0-9A-Fa-f]{8} +\?$hookSym@" -Quiet) {
    Remove-Item -LiteralPath $dll -Force
    throw "FATAL: $hookSym has a LOCAL definition in this build - it would assert on startup."
}
Write-Host "loadable: $hookSym resolves through KenshiLib's import"

# ...and that both pins actually reached the binary. A C++17 __has_include
# guard on the C++03 compiler once compiled the stamp out while the build
# reported success. ASCII search over the raw bytes - the pins are pure ASCII.
$bytes = [System.IO.File]::ReadAllBytes($dll)
$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
foreach ($pin in @($depsPin, $enetPin)) {
    if (-not $ascii.Contains($pin)) {
        Remove-Item -LiteralPath $dll -Force
        throw ("FATAL: pin '$pin' is not in the built DLL - the stamp was " +
               "compiled out and the plugin will log 'unstamped'.")
    }
}
Write-Host "stamped: built against KenshiLib $depsPin, vendored ENet $enetPin"

Write-Host ("built: {0}  {1} bytes" -f $dll, (Get-Item $dll).Length)
# Stamp only AFTER a successful link, so a failed build cannot convince the next
# run that its objects match these flags.
Set-Content -LiteralPath $stamp -Value $stampNow -NoNewline

Write-Host ("sha256: {0}" -f (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLower())
