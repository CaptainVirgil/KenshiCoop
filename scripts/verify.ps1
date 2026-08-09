<#
.SYNOPSIS
  Zero-game verification gate: the fast, game-independent safety net every
  commit and every refactor phase must keep green. Wraps the C++ unit layer
  (prototest - wire contract, content hash, interpolation buffer, and the other
  pure units), the transport layer (tunneltest - ENet over the Steam socket
  hooks, under loss and at the 1200 B datagram ceiling), and the PowerShell
  harness contract fixtures (manifest schema, scenario drift, oracle registry,
  verdict rule) into a single PASS/FAIL. Same four sections as the Linux twin,
  scripts/linux/verify.sh.

  Requires NO Kenshi launch and NO KenshiCoop.dll - only the two standalone
  test exes and the scripts. Run it before the two-client regression matrix
  (scripts\regress.ps1), which needs the game.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\verify.ps1

.EXAMPLE
  # Reuse an already-built prototest (skip the compile step).
  powershell -ExecutionPolicy Bypass -File scripts\verify.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
    # Reuse the existing dist\prototest.exe and dist\tunneltest.exe
    # instead of rebuilding them.
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Split-Path -Parent $scriptDir

$overall = $true

# Whichever PowerShell host is present. Windows PowerShell 5.1 stays the default
# on Windows; pwsh is what exists on Linux, where this file can also be driven
# directly. Resolved once, up here, because every section below that shells out
# to a .ps1 needs it - one of them used to hardcode `powershell` and would fail
# on any host that only has pwsh.
$psHost = $null
if (Get-Command powershell -ErrorAction SilentlyContinue) { $psHost = "powershell" }
elseif (Get-Command pwsh -ErrorAction SilentlyContinue)   { $psHost = "pwsh" }

# ---- 1. C++ unit layer (prototest: wire/hash/interp + the pure units) ----------
Write-Host "############################################################"
Write-Host "# verify: C++ unit layer (prototest)"
Write-Host "############################################################"
$prototest = Join-Path $repoRoot "dist\prototest.exe"
if (-not $SkipBuild) {
    # Delete the binary BEFORE rebuilding. Otherwise a build error leaves the
    # previous binary on disk and the run step below executes it, reporting a
    # green suite for code that does not compile - which is what the Linux gate
    # did on 2026-08-08 until it was fixed the same way. The exit-on-nonzero
    # below only covers a build that reports its own failure; a build that
    # exits 0 without producing a binary is caught by the Test-Path.
    Remove-Item -LiteralPath $prototest -Force -ErrorAction SilentlyContinue
    Write-Host "=== build prototest ==="
    & cmd.exe /c "`"$scriptDir\build_prototest.cmd`""
    if ($LASTEXITCODE -ne 0) { Write-Host "verify: FAIL (prototest build failed, exit $LASTEXITCODE)"; exit 1 }
}
if (Test-Path $prototest) {
    & $prototest
    $unitOk = ($LASTEXITCODE -eq 0)
    Write-Host ("UNIT LAYER: " + $(if ($unitOk) { "PASS" } else { "FAIL (exit $LASTEXITCODE)" }))
    if (-not $unitOk) { $overall = $false }
} else {
    Write-Host "UNIT LAYER: FAIL - dist\prototest.exe not found (run without -SkipBuild)"
    $overall = $false
}

# ---- 2. Transport layer (tunneltest: ENet over the Steam socket hooks) ---------
# build_tunneltest.cmd was orphaned - nothing invoked it - so the datagram
# ceiling and the loss behaviour were only ever checked by hand. The Linux gate
# runs it; this is the Windows twin.
Write-Host ""
Write-Host "############################################################"
Write-Host "# verify: transport layer (tunneltest)"
Write-Host "############################################################"
$tunneltest = Join-Path $repoRoot "dist\tunneltest.exe"
$tunnelOk = $true
if (-not $SkipBuild) {
    Remove-Item -LiteralPath $tunneltest -Force -ErrorAction SilentlyContinue
    Write-Host "=== build tunneltest ==="
    & cmd.exe /c "`"$scriptDir\build_tunneltest.cmd`""
    # Unlike prototest this does NOT exit the script: a broken transport test
    # should still let the contract fixtures report, and the summary carries the
    # overall verdict.
    if ($LASTEXITCODE -ne 0) { Write-Host "TRANSPORT LAYER: FAIL (build failed, exit $LASTEXITCODE)"; $tunnelOk = $false }
}
if ($tunnelOk) {
    if (Test-Path $tunneltest) {
        & $tunneltest
        $tunnelOk = ($LASTEXITCODE -eq 0)
        Write-Host ("TRANSPORT LAYER: " + $(if ($tunnelOk) { "PASS" } else { "FAIL (exit $LASTEXITCODE)" }))
    } else {
        Write-Host "TRANSPORT LAYER: FAIL - dist\tunneltest.exe not found (run without -SkipBuild)"
        $tunnelOk = $false
    }
}
if (-not $tunnelOk) { $overall = $false }

