#!/usr/bin/env bash
# Make the vendored KenshiLib headers compile under the Wine-driven v100
# toolchain. Two independent fixes, both idempotent -- rerun after any checkout
# or update of third_party/KenshiLib_deps.
#
# 1. `#pragma once` -> include guards.
#    MSVC dedups `#pragma once` by comparing the canonical path string of the
#    header. Under Wine the same file arrives spelled two ways -- one route
#    yields `Z:\...\kenshi\Building\Building.h`, another
#    `z:\...\kenshi/Building/Building.h` -- so the compiler treats it as two
#    distinct headers. Include guards key off a macro, so they are immune.
#
# 2. Duplicate `enum BuildingDesignation`.
#    KenshiLib defines it in BOTH kenshi/Platoon.h and kenshi/Building/Building.h
#    (introduced in KenshiLib v0.3.4). The two definitions are byte-identical, so
#    any TU that pulls in both fails with C2011 for no good reason. We wrap each
#    copy in a shared guard macro; first one included wins, and since they are
#    identical it does not matter which.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
INC="${1:-$REPO/third_party/KenshiLib_deps/KenshiLib/Include}"

[ -d "$INC" ] || { echo "no include tree at $INC" >&2; exit 1; }

# ---- 1. pragma once -> include guards ----------------------------------------
guarded=0
while IFS= read -r -d '' header; do
  grep -q '^[[:space:]]*#pragma once' "$header" || continue

  rel="${header#$INC/}"
  guard="KLGUARD_$(printf '%s' "$rel" | tr '/.-' '___' | tr '[:lower:]' '[:upper:]')"

  python3 - "$header" "$guard" <<'PY'
import sys
path, guard = sys.argv[1], sys.argv[2]
with open(path, 'r', encoding='utf-8', errors='surrogateescape') as fh:
    lines = fh.readlines()

out, replaced = [], False
for line in lines:
    if not replaced and line.strip() == '#pragma once':
        out.append(f'#ifndef {guard}\n#define {guard}\n')
        replaced = True
        continue
    out.append(line)

if replaced:
    if out and not out[-1].endswith('\n'):
        out.append('\n')
    out.append(f'#endif // {guard}\n')
    with open(path, 'w', encoding='utf-8', errors='surrogateescape') as fh:
        fh.writelines(out)
PY
  guarded=$((guarded + 1))
done < <(find "$INC" -type f \( -name '*.h' -o -name '*.hpp' \) -print0)

# ---- 2. de-duplicate enum BuildingDesignation --------------------------------
deduped=0
for rel in kenshi/Platoon.h kenshi/Building/Building.h; do
  f="$INC/$rel"
  [ -f "$f" ] || continue
  grep -q '^enum BuildingDesignation' "$f" || continue
  grep -q 'KLGUARD_ENUM_BUILDINGDESIGNATION' "$f" && continue

  python3 - "$f" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='surrogateescape') as fh:
    lines = fh.readlines()

out, i, n = [], 0, len(lines)
while i < n:
    if lines[i].startswith('enum BuildingDesignation'):
        out.append('#ifndef KLGUARD_ENUM_BUILDINGDESIGNATION\n')
        out.append('#define KLGUARD_ENUM_BUILDINGDESIGNATION\n')
        while i < n:
            out.append(lines[i])
            if lines[i].lstrip().startswith('};'):
                i += 1
                break
            i += 1
        out.append('#endif // KLGUARD_ENUM_BUILDINGDESIGNATION\n')
        continue
    out.append(lines[i])
    i += 1

with open(path, 'w', encoding='utf-8', errors='surrogateescape') as fh:
    fh.writelines(out)
PY
  deduped=$((deduped + 1))
done

echo "vendored headers: $guarded guard conversion(s), $deduped enum dedup(s)"
