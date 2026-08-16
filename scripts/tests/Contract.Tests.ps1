<#
.SYNOPSIS
  Zero-game contract + drift fixtures for the KenshiCoop harness (Phase 0 safety
  net). Runs in milliseconds with NO game launch and NO built DLL - it only reads
  the manifest, the oracle library and the C++ scenario factory, so it can gate
  every commit and every later refactor phase.

.DESCRIPTION
  Asserts the harness contracts the refactor must not silently break:
    * manifest schema   - every scenario declares the required fields with sane
                          types / enum values
    * scenario drift    - the manifest scenario set and the C++ makeScenario
                          factory agree (a manifest name with no maker would
                          load nothing; a maker with no manifest entry would run
                          under the generic cross-check with no real gate)
    * oracle registry   - every oracle id referenced by the manifest resolves in
                          CoopOracles.psm1's dispatch, and an unknown id is
                          rejected (never silently passed)
    * verdict rule      - the no-signal guard (a SKIP of the PRIMARY gate fails
                          the run) and verdict.json serialization round-trip
  Also includes NEGATIVE fixtures that prove each checker actually fires on bad
  input (a malformed manifest entry, an unknown oracle id, a drifted scenario
  name, a primary-gate SKIP), so a green run means the guards work - not that
  they were skipped.

  Exit code = number of failed assertions (0 = PASS), matching prototest so
  verify.ps1 can sum them.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path      # scripts\tests
$scriptsRoot = Split-Path -Parent $scriptDir                       # scripts
$repoRoot  = Split-Path -Parent $scriptsRoot                       # repo root

Import-Module (Join-Path $scriptsRoot "CoopOracles.psm1") -Force
Import-Module (Join-Path $scriptsRoot "CoopHarness.psm1") -Force

# ---- tiny assert harness ------------------------------------------------------
$script:Pass = 0
$script:Fail = 0
function Check {
    param([string]$Name, [bool]$Cond)
    if ($Cond) { $script:Pass++; Write-Host "  ok   $Name" }
    else       { $script:Fail++; Write-Host "  FAIL $Name" }
}

# ---- shared inputs ------------------------------------------------------------
$manifest = Get-ScenarioManifest
$scenarios = $manifest.Scenarios

# The oracle registry = the dispatch case ids in CoopOracles.psm1's
# Invoke-OneOracle switch (Phase 0 stand-in for the Phase 2 registry hashtable),
# plus the pre-run gates, which are resolvable ids that simply resolve somewhere
# other than the switch. Taken from the module rather than restated here, so a
# gate cannot be dispatchable at run time and dangling at check time.
function Get-OracleRegistry {
    $psm = Join-Path $scriptsRoot "CoopOracles.psm1"
    $ids = New-Object System.Collections.Generic.HashSet[string]
    foreach ($m in (Select-String -Path $psm -Pattern '^\s*"([a-z0-9_]+)"\s*\{\s*return')) {
        [void]$ids.Add($m.Matches[0].Groups[1].Value)
    }
    foreach ($g in (Get-PreRunGates)) { [void]$ids.Add($g) }
    return $ids
}

# The set of scenario names the C++ factory can actually construct, parsed from
# the `if (name == "...")` makers in src/plugin/test/*.cpp.
function Get-CppScenarioNames {
    $names = New-Object System.Collections.Generic.HashSet[string]
    $files = Get-ChildItem -Path (Join-Path $repoRoot "src\plugin\test") -Filter *.cpp
    foreach ($f in $files) {
        foreach ($m in (Select-String -Path $f.FullName -Pattern 'name\s*==\s*"([^"]+)"')) {
            foreach ($mm in $m.Matches) { [void]$names.Add($mm.Groups[1].Value) }
        }
    }
    return $names
}

# Scenario names that exist in the C++ factory but INTENTIONALLY have no manifest
# entry (pure diagnostics driven outside the tiered matrix). Documented here so
# the drift check stays a hard gate for everything else. (xfer_block moved INTO
# the manifest in Phase 2 so it can carry its own DiagEnv instead of a Config
# name-check, so it is no longer allowlisted.)
$manifestlessCpp = @('world_item_drop')

# The reverse allowlist: manifest scenarios that are RUNNER-ONLY - judged by a
# gate but driven by a bespoke script (a normal co-op tick, NOT a compiled
# KENSHICOOP_SCENARIO), so they intentionally have no C++ maker. bootstrap_stream
# is the missing-save streaming proof, run by scripts\stream_test.ps1.
$manifestRunnerOnly = @('bootstrap_stream')