# ---- 3. PowerShell contract / drift fixtures (zero game) -----------------------
Write-Host ""
Write-Host "############################################################"
Write-Host "# verify: harness contract fixtures"
Write-Host "############################################################"
$fixtures = Join-Path (Join-Path $scriptDir "tests") "Contract.Tests.ps1"
if (Test-Path $fixtures) {
    # NOT an early return on failure: this runs at script scope, where `return`
    # would abandon the remaining sections and the summary.
    if (-not $psHost) {
        Write-Host "CONTRACT FIXTURES: FAIL - no PowerShell host found"
        $overall = $false
    } else {
        & $psHost -NoProfile -ExecutionPolicy Bypass -File $fixtures
        $fixOk = ($LASTEXITCODE -eq 0)
        Write-Host ("CONTRACT FIXTURES: " + $(if ($fixOk) { "PASS" } else { "FAIL (exit $LASTEXITCODE)" }))
        if (-not $fixOk) { $overall = $false }
    }
} else {
    Write-Host "CONTRACT FIXTURES: FAIL - scripts\tests\Contract.Tests.ps1 not found"
    $overall = $false
}

# ---- 4. Per-oracle fixtures on synthetic logs (zero game) ----------------------
# An oracle judged only by its own green runs is unfalsifiable: these feed each
# gate a log pair shaped like the BUG it exists for and require a FAIL, so a
# passing regression run means the gate looked rather than that it cannot look.
Write-Host ""
Write-Host "############################################################"
Write-Host "# verify: oracle fixtures (synthetic logs)"
Write-Host "############################################################"
$oracleOk = $true
foreach ($f in @("tests\CrawlMove.Fixture.ps1")) {
    $p = Join-Path $scriptDir $f
    if (-not (Test-Path $p)) {
        Write-Host "ORACLE FIXTURE: FAIL - scripts\$f not found"
        $oracleOk = $false
        continue
    }
    & $psHost -NoProfile -ExecutionPolicy Bypass -File $p
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("ORACLE FIXTURE ${f}: FAIL (exit $LASTEXITCODE)")
        $oracleOk = $false
    }
}
Write-Host ("ORACLE FIXTURES: " + $(if ($oracleOk) { "PASS" } else { "FAIL" }))
if (-not $oracleOk) { $overall = $false }

# ---- summary -------------------------------------------------------------------
Write-Host ""
Write-Host "================= VERIFY SUMMARY ================="
Write-Host ("  unit layer (prototest):   " + $(if ($unitOk) { "PASS" } else { "FAIL" }))
Write-Host ("  transport (tunneltest):   " + $(if ($tunnelOk) { "PASS" } else { "FAIL" }))
Write-Host ("  contract fixtures:        " + $(if ($fixOk)  { "PASS" } else { "FAIL" }))
Write-Host ("  oracle fixtures:          " + $(if ($oracleOk) { "PASS" } else { "FAIL" }))
Write-Host ("OVERALL: " + $(if ($overall) { "PASS" } else { "FAIL" }))
if ($overall) { exit 0 } else { exit 1 }
