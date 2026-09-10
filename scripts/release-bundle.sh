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
#   1. **The asset list is a contract, not a directory listing.** The
#      names below are spelled out and every one of them must be present;
#      a build tree that grew a file, or lost one, stops the release here
#      rather than shipping a set the consumer's lockfile does not
#      describe. Copying a build directory wholesale would have
#      published `smoke.mjs`, `drive.mjs` and whatever else a preset
#      happens to leave there — the wasm tree carries five such files
#      today.
#   2. **The ABI's version travels with the bytes.** A consumer decides
#      whether it speaks a bundle *before* instantiating it, and it can
#      only do that from a file: manifest.json carries the ABI's own
#      major/minor (`AF_ABI_VERSION_*` in core/include/amberfolio/abi.h)
#      and the module's whole export list, both read out of the tree here
#      rather than spelled a second time. #211 is the consumer asking for
#      exactly this, and the reason they are read rather than repeated is
#      that a manifest disagreeing with the module it describes is worse
#      than one that says nothing at all.
#
#      `exportsDigest` is the same export list again, sorted and hashed,
#      so a consumer can pin one value against a whole surface rather than
#      trusting that major/minor were bumped correctly for it (#375: they
#      were not, the one time a name was renamed and not just added).
#
#      Which is also why a tree that declares no ABI version gets a
#      manifest with no `abi` key rather than a refusal. `release.yml`
#      runs *this* script against the tree of an older tag — current
#      tool, historical content — and v0.1.0 and v0.2.0 predate the
#      declaration. Saying nothing about a contract that did not exist
#      yet is the honest answer; inventing 1.0 for them would not be.
#   3. **`sourceCommit` is a commit and nothing else.** It is what the
#      hosting page turns into a `tree/<sha>` link, and that link is how
#      AGPL-3.0-only §13's offer is discharged for someone running the
#      program over a network. A tag can be moved and an abbreviated sha
#      goes ambiguous the day the repository grows into the collision, so
#      the full forty characters are checked for here — including that the
#      object really is a commit, because `refs/tags/v0.2.0` names an
#      *annotated tag object* whose own sha is forty hex characters that
#      look exactly like a commit and resolve to nothing.
#   4. **The OCR engine travels as one asset, or not at all.** The engine
#      `page/journal.mjs` is written against is a fact about the bundle
#      rather than a choice a site makes, so a consumer pinning a tag has
#      to be able to get it (#287). It rides as a single tarball with a
#      single hash, described under `engine` in the manifest; a build tree
#      that has not fetched one stages without it and says so. `ENGINE_DIR`
#      below has the argument.
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

# The files hosts/web/CMakeLists.txt emits for a page to run the
# module: the Emscripten pair (OUTPUT_NAME amberfolio, SUFFIX .mjs,
# EXPORT_ES6, MODULARIZE), the seven page scripts it copies beside them
# and the edition table they read.
# Deliberately *not* index.html — the release is the emulator, and the
# page around it belongs to whoever is hosting it.
#
# **sidecars.mjs joined the list in M6 (#385)**, for persist.mjs's reason
# exactly: app.mjs imports it by name, so a consumer serving the released
# app.mjs without it gets a 404 for the file holding the one question this
# page asks a player.
#
# **persist.mjs joined the list in M6 (#381)**, for the reason journal.mjs
# did: app.mjs imports it by name, and a consumer serving the released
# app.mjs without it gets a 404 for the file that holds everything the
# page remembers between visits. It is additive for a consumer that reads
# manifest.json and a fetch to add for one that spells the names out.
#
# **journal.mjs joined the list in M5-C1 (#229), and was owed before
# that.** app.mjs has imported it since #174 and it was never staged, so
# a consumer serving the released app.mjs got a 404 for a file it asks
# for by name; #229 makes host.mjs import it too, which turns a latent
# gap in the page into a hard requirement for anything loading the
# façade. Adding a file is additive for a consumer that reads
# manifest.json (which lists them) and a fetch it has to add for one that
# spells the names out.
#
# **These names are keys in somebody else's lockfile.** Renaming one is a
# breaking change for every consumer pinning it, not a refactor, and the
# same goes for the hash spelling below: a consumer normalises hex to SRI,
# so a change there would be invisible to the code and merely confusing to
# a person reading SHA256SUMS and manifest.json side by side. The list is
# asserted by name and in order in test-release-bundle.sh, so it cannot
# move quietly — changing it means editing a test that says why.
#
# **editions.mjs and editions.json joined the list in M6 (#207).** The
# JSON is the table of what each edition this build recognises is made
# of, and it is an asset rather than an ABI call for one reason: a page
# renders the checklist while the module is still downloading, and a site
# generates its edition roster without running anything at all. The same
# file is what hosts/common compiles its C++ arrays out of, so a browser
# and a desktop cannot disagree about it. editions.mjs is the reader over
# it, and app.mjs imports it by name — the #229 lesson, applied before it
# could bite again.
BUNDLE=(
  amberfolio.wasm
  amberfolio.mjs
  host.mjs
  app.mjs
  audio-worklet.mjs
  picker.mjs
  journal.mjs
  persist.mjs
  sidecars.mjs
  editions.mjs
  editions.json
)

