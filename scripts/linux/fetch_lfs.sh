#!/usr/bin/env bash
# Resolve Git LFS pointer files in a checkout without git-lfs installed.
#
# Arch ships git-lfs only as a package install (sudo), and the KenshiLib deps
# repo keeps its .lib files and boost.zip in LFS. This walks the working tree,
# finds pointer files, and pulls the real objects through the LFS batch API.
#
#   Usage: scripts/linux/fetch_lfs.sh <repo-dir> [remote-url]
set -euo pipefail

REPO_DIR="${1:?usage: fetch_lfs.sh <repo-dir> [remote-url]}"
REMOTE="${2:-$(git -C "$REPO_DIR" remote get-url origin)}"
REMOTE="${REMOTE%.git}.git"

mapfile -t POINTERS < <(
  grep -rl --binary-files=without-match '^version https://git-lfs\.github\.com/spec/v1' "$REPO_DIR" 2>/dev/null || true
)

[ "${#POINTERS[@]}" -gt 0 ] || { echo "no LFS pointers under $REPO_DIR"; exit 0; }
echo "resolving ${#POINTERS[@]} LFS object(s) from $REMOTE"

for f in "${POINTERS[@]}"; do
  oid="$(grep -oP 'oid sha256:\K\S+' "$f")"
  size="$(grep -oP '^size \K\d+' "$f")"
  [ -n "$oid" ] && [ -n "$size" ] || { echo "  ! unparsable pointer: $f" >&2; continue; }

  url="$(curl -sf -X POST \
      -H 'Accept: application/vnd.git-lfs+json' \
      -H 'Content-Type: application/vnd.git-lfs+json' \
      -d "{\"operation\":\"download\",\"transfers\":[\"basic\"],\"objects\":[{\"oid\":\"$oid\",\"size\":$size}]}" \
      "$REMOTE/info/lfs/objects/batch" |
    python3 -c 'import json,sys; print(json.load(sys.stdin)["objects"][0]["actions"]["download"]["href"])')"

  curl -sL -o "$f.lfs" "$url"
  got="$(sha256sum "$f.lfs" | cut -d' ' -f1)"
  if [ "$got" != "$oid" ]; then
    rm -f "$f.lfs"
    echo "  ! checksum mismatch for $f (want $oid, got $got)" >&2
    exit 1
  fi
  mv "$f.lfs" "$f"
  echo "  ok $(basename "$f") ($(numfmt --to=iec "$size"))"
done