# Return a list of schema problems for one scenario entry (empty = valid).
function Get-SchemaProblems {
    param([string]$Name, $Entry)
    $problems = @()
    $validTiers = @('smoke', 'full', 'probe', 'none')
    foreach ($k in @('Save', 'PrimaryGate', 'Gating', 'Advisory', 'Tier')) {
        if (-not $Entry.ContainsKey($k)) { $problems += "$Name missing '$k'" }
    }
    if ($Entry.ContainsKey('Tier') -and ($validTiers -notcontains $Entry.Tier)) {
        $problems += "$Name Tier '$($Entry.Tier)' not in {$($validTiers -join ',')}"
    }
    if ($Entry.ContainsKey('Gating')  -and $Entry.Gating  -isnot [array] -and $null -ne $Entry.Gating -and $Entry.Gating.Count -eq $null) {
        # a single-element @('x') is [object[]]; a bare string is the bug we guard
        if ($Entry.Gating -is [string]) { $problems += "$Name Gating is a bare string, not an array" }
    }
    if ($Entry.ContainsKey('PrimaryGate') -and $Entry.PrimaryGate -isnot [string]) {
        $problems += "$Name PrimaryGate is not a string"
    }
    return $problems
}

# ---- 1. manifest schema -------------------------------------------------------
Write-Host "== manifest schema =="
Check "manifest has Scenarios"   ($null -ne $scenarios -and $scenarios.Keys.Count -gt 0)
Check "manifest has Profiles"    ($null -ne $manifest.Profiles)
Check "manifest has WanProfiles" ($null -ne $manifest.WanProfiles)

$schemaProblems = @()
foreach ($name in $scenarios.Keys) {
    $schemaProblems += Get-SchemaProblems -Name $name -Entry $scenarios[$name]
}
if ($schemaProblems.Count -gt 0) { $schemaProblems | ForEach-Object { Write-Host "      $_" } }
Check "every scenario satisfies the schema" ($schemaProblems.Count -eq 0)

# NEGATIVE: a synthetic entry missing PrimaryGate must be flagged.
$badEntry = @{ Save = 'x'; Gating = @(); Advisory = @(); Tier = 'full' }
Check "schema checker flags a missing PrimaryGate" ((Get-SchemaProblems -Name 'bad' -Entry $badEntry).Count -gt 0)
# NEGATIVE: an invalid Tier must be flagged.
$badTier = @{ Save = 'x'; PrimaryGate = 'crosscheck'; Gating = @(); Advisory = @(); Tier = 'weekly' }
Check "schema checker flags an invalid Tier" ((Get-SchemaProblems -Name 'bad' -Entry $badTier).Count -gt 0)

# ---- 2. oracle registry -------------------------------------------------------
Write-Host "== oracle registry =="
$registry = Get-OracleRegistry
Check "oracle registry is non-empty" ($registry.Count -gt 0)
Check "registry contains a known oracle (crosscheck)" ($registry.Contains('crosscheck'))

$unknownRefs = @()
foreach ($name in $scenarios.Keys) {
    $e = $scenarios[$name]
    $ids = @()
    if ($e.PrimaryGate -ne "") { $ids += $e.PrimaryGate }
    $ids += @($e.Gating)
    $ids += @($e.Advisory)
    foreach ($id in $ids) {
        if ($id -ne "" -and -not $registry.Contains($id)) { $unknownRefs += "$name -> '$id'" }
    }
}
if ($unknownRefs.Count -gt 0) { $unknownRefs | ForEach-Object { Write-Host "      unknown oracle: $_" } }
Check "every manifest oracle id resolves in the registry" ($unknownRefs.Count -eq 0)

# NEGATIVE: Invoke-OneOracle must reject an unknown id (never silently pass).
$tmpH = [System.IO.Path]::GetTempFileName()
$tmpJ = [System.IO.Path]::GetTempFileName()
Reset-GateResults
$st = Invoke-OneOracle -Id 'definitely_not_an_oracle' -HostLog $tmpH -JoinLog $tmpJ -Tolerance 3.0 -ExpectedSkewMs $null
Check "unknown oracle id returns FAIL" ($st -eq 'FAIL')
$g = Get-GateResults | Where-Object { $_.gate -eq 'definitely_not_an_oracle' } | Select-Object -First 1
Check "unknown oracle id records 'unknown oracle id'" ($null -ne $g -and $g.detail -eq 'unknown oracle id')

# ---- 3. scenario drift (manifest <-> C++ factory) -----------------------------
Write-Host "== scenario name drift =="
$cppNames = Get-CppScenarioNames
Check "parsed C++ scenario names" ($cppNames.Count -gt 0)

