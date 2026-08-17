#!/usr/bin/env bash
# publish_release.sh <tag> <notes-file> [title]
#
# The whole release tail in one place: stage the per-OS archives from the kit,
# push the tag, publish the GitHub release with all five assets. Exists because
# `gh release create` resolves asset paths against the CWD and the staging step
# naturally leaves a shell inside dist/stage-<tag>/ - that exact mistake shipped
# a "no matches found for `dist/...`" failure on five consecutive releases
# (v0.67, v0.68, v0.69, v0.70, v0.71/72). Doctrine: do the safety thing where
# everything funnels, not at each call site.
#
# Prereq: scripts/linux/make_kit.sh <tag> has already produced
# dist/KenshiCoop-kit-<tag>.zip (this script refuses to run without it).
set -euo pipefail

TAG="${1:?usage: publish_release.sh <tag> <notes-file> [title]}"
NOTES="${2:?usage: publish_release.sh <tag> <notes-file> [title]}"
TITLE="${3:-$TAG}"

# Anchor on the repo root regardless of where we were invoked from.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

KIT="dist/KenshiCoop-kit-$TAG.zip"
[[ -f "$KIT" ]] || { echo "ERROR: $KIT missing - run scripts/linux/make_kit.sh $TAG first" >&2; exit 1; }
[[ -f "$NOTES" ]] || { echo "ERROR: notes file $NOTES missing" >&2; exit 1; }

STAGE="dist/stage-$TAG"
rm -rf "$STAGE"; mkdir -p "$STAGE"
( cd "$STAGE" && unzip -q "../../$KIT" \
  && tar czf "../KenshiCoop-$TAG-linux.tar.gz" KenshiCoop \
  && zip -qr "../KenshiCoop-$TAG-windows.zip" KenshiCoop )

# make_kit.sh already created the local tag (the build stamp names it).
git push -q origin "$TAG"

# SemVer pre-release tags (v1.0.0-beta.1, -rc.2, -alpha.3) get GitHub's
# pre-release flag automatically. The updaters deliberately do NOT use
# /releases/latest (it excludes pre-releases), so flagging costs nothing
# there and keeps the releases page honest about beta status.
PRE=()
case "$TAG" in
  *-alpha.*|*-beta.*|*-rc.*) PRE=(--prerelease) ;;
esac

gh release create "$TAG" \
  "$KIT" \
  "dist/KenshiCoop-$TAG-linux.tar.gz" \
  "dist/KenshiCoop-$TAG-windows.zip" \
  scripts/update-kenshicoop.ps1 \
  scripts/linux/update-kenshicoop.sh \
  --repo CaptainVirgil/KenshiCoop --target main --verify-tag \
  "${PRE[@]}" \
  --title "$TITLE" --notes-file "$NOTES"

echo "published: https://github.com/CaptainVirgil/KenshiCoop/releases/tag/$TAG"