# Shipped beside the bundle so a consumer can render the notices without
# cloning. LICENSE is the outbound licence the released bytes are under
# and the one the §13 offer is made under; LICENSES/ carries the inbound
# terms (CONTRIBUTING.md) and is taken as it stands, so a file added there
# is in the next release with no edit here.
NOTICES=(LICENSE NOTICE.md)
NOTICES_DIR=LICENSES

# The browser's OCR engine, when the build tree has one (#287).
#
# `page/journal.mjs` refuses a CDN by design and reads one library
# version's output shape, so *which* tesseract.js a page serves is a fact
# about the bundle and not a decision a site gets to make. The engine is
# about 32 MB of third-party binaries and is never committed
# (`scripts/fetch-ocr-engine.py` carries the argument): CI fetches it into
# the build tree beside the module and this attaches it, so a site pinning
# a tag can serve it from its own origin without choosing a version. The
# release carried the page's files and no engine, and a site could decode
# every entry of a recognised journal and recognise none.
#
# **One tarball rather than an asset per file**, because the directory is
# one thing: `loadEngine()` wants a directory, and attaching its files
# separately would make every consumer reassemble it. Its entries are
# `vendor/tesseract/...`, so unpacking it where the bundle is served puts
# the engine exactly where `ENGINE_URL` looks — no rewriting of paths, no
# choice to get wrong.
#
# **A missing engine is not an error**, for the `abi` key's reason:
# `release.yml` stages the tree of an older tag, and somebody staging by
# hand has fetched nothing. That gets a manifest with no `engine` key and
# a line on stderr. What *is* an error is a directory that is there and is
# not one — no `version.txt`, so nothing can say which library it is, or
# no `tesseract.min.js`, the one file `journal.mjs` asks for by name.
ENGINE_DIR=vendor/tesseract
ENGINE_ASSET=vendor-tesseract.tar.gz
ENGINE_VERSION_FILE=version.txt
ENGINE_ENTRY=tesseract.min.js

die() {
  echo "release-bundle: $*" >&2
  exit 1
}

# sha256sum is coreutils and shasum is what macOS has; staging a release
# from a laptop should not depend on which.
if command -v sha256sum >/dev/null 2>&1; then
  sha256_of() { sha256sum "$1" | cut -d' ' -f1; }
  sha256_of_stdin() { sha256sum | cut -d' ' -f1; }
elif command -v shasum >/dev/null 2>&1; then
  sha256_of() { shasum -a 256 "$1" | cut -d' ' -f1; }
  sha256_of_stdin() { shasum -a 256 | cut -d' ' -f1; }
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

# The ABI's own version, which is not the one above and does not move with
# it: abi.h states the rule at the point of definition. Read here so a
# manifest cannot drift from the header its module was compiled against.
abi_header=$repo_root/core/include/amberfolio/abi.h
read_abi_number() { # read_abi_number <macro-name>
  sed -n "s/^#define[[:space:]]\{1,\}$1[[:space:]]\{1,\}\([0-9]\{1,\}\)u\{0,1\}[[:space:]]*$/\1/p" \
    "$abi_header" | head -n 1
}
abi_major=$(read_abi_number AF_ABI_VERSION_MAJOR)
abi_minor=$(read_abi_number AF_ABI_VERSION_MINOR)
if [ -z "$abi_major" ] || [ -z "$abi_minor" ]; then
  # Two quite different trees arrive here and only one of them is a bug.
  # A header that *talks about* AF_ABI_VERSION without defining it is a
  # botched edit, and shipping a manifest that quietly drops the key
  # would hide it. A header that has never heard of it is simply older
  # than the declaration, which is a tree this script is expected to be
  # pointed at.
  if grep -q 'AF_ABI_VERSION' "$abi_header"; then
    die "$abi_header mentions AF_ABI_VERSION but defines no" \
      "AF_ABI_VERSION_MAJOR/MINOR: manifest.json states the ABI a" \
      "consumer has to speak, and a guessed one would be worse than a" \
      "release that does not state it at all"
  fi
  abi_major=
  abi_minor=
  echo "release-bundle: $abi_header declares no ABI version, so" \
    "manifest.json will carry no \"abi\" key — which is what a consumer" \
    "reads as \"older than the declaration\" rather than as 1.0" >&2
