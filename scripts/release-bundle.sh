#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Stage the web host's build output as the asset set of a GitHub Release
# (issue #200), and state its hashes.
#
# The site that hosts the wasm build consumes a release as a *pinned build
# input*: it never contains emulator source, it records a tag and a sha256
# per asset in a lockfile, and its build refuses to start when the bytes
# it downloads do not hash to what it pinned. Two things follow, and they
# are what this script exists to hold.
#
#   1. **The asset list is a contract, not a directory listing.** The six
#      names below are spelled out and every one of them must be present;
#      a build tree that grew a seventh file, or lost one, stops the
#      release here rather than shipping a set the consumer's lockfile
#      does not describe. Copying a build directory wholesale would have
#      published `smoke.mjs`, `drive.mjs` and whatever else a preset
#      happens to leave there — the wasm tree carries five such files
#      today.
#   2. **`sourceCommit` is a commit and nothing else.** It is what the
#      hosting page turns into a `tree/<sha>` link, and that link is how
#      AGPL-3.0-only §13's offer is discharged for someone running the
#      program over a network. A tag can be moved and an abbreviated sha
#      goes ambiguous the day the repository grows into the collision, so
#      the full forty characters are checked for here — including that the
#      object really is a commit, because `refs/tags/v0.2.0` names an
#      *annotated tag object* whose own sha is forty hex characters that
#      look exactly like a commit and resolve to nothing.
#
# The version is read from the top-level CMakeLists.txt and asserted equal
# to the tag, which makes CONTRIBUTING.md's "its name matches
# project(VERSION ...) exactly" a gate instead of a habit. `af_version`
# reports that string across the C ABI, so a release named otherwise is a
# binary that lies about which milestone it is.
#
# Usage: scripts/release-bundle.sh <built-web-host-dir> <out-dir> <tag> [<commit>]
#
# With no <commit>, the tag is resolved in the repository this script
# lives in. Pass one explicitly when staging outside a checkout.
#
# Self-tested by scripts/test-release-bundle.sh — run it after editing.
set -euo pipefail

# The six files hosts/web/CMakeLists.txt emits for a page to run the
# module: the Emscripten pair (OUTPUT_NAME amberfolio, SUFFIX .mjs,
# EXPORT_ES6, MODULARIZE) and the four page scripts it copies beside them.
# Deliberately *not* index.html — the release is the emulator, and the
# page around it belongs to whoever is hosting it.
#
# **These names are keys in somebody else's lockfile.** Renaming one is a
# breaking change for every consumer pinning it, not a refactor, and the
# same goes for the hash spelling below: a consumer normalises hex to SRI,
# so a change there would be invisible to the code and merely confusing to
# a person reading SHA256SUMS and manifest.json side by side. The list is
# asserted by name and in order in test-release-bundle.sh, so it cannot
# move quietly — changing it means editing a test that says why.
BUNDLE=(
  amberfolio.wasm
  amberfolio.mjs
  host.mjs
  app.mjs
  audio-worklet.mjs
  picker.mjs
)

# Shipped beside the bundle so a consumer can render the notices without
# cloning. LICENSE is the outbound licence the released bytes are under
# and the one the §13 offer is made under; LICENSES/ carries the inbound
# terms (CONTRIBUTING.md) and is taken as it stands, so a file added there
# is in the next release with no edit here.
NOTICES=(LICENSE NOTICE.md)
NOTICES_DIR=LICENSES

die() {
  echo "release-bundle: $*" >&2
  exit 1
}

# sha256sum is coreutils and shasum is what macOS has; staging a release
# from a laptop should not depend on which.
if command -v sha256sum >/dev/null 2>&1; then
  sha256_of() { sha256sum "$1" | cut -d' ' -f1; }
elif command -v shasum >/dev/null 2>&1; then
  sha256_of() { shasum -a 256 "$1" | cut -d' ' -f1; }
else
  die "no sha256sum and no shasum on this machine"
fi

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
  die "usage: $0 <built-web-host-dir> <out-dir> <tag> [<commit>]"
fi

built=$1
out=$2
tag=$3
commit=${4:-}

repo_root=$(cd "$(dirname "$0")/.." && pwd)

if [ ! -d "$built" ]; then
  die "$built is not a directory"
fi

# vMAJOR.MINOR.PATCH and nothing else. The release job is reachable by a
# hand-typed workflow input, so this is the one place that decides what a
# release may be called.
case $tag in
  v[0-9]*.[0-9]*.[0-9]*) ;;
  *) die "tag '$tag' is not vMAJOR.MINOR.PATCH" ;;