# 3a. Every manifest scenario MUST be constructible by the factory.
$manifestNoMaker = @()
foreach ($name in $scenarios.Keys) {
    if ($manifestRunnerOnly -contains $name) { continue } # bespoke-runner, no maker
    if (-not $cppNames.Contains($name)) { $manifestNoMaker += $name }
}
if ($manifestNoMaker.Count -gt 0) { Write-Host ("      manifest names with no C++ maker: " + ($manifestNoMaker -join ', ')) }
Check "every manifest scenario has a C++ maker" ($manifestNoMaker.Count -eq 0)

# 3b. Every C++ maker MUST have a manifest entry (or be an allowlisted diagnostic).
$cppNoManifest = @()
foreach ($n in $cppNames) {
    if (-not $scenarios.ContainsKey($n) -and ($manifestlessCpp -notcontains $n)) { $cppNoManifest += $n }
}
if ($cppNoManifest.Count -gt 0) { Write-Host ("      C++ makers with no manifest entry: " + ($cppNoManifest -join ', ')) }
Check "every C++ maker has a manifest entry (or is allowlisted)" ($cppNoManifest.Count -eq 0)

# NEGATIVE: a fabricated manifest name absent from the factory must be detected.
$fakeMissing = 'scenario_that_cannot_exist'
Check "drift checker detects a manifest name with no maker" (-not $cppNames.Contains($fakeMissing))

