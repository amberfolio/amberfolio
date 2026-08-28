#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# Self-test for scripts/release-bundle.sh, in the spirit of
# test-guards.sh: the green path proves the shape, and the red paths
# prove the refusals, because a staging script that cannot refuse is a
# staging script that will one day publish the wrong bytes.
#
# What it is protecting is downstream and out of sight. The site that
# hosts the wasm build pins a sha256 per asset and *refuses to build* on a
# mismatch, so a manifest that disagrees with the attached bytes is a red
# build in another repository with no way to tell from here which of the
# two is wrong. And `sourceCommit` is load-bearing in law rather than in
# code: it is the link that discharges AGPL-3.0-only §13 for a user
# running the program over a network, and nothing downstream can tell a
# commit sha from an annotated tag object's sha by looking at it.
#
# Run after editing release-bundle.sh.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0

expect() { # expect <description> <wanted-exit: 0|nonzero> <command...>
  local desc="$1" want="$2"
  shift 2
  local got=0
  "$@" >/dev/null 2>&1 || got=$?
  if { [ "$want" = 0 ] && [ "$got" -eq 0 ]; } ||
    { [ "$want" != 0 ] && [ "$got" -ne 0 ]; }; then
    echo "ok: $desc"
  else
    echo "FAIL: $desc (wanted exit $want, got $got)"
    fail=1
  fi
}

check() { # check <description> <command...>
  local desc="$1"
  shift
  if "$@"; then
    echo "ok: $desc"
  else
    echo "FAIL: $desc"
    fail=1
  fi
}

if ! command -v python3 >/dev/null 2>&1; then
  echo "test-release-bundle: python3 is needed to read the manifest back" >&2
  exit 127
fi

# A throwaway repository with just the pieces release-bundle.sh reads: a
# version, the notices, and a tag. Deliberately a real git repository —
# the annotated-tag trap this script exists to pin only exists in one.
mkrepo() { # mkrepo <name> <version> -> prints repo path
  local d="$tmp/$1" version="$2"
  mkdir -p "$d/scripts" "$d/LICENSES"
  cp "$here/release-bundle.sh" "$d/scripts/"
  cat >"$d/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.25)
project(amberfolio
  VERSION $version
  LANGUAGES CXX)
CMAKE
  echo "the outbound licence" >"$d/LICENSE"
  echo "the notices" >"$d/NOTICE.md"
  echo "the inbound licence" >"$d/LICENSES/Apache-2.0.txt"
  git -C "$d" init -q -b main
  git -C "$d" config user.email test@example.com
  git -C "$d" config user.name "Release Test"
  git -C "$d" config core.autocrlf false
  git -C "$d" add -A
  git -C "$d" commit -q -s -m base
  git -C "$d" tag -a -m "milestone" "v$version"
  echo "$d"
}

# What the wasm preset leaves in hosts/web/Release: the six that ship, and
# the apparatus that must not. drive.mjs and smoke.mjs are here because
# they are really there — a directory copy would have released both.
mkbuild() { # mkbuild <name> -> prints directory path
  local d="$tmp/$1"
  mkdir -p "$d"
  local f
  for f in amberfolio.wasm amberfolio.mjs host.mjs app.mjs \
    audio-worklet.mjs picker.mjs smoke.mjs drive.mjs boot.mjs index.html; do
    echo "contents of $f" >"$d/$f"
  done
  echo "$d"
}

repo=$(mkrepo green 0.2.0)
build=$(mkbuild green-build)
out=$tmp/green-out
expect "a complete build stages" 0 \
  bash "$repo/scripts/release-bundle.sh" "$build" "$out" v0.2.0

check "the six bundle files are attached" test \
  -f "$out/amberfolio.wasm" -a -f "$out/amberfolio.mjs" -a -f "$out/host.mjs" \
  -a -f "$out/app.mjs" -a -f "$out/audio-worklet.mjs" -a -f "$out/picker.mjs"
check "the notices are attached" test \
  -f "$out/LICENSE" -a -f "$out/NOTICE.md" -a -f "$out/Apache-2.0.txt"
check "SHA256SUMS and manifest.json are written" test \
  -f "$out/SHA256SUMS" -a -f "$out/manifest.json"

# The whole reason the asset list is spelled out rather than globbed.
for stray in smoke.mjs drive.mjs boot.mjs index.html; do
  check "$stray is not released" test ! -e "$out/$stray"
