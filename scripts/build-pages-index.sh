#!/usr/bin/env bash
# scripts/build-pages-index.sh — emit a PEP 503 simple/ tree pointing
# at the wheels of the **latest** GitHub Release.
#
# Resolves the "latest" release via `gh release view` (which uses
# GitHub's own latest pointer — most recent non-prerelease, non-draft
# tag), pulls its SHA256SUMS asset (uploaded by the release workflow),
# and emits:
#
#   $OUT_DIR/index.html             root listing (one <a> per package)
#   $OUT_DIR/<pkg>/index.html       per-package listing for this release
#
# Per-wheel hrefs point at the absolute GitHub-Releases asset URL with
# a `#sha256=` fragment, so pip can verify integrity and the Pages site
# only has to serve a few KB of HTML — the wheels themselves live in
# Releases.
#
# Indexing only the latest release is deliberate. pip selects by
# PEP 440 version regardless of which release a wheel came from, so a
# multi-release index just exposes superseded versions for no
# functional gain. When a new release is cut, the workflow re-runs
# this script and the index advances; older releases stay reachable
# directly via their GitHub Release page but are not in the index.
#
# Inputs (env):
#   GH_REPO   "<owner>/<repo>"   defaults to $GITHUB_REPOSITORY
#   OUT_DIR   output path        defaults to ./_site/simple
#
# Requires: gh, jq, awk, mktemp.

set -euo pipefail

GH_REPO="${GH_REPO:-${GITHUB_REPOSITORY:-}}"
OUT_DIR="${OUT_DIR:-_site/simple}"

if [[ -z "$GH_REPO" ]]; then
  echo "ERROR: GH_REPO (or GITHUB_REPOSITORY) must be set" >&2
  exit 1
fi

echo "building Pages index for $GH_REPO → $OUT_DIR"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Resolve the latest release. `gh release view` with no tag returns
# whichever release GitHub considers "latest" — most recent
# non-prerelease, non-draft. Marking a release as pre-release is the
# escape hatch for cutting a build without advancing the index.
TAG="$(gh release view --repo "$GH_REPO" --json tagName --jq .tagName 2>/dev/null || true)"

if [[ -z "$TAG" ]]; then
  echo "no published release in $GH_REPO — emitting empty index"
  TAG=""
fi

# entries.tsv: one row per wheel — pkg<TAB>filename<TAB>sha256
ENTRIES="$TMP/entries.tsv"
: > "$ENTRIES"

if [[ -n "$TAG" ]]; then
  echo "  using release $TAG"
  # SHA256SUMS is uploaded by the release workflow alongside the
  # wheels. If it's missing, the release predates the publish pipeline
  # and there's nothing safe to publish from it.
  if ! gh release download "$TAG" --repo "$GH_REPO" \
        --pattern 'SHA256SUMS' --dir "$TMP" 2>/dev/null; then
    echo "ERROR: $TAG has no SHA256SUMS asset" >&2
    exit 1
  fi

  while IFS= read -r line; do
    # `sha256sum` output: "<hex>  <filename>". Skip blank lines.
    [[ -z "$line" ]] && continue
    sha="${line%% *}"
    fname="${line##* }"
    [[ "$fname" == *.whl ]] || continue
    pkg="${fname%%-*}"
    printf '%s\t%s\t%s\n' "$pkg" "$fname" "$sha" >> "$ENTRIES"
  done < "$TMP/SHA256SUMS"
fi

# Unique package list. The first dash-separated field of a wheel
# filename is already PEP 503 normalized for our packages (cffi,
# cryptography). If you add a package whose name uses '_' or mixed
# case, normalize at the `printf "%s\t..."` line above instead.
PKGS=$(awk -F'\t' '{print $1}' "$ENTRIES" | sort -u)

# ---- root index.html ----
{
  cat <<'EOF'
<!DOCTYPE html>
<html><head>
  <meta name="pypi:repository-version" content="1.0">
  <title>WASI Python wheel index</title>
</head><body>
EOF
  for pkg in $PKGS; do
    printf '  <a href="%s/">%s</a><br>\n' "$pkg" "$pkg"
  done
  echo "</body></html>"
} > "$OUT_DIR/index.html"

# ---- per-package index.html ----
for pkg in $PKGS; do
  mkdir -p "$OUT_DIR/$pkg"
  {
    cat <<EOF
<!DOCTYPE html>
<html><head>
  <meta name="pypi:repository-version" content="1.0">
  <title>$pkg</title>
</head><body>
EOF
    awk -F'\t' -v p="$pkg" '$1 == p' "$ENTRIES" \
      | while IFS=$'\t' read -r _ fname sha; do
          url="https://github.com/$GH_REPO/releases/download/$TAG/$fname"
          printf '  <a href="%s#sha256=%s">%s</a><br>\n' "$url" "$sha" "$fname"
        done
    echo "</body></html>"
  } > "$OUT_DIR/$pkg/index.html"
done

echo "✓ wrote $OUT_DIR (release $TAG, $(echo "$PKGS" | wc -w) packages, $(wc -l < "$ENTRIES") wheels)"