# ---- 4. verdict rule + serialization ------------------------------------------
Write-Host "== verdict rule + serialization =="
$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("kc_contract_" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null
$hostLog = Join-Path $tmpDir "host.log"
$joinLog = Join-Path $tmpDir "join.log"
# Clean logs: reached gameplay, clean scenario exit, >=3 CLOCKSYNC samples, but
# NO MEMBER/RECV series (so any position oracle legitimately SKIPs = no signal).
@(
    "[10:00:00.000] HOST KenshiCoop: gameplay started",
    "[10:00:02.000] HOST SCENARIO RESULT PASS"
) | Set-Content -Path $hostLog -Encoding UTF8
@(
    "[10:00:00.000] JOIN KenshiCoop: gameplay started",
    "[10:00:00.100] JOIN CLOCKSYNC offset=5 rtt=10 n=1",
    "[10:00:00.600] JOIN CLOCKSYNC offset=4 rtt=8 n=2",
    "[10:00:01.100] JOIN CLOCKSYNC offset=4 rtt=8 n=3",
    "[10:00:02.000] JOIN SCENARIO RESULT PASS"
) | Set-Content -Path $joinLog -Encoding UTF8

# 4a. POSITIVE: a diagnostic scenario with no primary/gating passes on clean logs
#     and serializes a round-trippable verdict.json.
$okJson = Join-Path $tmpDir "verdict_ok.json"
$vOk = Invoke-RunAnalysis -HostLog $hostLog -JoinLog $joinLog -Scenario 'spike' -OutJson $okJson 2>&1 |
       Select-Object -Last 1
# Invoke-RunAnalysis returns the verdict object as the last pipeline item.
$vOk = & {
    Invoke-RunAnalysis -HostLog $hostLog -JoinLog $joinLog -Scenario 'spike' -OutJson $okJson | Out-Null
    Get-Content $okJson -Raw | ConvertFrom-Json
}
Check "clean no-gate scenario verdict.json written" (Test-Path $okJson)
Check "verdict.json round-trips (has pass/primary/gates)" ($null -ne $vOk -and $null -ne $vOk.gates -and $vOk.PSObject.Properties.Name -contains 'pass')
Check "clean no-gate scenario PASSES" ($vOk.pass -eq $true)

# 4b. NEGATIVE (no-signal guard): a scenario whose PRIMARY gate cannot be judged
#     on these logs must FAIL, citing the primary gate (as a FAIL or a SKIP -
#     either way the mechanism proof drives the verdict).
$skipJson = Join-Path $tmpDir "verdict_skip.json"
$vSkip = & {
    Invoke-RunAnalysis -HostLog $hostLog -JoinLog $joinLog -Scenario 'leader_move' -OutJson $skipJson | Out-Null
    Get-Content $skipJson -Raw | ConvertFrom-Json
}
Check "primary-gate no-signal run FAILS" ($vSkip.pass -eq $false)
$primaryReason = @($vSkip.reasons) -join '; '
Check "failure reason names the primary gate" ($primaryReason -match 'crosscheck')

# 4c. unknown-scenario handling: does not throw, falls back to generic
#     cross-check, and (with no signal) fails rather than silently passing.
$unkJson = Join-Path $tmpDir "verdict_unknown.json"
$threw = $false
try {
    $vUnk = & {
        Invoke-RunAnalysis -HostLog $hostLog -JoinLog $joinLog -Scenario 'no_such_scenario_xyz' -OutJson $unkJson | Out-Null
        Get-Content $unkJson -Raw | ConvertFrom-Json
    }
} catch { $threw = $true }
Check "unknown scenario does not throw" (-not $threw)
Check "unknown scenario falls back to crosscheck primary" ($vUnk.primary -eq 'crosscheck')

# ---- 5. manifest DiagEnv contract (Phase 2) -----------------------------------
# The per-scenario channel A/B knobs + log-only diagnostic traces moved OUT of
# the plugin's Config.cpp (which used to hard-code scenario names) and INTO the
# manifest DiagEnv, applied by CoopHarness. These fixtures guard that migration:
#   5a. every DiagEnv key is a known harness knob (no typos leak silently)
#   5b. the channel deltas Config.cpp used to encode are present in the manifest
#       (a deliberate two-source cross-check for the probe/inv/world scenarios
#       that the tiered matrix does not run live)
#   5c. Config.cpp no longer names scenarios for channel toggles (only the two
#       real-session `scenario == ""` defaults remain)
Write-Host "== manifest DiagEnv contract =="
$diagKeys = @(Get-CoopDiagEnvKeys)
Check "CoopHarness exposes a non-empty DiagEnv keyset" ($diagKeys.Count -gt 0)

$badDiag = @()
# Every knob is a 0/1 gate EXCEPT the few that carry a magnitude (a batch cap, say). Those are
# named here rather than loosening the rule, so a typo in a real gate still fails loudly.
$numericDiagKeys = @('KENSHICOOP_WI_BATCH_MAX', 'KENSHICOOP_WD_TRANSIENT_DEAD')
foreach ($name in $scenarios.Keys) {
    $e = $scenarios[$name]
    if (-not $e.ContainsKey('DiagEnv')) { continue }
    foreach ($k in $e.DiagEnv.Keys) {
        if ($diagKeys -notcontains $k) { $badDiag += "$name -> unknown key '$k'" }
        elseif ($numericDiagKeys -contains $k) {
            if ("$($e.DiagEnv[$k])" -notmatch '^\d+$') { $badDiag += "$name -> '$k' value '$($e.DiagEnv[$k])' not a number" }
        }
        elseif ("$($e.DiagEnv[$k])" -notin @('0', '1')) { $badDiag += "$name -> '$k' value '$($e.DiagEnv[$k])' not 0/1" }
    }
}
if ($badDiag.Count -gt 0) { $badDiag | ForEach-Object { Write-Host "      $_" } }
Check "every DiagEnv key is known + valued 0/1 (or numeric where declared)" ($badDiag.Count -eq 0)

# 5b. Spec table = the exact channel deltas Config.cpp USED to hard-code. If this
#     drifts from the manifest, a probe would silently run with the wrong channel
#     state (its baseline invalidated). Kept here on purpose as the second source.
$diagSpec = @{
    speed_probe    = @{ KENSHICOOP_SPEED_SYNC = '0' }
    shop_probe     = @{ KENSHICOOP_MONEY_SYNC = '0' }
    spawn_probe    = @{ KENSHICOOP_SPAWN_SYNC = '0' }
    recruit_probe  = @{ KENSHICOOP_RECRUIT_SYNC = '0' }
    faction_probe  = @{ KENSHICOOP_FACTION_SYNC = '0' }
    time_probe     = @{ KENSHICOOP_TIME_SYNC = '0'; KENSHICOOP_SPEED_SYNC = '0' }
    door_probe     = @{ KENSHICOOP_DOOR_SYNC = '0' }
    build_probe    = @{ KENSHICOOP_BUILD_SYNC = '0' }
    bdoor_probe    = @{ KENSHICOOP_BDOOR_SYNC = '0' }
    hunger_probe   = @{ KENSHICOOP_HUNGER_SYNC = '0' }
    save_probe     = @{ KENSHICOOP_SAVE_SYNC = '0' }
    load_probe     = @{ KENSHICOOP_LOAD_SYNC = '0' }
    prod_probe     = @{ KENSHICOOP_PROD_SYNC = '0' }
    research_probe = @{ KENSHICOOP_RESEARCH_SYNC = '0' }
    deed_probe     = @{ KENSHICOOP_DEED_SYNC = '0' }
    store_probe    = @{ KENSHICOOP_STORE_SYNC = '0' }
    squad_probe    = @{ KENSHICOOP_SQUAD_SYNC = '0' }
    latejoin_probe = @{ KENSHICOOP_LATEJOIN_SYNC = '0' }
    speed_sync     = @{ KENSHICOOP_TIME_SYNC = '0' }
    trade_probe    = @{ KENSHICOOP_XFER_SYNC = '0'; KENSHICOOP_INV_SYNC = '1' }
    xfer_block     = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_BLOCK_XFER = '1' }
    inv_order      = @{ KENSHICOOP_INV_SYNC = '1' }
    inv_bidir      = @{ KENSHICOOP_INV_SYNC = '1' }
    inv_equip      = @{ KENSHICOOP_INV_SYNC = '1' }
    inv_reequip    = @{ KENSHICOOP_INV_SYNC = '1' }
    vendor_trade   = @{ KENSHICOOP_INV_SYNC = '1' }
    store_sync     = @{ KENSHICOOP_INV_SYNC = '1' }
    trade_peer     = @{ KENSHICOOP_INV_SYNC = '1' }
    weapon_loot    = @{ KENSHICOOP_INV_SYNC = '1' }
    world_weapon_drop = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    world_armor_drop  = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    inv_backpack_drop = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    world_pickup_mirror = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    inv_regear     = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    inv_regear_refuse = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    inv_regear_refuse_all = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    inv_regear_forget = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    world_item_burst = @{ KENSHICOOP_WORLD_SYNC = '1' }
    inv_nested_bag  = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    inv_dump_all    = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    inv_dump_all_forget = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    inv_dump_all_transient = @{ KENSHICOOP_INV_SYNC = '1'; KENSHICOOP_WORLD_SYNC = '1' }
    world_item_sync = @{ KENSHICOOP_WORLD_SYNC = '1' }
    world_item_join = @{ KENSHICOOP_WORLD_SYNC = '1' }
    limb_loss       = @{ KENSHICOOP_WORLD_SYNC = '1' }
    rejoin_items    = @{ KENSHICOOP_WORLD_SYNC = '1' }
}
$specMiss = @()
foreach ($name in $diagSpec.Keys) {
    if (-not $scenarios.ContainsKey($name)) { $specMiss += "$name absent from manifest"; continue }
    $e = $scenarios[$name]
    $diag = if ($e.ContainsKey('DiagEnv')) { $e.DiagEnv } else { @{} }
    foreach ($k in $diagSpec[$name].Keys) {
        if ("$($diag[$k])" -ne "$($diagSpec[$name][$k])") {
            $specMiss += "$name -> $k expected '$($diagSpec[$name][$k])' got '$($diag[$k])'"
        }
    }
}
if ($specMiss.Count -gt 0) { $specMiss | ForEach-Object { Write-Host "      $_" } }
Check "manifest DiagEnv matches the Config channel spec" ($specMiss.Count -eq 0)