fi

# The module's export list, taken from the one place that decides it — the
# CMake variable joined into -sEXPORTED_FUNCTIONS. Anything else here
# would be a second spelling; hosts/web/tests/smoke.mjs already keeps the
# only other one, and that one asserts *presence*, so it cannot see an
# export that was added.
#
# Comments and blank lines live inside that set() block, and its closing
# paren sits on the last name rather than on a line of its own.
web_cmake=$repo_root/hosts/web/CMakeLists.txt
exports=$(awk '
  /^set\(_amberfolio_web_export_names/ { inside = 1; next }
  inside {
    line = $0
    sub(/#.*/, "", line)
    closing = (index(line, ")") > 0)
    gsub(/[()]/, " ", line)
    count = split(line, token, /[ \t]+/)
    for (i = 1; i <= count; i++) {
      if (token[i] ~ /^_[A-Za-z0-9_]+$/) print token[i]
    }
    if (closing) exit
  }
' "$web_cmake")
if [ -z "$exports" ]; then
  die "no export names found in $web_cmake: manifest.json states what the" \
    "module exports, and an empty list would be a lie about the bundle"
fi
# A canary against a parser that half works: a list with no af_version in
# it is not this module's list, however many names came back.
if ! printf '%s\n' "$exports" | grep -qx '_af_version'; then
  die "the export list read from $web_cmake has no _af_version in it, so" \
    "it is not the module's list — the set() block's shape has moved"
fi

# A digest of the export list a loader can pin instead of trusting that
# `abi_major`/`abi_minor` above were bumped correctly (#375: v0.5.0 was
# not, when a rename should have moved `abi_major`). Sorted so a harmless
# reordering of the CMake block does not move it, and generated from this
# same `$exports` rather than a second list kept in step by hand — an add,
# a remove or a rename of any name changes it, which a bare export *count*
# cannot see.
exports_digest=$(printf '%s\n' "$exports" | LC_ALL=C sort | sha256_of_stdin)

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

# The engine, if this build tree fetched one (#287). Staged after the
# notices so that a refusal here still leaves nothing behind: the trap is
# armed until the very end.
engine_files=
engine_version=
if [ -d "$built/$ENGINE_DIR" ]; then
  if [ ! -f "$built/$ENGINE_DIR/$ENGINE_VERSION_FILE" ]; then
    die "$built/$ENGINE_DIR has no $ENGINE_VERSION_FILE: the manifest states" \
      "which tesseract.js these bytes are, and an engine nothing can name is" \
      "one a lockfile cannot pin. Every fetch writes that file beside what it" \
      "fetched (scripts/fetch-ocr-engine.py)"
  fi
  if [ ! -f "$built/$ENGINE_DIR/$ENGINE_ENTRY" ]; then
    die "$built/$ENGINE_DIR has no $ENGINE_ENTRY, which is the one file" \
      "page/journal.mjs asks for by name: whatever is in that directory, it" \
      "is not the engine this bundle is written against"
  fi
  engine_version=$(tr -d '\r\n' <"$built/$ENGINE_DIR/$ENGINE_VERSION_FILE")
  # A version goes into JSON unescaped, so it has to be a version. A file
  # holding a quote or a backslash would produce a manifest that parses as
  # nothing at all, which is the one failure a consumer meets with nothing
  # pointing back at here.
  case $engine_version in
    "" | *[!A-Za-z0-9.+-]*)
      die "$built/$ENGINE_DIR/$ENGINE_VERSION_FILE does not hold a version" \
        "(got '$engine_version'): manifest.json states it, unescaped"
      ;;
  esac
  # Reproducible where tar can be: names sorted, no timestamps, no owner,
  # and gzip told not to record its own. Two people staging one tree then
  # get one digest, which is the property a lockfile wants. bsdtar has
  # none of those flags and still makes a correct tarball — the digest is
  # a fact about the bytes attached either way, and only its equality
  # across machines is lost.
  case $(tar --version 2>/dev/null) in
    *"GNU tar"*)
      tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
        --format=ustar -cf - -C "$built" "$ENGINE_DIR" |
        gzip -n -9 >"$out/$ENGINE_ASSET"
      ;;
    *)
      tar -cf - -C "$built" "$ENGINE_DIR" | gzip -n -9 >"$out/$ENGINE_ASSET"
      ;;
  esac
  staged+=("$ENGINE_ASSET")
  # Every file inside it, by the path it unpacks to, so a consumer can
  # check what it downloaded *and* what it ended up serving.
  engine_files=$(cd "$built" && find "$ENGINE_DIR" -type f | LC_ALL=C sort)
  if [ -z "$engine_files" ]; then
    die "$built/$ENGINE_DIR holds no files"
  fi