esac
version=${tag#v}

# project(amberfolio VERSION x.y.z ...) in the top-level CMakeLists.
declared=$(sed -n \
  's/^[[:space:]]*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\)[[:space:]]*$/\1/p' \
  "$repo_root/CMakeLists.txt" | head -n 1)
if [ -z "$declared" ]; then
  die "no project(VERSION ...) found in CMakeLists.txt"
fi
if [ "$declared" != "$version" ]; then
  die "tag $tag against project(VERSION $declared): CONTRIBUTING.md's" \
    "\"Releases and tags\" makes those the same string, and af_version" \
    "reports the CMake one across the C ABI"
fi

if [ -z "$commit" ]; then
  commit=$(git -C "$repo_root" rev-parse --verify "refs/tags/$tag^{commit}" \
    2>/dev/null) || die "tag $tag does not name a commit in $repo_root"
fi
if ! printf '%s' "$commit" | grep -Eq '^[0-9a-f]{40}$'; then
  die "sourceCommit '$commit' is not a full 40-character lowercase sha"
fi
# And that it names a commit rather than the annotated tag object that
# points at one. Only checkable inside a checkout that has the object;
# skipped, never guessed, outside one.
if git -C "$repo_root" cat-file -e "$commit" 2>/dev/null; then
  kind=$(git -C "$repo_root" cat-file -t "$commit")
  if [ "$kind" != commit ]; then
    die "sourceCommit $commit is a $kind object, not a commit." \
      "On a tag push GITHUB_SHA is the annotated tag's own sha;" \
      "peel it with: git rev-parse \"\$GITHUB_REF^{commit}\""
  fi
fi

# A fresh directory every time: a release is the set this run staged, not
# that set plus whatever a previous one left behind.
if [ -e "$out" ]; then
  die "$out already exists; stage into a fresh directory"
fi
mkdir -p "$out"
# And it exists only if this run finished. A refusal partway through would
# otherwise leave a directory holding some of a release, which is exactly
# the shape a later step would happily upload.
trap 'rm -rf "$out"' EXIT

for name in "${BUNDLE[@]}"; do
  if [ ! -f "$built/$name" ]; then
    die "$built/$name is missing: the release asset list and what the web" \
      "host builds have drifted apart"
  fi
  cp "$built/$name" "$out/$name"
done

staged=("${BUNDLE[@]}")

copy_notice() {
  local src=$1 name=$2
  if [ ! -f "$src" ]; then
    die "$src is missing"
  fi
  # Release assets are one flat namespace, so LICENSES/x.txt arrives as
  # x.txt. Refuse rather than overwrite if that ever collides.
  if [ -e "$out/$name" ]; then
    die "two different files would be released as '$name'"
  fi
  cp "$src" "$out/$name"
  staged+=("$name")
}

for name in "${NOTICES[@]}"; do
  copy_notice "$repo_root/$name" "$name"
done
while IFS= read -r path; do
  copy_notice "$path" "$(basename "$path")"
done < <(find "$repo_root/$NOTICES_DIR" -maxdepth 1 -type f | sort)

# Hex, plain `sha256sum` output, sorted by name: the copy a person checks
# by hand. It covers everything attached, notices included.
: >"$out/SHA256SUMS"
while IFS= read -r name; do
  printf '%s  %s\n' "$(sha256_of "$out/$name")" "$name" >>"$out/SHA256SUMS"
done < <(printf '%s\n' "${staged[@]}" | sort)

# manifest.json is the machine-readable one, and it describes the
# *bundle* — the six files a consumer pins. The notices are in SHA256SUMS
# beside it; nothing downstream pins a licence text, and listing them here
# would invite something to.
{
  printf '{\n'
  printf '  "version": "%s",\n' "$version"
  printf '  "sourceCommit": "%s",\n' "$commit"
  printf '  "files": [\n'
  last=$((${#BUNDLE[@]} - 1))
  for i in "${!BUNDLE[@]}"; do
    name=${BUNDLE[$i]}
    comma=,
    if [ "$i" -eq "$last" ]; then comma=; fi
    printf '    { "name": "%s", "sha256": "%s", "size": %s }%s\n' \
      "$name" "$(sha256_of "$out/$name")" \
      "$(wc -c <"$out/$name" | tr -d ' ')" "$comma"
  done
  printf '  ]\n'
  printf '}\n'
} >"$out/manifest.json"

trap - EXIT
echo "release-bundle: $tag ($commit) staged in $out"
printf '  %s\n' "${staged[@]}" SHA256SUMS manifest.json