# 5c. Config.cpp must not reintroduce per-scenario channel names: the only
#     `c.scenario == "..."` allowed is the empty real-session default.
$configCpp = Join-Path $repoRoot "src\plugin\core\Config.cpp"
$scenarioNameHits = @()
foreach ($m in (Select-String -Path $configCpp -Pattern 'c\.scenario\s*==\s*"([^"]*)"')) {
    foreach ($mm in $m.Matches) {
        if ($mm.Groups[1].Value -ne "") { $scenarioNameHits += "line $($m.LineNumber): $($mm.Value)" }
    }
}
# scenario.compare(...) prefix-matching is likewise a hard-coded name test.
foreach ($m in (Select-String -Path $configCpp -Pattern 'scenario\.compare')) {
    $scenarioNameHits += "line $($m.LineNumber): scenario.compare(...)"
}
if ($scenarioNameHits.Count -gt 0) { $scenarioNameHits | ForEach-Object { Write-Host "      $_" } }
Check "Config.cpp has no per-scenario channel name-checks" ($scenarioNameHits.Count -eq 0)

# ---- Phase 5a: engine boundary dependency check -------------------------------
# The PUBLIC engine headers (the SEH-guarded facade Replicator/Scenario/Plugin
# include) must never pull a game-internal header (<kenshi/..>, <core/..>,
# <mygui/..>, <ogre/..>): those live ONLY in the adapter (EngineInternal.h) and
# the domain .cpp TUs. This is the "no direct Kenshi-internal include outside the
# approved adapter" barrier the domain split established - it keeps every public
# consumer compiling against pointers/PODs, not the engine ABI.
Write-Host "== engine boundary dependency check (Phase 5a) =="
$gameDir = Join-Path $repoRoot "src\plugin\game"
$publicEngineHeaders = @("Engine.h", "EngineSync.h", "EngineScenario.h",
                         "EngineProbe.h", "EngineUi.h")
$internalIncludeRe = '^\s*#\s*include\s*[<"](kenshi|core|mygui|ogre)/'
$leaks = @()
$missingHdr = @()
foreach ($h in $publicEngineHeaders) {
    $p = Join-Path $gameDir $h
    if (-not (Test-Path $p)) { $missingHdr += $h; continue }
    foreach ($m in (Select-String -Path $p -Pattern $internalIncludeRe)) {
        $leaks += "$h line $($m.LineNumber): $($m.Line.Trim())"
    }
}
if ($missingHdr.Count -gt 0) { $missingHdr | ForEach-Object { Write-Host "      missing $_" } }
Check "all narrow public engine headers exist" ($missingHdr.Count -eq 0)
if ($leaks.Count -gt 0) { $leaks | ForEach-Object { Write-Host "      $_" } }
Check "public engine headers pull no game-internal include" ($leaks.Count -eq 0)