else
  echo "release-bundle: $built/$ENGINE_DIR is missing, so this release" \
    "carries no OCR engine and manifest.json will have no \"engine\" key —" \
    "which a consumer reads as \"fetch one yourself\" (#287)" >&2
fi

# Hex, plain `sha256sum` output, sorted by name: the copy a person checks
# by hand. It covers everything attached, notices included.
: >"$out/SHA256SUMS"
while IFS= read -r name; do
  printf '%s  %s\n' "$(sha256_of "$out/$name")" "$name" >>"$out/SHA256SUMS"
done < <(printf '%s\n' "${staged[@]}" | sort)

# manifest.json is the machine-readable one, and it describes the
# *bundle* — the files a consumer pins. The notices are in SHA256SUMS
# beside it; nothing downstream pins a licence text, and listing them here
# would invite something to.
{
  printf '{\n'
  printf '  "version": "%s",\n' "$version"
  # Absent, rather than guessed, on a tree from before the declaration.
  if [ -n "$abi_major" ]; then
    printf '  "abi": { "major": %s, "minor": %s },\n' "$abi_major" "$abi_minor"
  fi
  printf '  "exportsDigest": "sha256:%s",\n' "$exports_digest"
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
  printf '  ],\n'
  # The OCR engine, when there is one (#287): one asset, its digest, and
  # the digest of every file it unpacks to, so a consumer can verify the
  # download and again what it is about to serve. `unpacksTo` is where
  # those paths land, which is where `journal.mjs`'s `ENGINE_URL` looks.
  if [ -n "$engine_files" ]; then
    printf '  "engine": {\n'
    printf '    "name": "%s",\n' "$ENGINE_ASSET"
    printf '    "sha256": "%s",\n' "$(sha256_of "$out/$ENGINE_ASSET")"
    printf '    "size": %s,\n' "$(wc -c <"$out/$ENGINE_ASSET" | tr -d ' ')"
    printf '    "library": "tesseract.js",\n'
    printf '    "version": "%s",\n' "$engine_version"
    printf '    "unpacksTo": "%s/",\n' "$ENGINE_DIR"
    printf '    "files": [\n'
    engine_first=1
    while IFS= read -r name; do
      if [ "$engine_first" -eq 1 ]; then engine_first=0; else printf ',\n'; fi
      printf '      { "name": "%s", "sha256": "%s", "size": %s }' \
        "$name" "$(sha256_of "$built/$name")" \
        "$(wc -c <"$built/$name" | tr -d ' ')"
    done <<ENGINE_FILES
$engine_files
ENGINE_FILES
    printf '\n    ]\n'
    printf '  },\n'
  fi
  # Every entry point the module exports, in the order the link line
  # takes them. `abi` above is what a loader compares; this is what it
  # prints when the comparison fails, and what a host checks a single
  # name against without instantiating anything.
  printf '  "exports": [\n'
  printf '%s\n' "$exports" | awk '
    NR > 1 { printf(",\n") }
    { printf("    \"%s\"", $0) }
    END { printf("\n") }'
  printf '  ]\n'
  printf '}\n'
} >"$out/manifest.json"

trap - EXIT
echo "release-bundle: $tag ($commit) staged in $out"
printf '  %s\n' "${staged[@]}" SHA256SUMS manifest.json