done

# The hashes are of the bytes actually sitting there, not of anything the
# script remembered. sha256sum -c is the check a person would run.
if command -v sha256sum >/dev/null 2>&1; then
  check "SHA256SUMS verifies against the staged bytes" \
    bash -c "cd '$out' && sha256sum --quiet -c SHA256SUMS"
else
  check "SHA256SUMS verifies against the staged bytes" \
    bash -c "cd '$out' && shasum -a 256 -c SHA256SUMS >/dev/null"
fi

commit=$(git -C "$repo" rev-parse --verify "refs/tags/v0.2.0^{commit}")

# The manifest is read back the way the consumer reads it: as JSON, with
# every field it relies on asserted. A trailing comma or a bare newline
# would sail past any grep and stop a build downstream.
check "manifest.json says what the release is" python3 - "$out" "$commit" <<'PY'
import hashlib, json, os, sys

out, commit = sys.argv[1], sys.argv[2]
with open(os.path.join(out, "manifest.json"), "rb") as f:
    manifest = json.load(f)

expected = [
    "amberfolio.wasm", "amberfolio.mjs", "host.mjs",
    "app.mjs", "audio-worklet.mjs", "picker.mjs",
]
assert manifest["version"] == "0.2.0", manifest["version"]
assert manifest["sourceCommit"] == commit, manifest["sourceCommit"]
assert [f["name"] for f in manifest["files"]] == expected, manifest["files"]
for entry in manifest["files"]:
    blob = open(os.path.join(out, entry["name"]), "rb").read()
    assert entry["sha256"] == hashlib.sha256(blob).hexdigest(), entry
    assert entry["size"] == len(blob), entry
PY

# --- and now the refusals. -------------------------------------------

expect "an existing output directory is refused" 1 \
  bash "$repo/scripts/release-bundle.sh" "$build" "$out" v0.2.0

short=$(mkbuild short-build)
rm "$short/picker.mjs"
expect "a bundle file missing from the build is refused" 1 \
  bash "$repo/scripts/release-bundle.sh" "$short" "$tmp/short-out" v0.2.0
check "nothing is left behind by a refused staging" test \
  ! -e "$tmp/short-out/amberfolio.wasm"

expect "a tag that is not vMAJOR.MINOR.PATCH is refused" 1 \
  bash "$repo/scripts/release-bundle.sh" "$build" "$tmp/badtag-out" main

# CONTRIBUTING.md's rule, as a gate: the tag and project(VERSION ...) are
# the same string or the binary lies about which milestone it is.
expect "a tag that disagrees with project(VERSION ...) is refused" 1 \
  bash "$repo/scripts/release-bundle.sh" "$build" "$tmp/drift-out" v0.3.0

expect "an unknown tag is refused" 1 \
  bash "$repo/scripts/release-bundle.sh" "$build" "$tmp/notag-out" v9.9.9

# The trap this script mostly exists for. On a tag push GITHUB_SHA is the
# annotated tag object's own sha: forty lowercase hex characters that pass
# every format check downstream and resolve to no source tree at all.
tagobj=$(git -C "$repo" rev-parse --verify refs/tags/v0.2.0)
check "the tag object and its commit really are different shas" \
  test "$tagobj" != "$commit"
expect "an annotated tag object's sha is refused as sourceCommit" 1 \
  bash "$repo/scripts/release-bundle.sh" "$build" "$tmp/tagobj-out" v0.2.0 "$tagobj"

expect "an abbreviated sha is refused as sourceCommit" 1 \
  bash "$repo/scripts/release-bundle.sh" "$build" "$tmp/short-sha-out" v0.2.0 \
  "${commit:0:12}"

# A flat asset namespace has no directories in it, so LICENSES/NOTICE.md
# and NOTICE.md would be one name for two files. Refuse, never overwrite.
collide=$(mkrepo collide 0.2.0)
echo "a second file wanting the same name" >"$collide/LICENSES/NOTICE.md"
git -C "$collide" add -A
git -C "$collide" commit -q -s -m "collide"
expect "two files that would share one asset name are refused" 1 \
  bash "$collide/scripts/release-bundle.sh" "$build" "$tmp/collide-out" v0.2.0

if [ "$fail" -ne 0 ]; then
  echo "test-release-bundle: FAILED" >&2
  exit 1
fi
echo "test-release-bundle: OK"