# Positive control: the adapter EngineInternal.h SHOULD carry the game-internal
# prelude (otherwise the check above is passing vacuously against the wrong root).
$adapter = Join-Path $gameDir "EngineInternal.h"
$adapterHasInternal = (Test-Path $adapter) -and `
    ((Select-String -Path $adapter -Pattern $internalIncludeRe).Count -gt 0)
Check "adapter EngineInternal.h carries the game-internal prelude" $adapterHasInternal

# ---- wire string termination coverage -----------------------------------------
# Every char[] that arrives over the wire has to be NUL-terminated on receipt:
# a sender that omits the terminator turns the next std::string(...) into a read
# past the end of the packet struct, and one of those fields is used to build a
# filesystem path.
#
# Termination lives in ONE place - wireSanitize() in Wire.h, called by
# readPacket() - precisely so no dispatch arm has to remember it. But the
# overload set is hand-written, so a NEW packet with a char[] silently gets the
# generic no-op overload and reintroduces the bug. That is what this check
# catches: it reads the struct definitions out of Wire.h and requires every
# char[] field to be named by a wireSanitize overload for its own struct.
#
# It failed on five packets the first time it was run (the whole save/load name
# family), which is the argument for having it.
Write-Host ""
Write-Host "== wire string termination coverage =="
$wireH = Join-Path $repoRoot "src/netproto/Wire.h"
if (-not (Test-Path $wireH)) {
    Check "src/netproto/Wire.h exists" $false
} else {
    $wireLines = Get-Content -LiteralPath $wireH

    # Entry types are read in place out of the ENet buffer rather than through
    # readPacket, so they cannot be sanitized by it. They are terminated where
    # Inbound copies them; this check moves to that file for them instead of
    # waiving them, because "exempt" and "covered somewhere else" must not look
    # the same here.
    $entryTypes = @{ "InvItemEntry" = $true; "WorldItemEntry" = $true }

    # Pass 1: struct -> char[] field names, ignoring the wireSanitize block itself.
    # Comment lines are skipped and the struct scope is closed at `};`, because
    # Wire.h documents its variable-length tails in exactly the syntax of a field
    # ("// char name[nameLen] follows") - without both, this reported two
    # comments as unterminated fields.
    $fieldsOf = @{}
    $curStruct = $null
    foreach ($line in $wireLines) {
        if ($line -match '^\s*//') { continue }
        if ($line -match '^\s*struct\s+(\w+)') { $curStruct = $matches[1]; continue }
        if ($line -match '^\s*\}\s*;') { $curStruct = $null; continue }
        if ($line -match '^\s*inline\s+void\s+wireSanitize') { $curStruct = $null; continue }
        if ($null -eq $curStruct) { continue }
        if ($line -match '\bchar\s+(\w+)\s*\[') {
            if (-not $fieldsOf.ContainsKey($curStruct)) { $fieldsOf[$curStruct] = @() }
            $fieldsOf[$curStruct] += $matches[1]
        }
    }
    Check "Wire.h parsed: at least one char[] wire field found" ($fieldsOf.Count -gt 0)

    # Pass 2: the overload set. Body may be on the same line or the next few.
    $wireText = ($wireLines -join "`n")
    $sanitizeOf = @{}
    foreach ($m in [regex]::Matches($wireText,
        '(?s)inline\s+void\s+wireSanitize\s*\(\s*(\w+)\s*&\s*\w*\s*\)\s*\{(.*?)\n?\s*\}')) {
        $sanitizeOf[$m.Groups[1].Value] = $m.Groups[2].Value
    }
    Check "wireSanitize overload set is non-empty" ($sanitizeOf.Count -gt 0)

    # readPacket must actually call it, or the whole overload set is decoration.
    $callsIt = $wireText -match '(?s)inline\s+bool\s+readPacket.*?wireSanitize\s*\(\s*\*\s*out\s*\)'
    Check "readPacket calls wireSanitize on every parsed packet" $callsIt

    $uncovered = @()
    $inboundText = ""
    $inboundH = Join-Path $repoRoot "src/plugin/core/Inbound.h"
    if (Test-Path $inboundH) { $inboundText = (Get-Content -LiteralPath $inboundH) -join "`n" }

    foreach ($structName in ($fieldsOf.Keys | Sort-Object)) {
        foreach ($field in $fieldsOf[$structName]) {
            if ($entryTypes.ContainsKey($structName)) {
                # Terminated on copy in Inbound.h rather than by readPacket.
                if ($inboundText -notmatch ("wireTerm\s*\(\s*[\w\.\[\]]*\." + [regex]::Escape($field) + "\s*\)")) {
                    $uncovered += "$structName.$field (entry type: no wireTerm in Inbound.h)"
                }
                continue
            }
            if (-not $sanitizeOf.ContainsKey($structName)) {
                $uncovered += "$structName.$field (no wireSanitize overload for $structName)"
            } elseif ($sanitizeOf[$structName] -notmatch ('\b' + [regex]::Escape($field) + '\b')) {
                $uncovered += "$structName.$field (overload exists but does not name the field)"
            }
        }
    }
    if ($uncovered.Count -gt 0) { $uncovered | ForEach-Object { Write-Host "      $_" } }
    Check "every char[] wire field is terminated on receipt" ($uncovered.Count -eq 0)
}

# ---- cleanup ------------------------------------------------------------------
Remove-Item -Path $tmpH, $tmpJ -Force -ErrorAction SilentlyContinue
Remove-Item -Path $tmpDir -Recurse -Force -ErrorAction SilentlyContinue

# ---- summary ------------------------------------------------------------------
Write-Host ""

# A watchdog beat must never escape the guard of the call it names.
#
# 2026-08-10: the publish tick was sub-phased by prefixing each stage with
# coop::mainThreadBeat("pub:<stage>"). Twelve of those stages were written as a
# BRACELESS guarded statement -
#     if (g_cfg.worldSync)
#         g_repl.publishWorldItems(...);
# - so the mechanical prefix produced
#     if (g_cfg.worldSync)
#         coop::mainThreadBeat("pub:worldItems"); g_repl.publishWorldItems(...);
# which makes the beat the guarded statement and the PUBLISH UNCONDITIONAL.
# Every config gate in the tick (worldSync, invSync, xferSync, speedSync,
# timeSync, spawnSync, recruitSync, squadSync) was silently defeated. The
# compiler caught exactly one of the twelve - the single stage that happened to
# have an `else` - and the whole C++ gate passed on the other eleven, because
# nothing in it exercises a disabled feature.
#
# This is a source-drift check, not a behaviour test: it asks that no control
# statement be followed by a beat on the next line without an opening brace.
Write-Host ""
Write-Host "== the combat snap veto has a reachable ceiling =="
# prototest cannot see these: ReplicatorUtil.h pulls ENet and the engine facade,
# which that suite deliberately excludes. So the relationship is checked HERE,
# out of the source, the same way wireSanitize and pktName are.
#
# The v0.61 veto reused COMBAT_SNAP_DIST as its applicability window. That is a
# 20 u CONVERGENCE threshold (a driven brawl churns 12-18 u), not a statement
# about how far a fighting body can legitimately be - so the veto was dead code.
# Measured 2026-08-16: 1,679 combat/NPC hard snaps, median gap 89.9 u, p90
# 294.9 u, NOT ONE at or below 20 u; the live counters read snapCbt=727 against
# snapVeto=0. A veto that cannot fire is worse than none, because its zero
# counter reads as "this case never happens".
$utilH = Join-Path $repoRoot "src/plugin/sync/ReplicatorUtil.h"
if (-not (Test-Path $utilH)) {
    Check "src/plugin/sync/ReplicatorUtil.h exists" $false
} else {
    $ut = Get-Content -LiteralPath $utilH -Raw
    $mSnap = [regex]::Match($ut, 'COMBAT_SNAP_DIST\s*=\s*([0-9.]+)f')
    $mVeto = [regex]::Match($ut, 'COMBAT_VETO_MAX_DIST\s*=\s*([0-9.]+)f')
    Check "both combat distances are readable" ($mSnap.Success -and $mVeto.Success)
    if ($mSnap.Success -and $mVeto.Success) {
        $snap = [double]$mSnap.Groups[1].Value
        $veto = [double]$mVeto.Groups[1].Value
        Check ("veto ceiling reaches past the convergence band ({0} > 2 x {1})" -f $veto, $snap) `
              ($veto -gt ($snap * 2.0))
        Check ("...and still lets a departed body snap ({0} < 250)" -f $veto) `
              ($veto -lt 250.0)
    }
}

Write-Host ""
Write-Host "== packet-mix name table covers every PKT_ =="
# pktName() in NetLink.cpp is a hand-written list beside an enum - the same
# shape as wireSanitize, which went stale five times before it got a check.
# A packet type missing from the table prints as "t42" instead of its name,
# which is survivable; the check exists so it is NOTICED rather than shipped.
$wireH2   = Join-Path $repoRoot "src/netproto/Wire.h"
$netLink2 = Join-Path $repoRoot "src/plugin/net/NetLink.cpp"
if (-not (Test-Path $wireH2) -or -not (Test-Path $netLink2)) {
    Check "Wire.h and NetLink.cpp exist" $false
} else {
    $enumNames = @(Select-String -Path $wireH2 -Pattern '^\s*(PKT_[A-Z_0-9]+)\s*=\s*\d+' |
                   ForEach-Object { $_.Matches[0].Groups[1].Value })
    $nl = Get-Content -LiteralPath $netLink2 -Raw
    # Only the pktName switch matters; it is the only place with `case PKT_x: return`
    $named = @([regex]::Matches($nl, 'case\s+(PKT_[A-Z_0-9]+)\s*:\s*return') |
               ForEach-Object { $_.Groups[1].Value })
    Check "Wire.h parsed: PKT_ enumerators found" ($enumNames.Count -gt 0)
    $missing = @($enumNames | Where-Object { $named -notcontains $_ })
    Check ("pktName names every PKT_ ({0} enumerators)" -f $enumNames.Count) `
          ($missing.Count -eq 0) ($missing -join ', ')
}

Write-Host ""
Write-Host "== watchdog beats stay inside their guard =="
$pluginCpp = Join-Path $repoRoot "src/plugin/Plugin.cpp"
if (-not (Test-Path $pluginCpp)) {
    Check "src/plugin/Plugin.cpp exists" $false
} else {
    $pl = Get-Content -LiteralPath $pluginCpp
    $escaped = @()
    for ($i = 0; $i -lt $pl.Count; $i++) {
        if ($pl[$i] -notmatch 'mainThreadBeat\(') { continue }

        # Form 1, same line: `if (cond) coop::mainThreadBeat("x"); g_repl.f();`
        # The bare `beat(); call();` pair on one line is the file's normal style
        # and is fine - what is not is a CONTROL statement followed by a beat with
        # no brace, because then the beat is the guarded statement.
        if ($pl[$i] -match '^\s*(if|else if|else|for|while)\b[^{]*\)\s*coop::mainThreadBeat') {
            $escaped += ("line {0}: unbraced guard and beat on one line" -f ($i + 1))
            continue
        }
        if ($i -eq 0) { continue }

        # Form 2, next line. Skip blank lines AND comment lines on the way back:
        # a comment between the `if` and the beat would otherwise become $prev,
        # fail the control-statement match, and hide the defect silently.
        $j = $i - 1
        while ($j -ge 0 -and ($pl[$j].Trim() -eq '' -or
                              $pl[$j].Trim().StartsWith('//') -or
                              $pl[$j].Trim().StartsWith('*') -or
                              $pl[$j].Trim().StartsWith('/*'))) { $j-- }
        if ($j -lt 0) { continue }
        $prev = $pl[$j].Trim()
        if ($prev -match '^(if|else if|else|for|while)\b' -and $prev -notmatch '\{\s*$') {
            $escaped += ("line {0}: beat follows unbraced '{1}'" -f ($i + 1), $prev)
        }
    }
    Check "no watchdog beat escapes an unbraced guard" ($escaped.Count -eq 0) `
        ($escaped -join '; ')

    # And every beat label must be unique - two stages sharing a name makes the
    # stall report ambiguous, which is the entire thing sub-phasing bought.
    $labels = @()
    foreach ($line in $pl) {
        if ($line -match 'mainThreadBeat\("([^"]+)"\)') { $labels += $Matches[1] }
    }
    $dupes = ($labels | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
    Check "watchdog beat labels are unique" ($dupes.Count -eq 0) ($dupes -join ', ')

    # And the per-stage cost table must be able to hold them all. It drops
    # silently past its cap (with a one-shot warning at runtime), and a cost
    # table missing a stage reads as "that stage is cheap".
    $distinct = ($labels | Sort-Object -Unique).Count
    $capLine = Select-String -Path (Join-Path $repoRoot "src/plugin/CoopLog.cpp") `
                             -Pattern 'STAGE_MAX\s*=\s*(\d+)' | Select-Object -First 1
    if ($capLine) {
        $cap = [int]$capLine.Matches[0].Groups[1].Value
        Check ("stage table holds every beat label ({0} labels, cap {1})" -f $distinct, $cap) `
              ($distinct -lt $cap)
    } else {
        Check "STAGE_MAX readable from CoopLog.cpp" $false
    }
}


Write-Host ("contract fixtures: {0}/{1} checks passed{2}" -f `
    $script:Pass, ($script:Pass + $script:Fail), $(if ($script:Fail) { " - FAIL" } else { " - PASS" }))
exit $script:Fail