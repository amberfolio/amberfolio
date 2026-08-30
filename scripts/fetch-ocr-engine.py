#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
#
# Fetch the browser's OCR engine and put it beside the page (M5-E3, #174).
#
# The journal's ingestion needs an OCR engine. On the desktop that is the
# player's own installed Tesseract, run as a program and not linked
# (hosts/sdl/src/tesseract_ocr.h says why). In a browser it is
# tesseract.js — the same engine, compiled to wasm — and a browser cannot
# run a program, so the engine has to be *served*.
#
#     python3 scripts/fetch-ocr-engine.py --into build/wasm/hosts/web/Debug
#     python3 scripts/fetch-ocr-engine.py --print-plan
#
# What it puts there is `vendor/tesseract/` beside the module: the
# library, its worker, its wasm core, and one language's data.
# `hosts/web/page/journal.mjs` looks in exactly that place and nowhere
# else.
#
#                          WHY NOT A CDN
#
# #174 permits a runtime CDN fetch only if the deployed page says so, and
# a page that quietly pulled several megabytes of somebody else's
# JavaScript the moment a player picked a file would be doing something
# the player did not ask for and could not see. It would also make the
# engine a moving target: what a player got would be whatever the CDN was
# serving that day, and an OCR result nobody can reproduce is not much of
# a result. So the engine is fetched here, pinned, and served from the
# page's own origin.
#
#                         WHY NOT COMMITTED
#
# The same reason nothing else third-party is committed here
# (CONTRIBUTING.md, and the content guard would refuse it anyway — these
# are binaries, and several megabytes each). It goes into the build tree,
# which is ignored.
#
#                          THE PINNED DIGESTS
#
# This file may not *invent* a fingerprint: every one in this repository
# is a fact about a file somebody actually hashed. So it began as trust
# on first use — the first run writes `sha256sums.txt` beside what it
# fetched, every later run verifies against it and refuses on a mismatch
# — with a note saying a maintainer who had checked the digests could
# commit that file.
#
# Somebody has. `scripts/ocr-engine.sha256sums` is the record, and it is
# a fact rather than a guess: the same fifteen files were fetched twice,
# on different days, and the two digest lists were identical. `--digests`
# is what points a run at it, and the deploy pipeline passes it on every
# build, so the bytes a player is served are checked against a committed
# record rather than against whatever the registry answered that morning
# (`.github/workflows/ci.yml`).
#
# The versions are still the pin that decides *what* is fetched; the
# digests decide whether what arrived is what was expected. When
# `.tesseract-js-version` moves, this file is stale by design: the run
# fails, a maintainer looks at both, and `--force` records the new ones.
#
# The versions are `.tesseract-js-version` and, for the desktop engine a
# player installs themselves, `.tesseract-version` — the same shape
# `.emscripten-version` has.
#
# Licences: tesseract.js and tesseract.js-core are Apache-2.0, and the
# language data is Apache-2.0 as well. Compatible with our AGPL-3.0-only
# outbound licence (CONTRIBUTING.md); NOTICE.md records both.

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import shutil
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

REGISTRY = "https://registry.npmjs.org"
# tessdata_fast rather than tessdata_best: an order of magnitude smaller,
# and what tesseract.js itself ships against. Pinned to a tag for the same
# reason every other dependency is.
TESSDATA_TAG = "4.1.0"
TESSDATA_URL = (
    "https://github.com/tesseract-ocr/tessdata_fast/raw/{tag}/{lang}.traineddata"
)

DIGESTS = "sha256sums.txt"

# The committed record, checked when a run is not pointed at another one.
# A CI runner has no previous fetch to compare against, so without this
# every deploy would be trust on first use.
COMMITTED_DIGESTS = Path(__file__).resolve().parent / "ocr-engine.sha256sums"


def read_pin(name: str) -> str:
    text = (ROOT / name).read_text(encoding="utf-8").strip()
    if not text:
        raise SystemExit(f"{name} is empty")
    return text


def fetch(url: str) -> bytes:
    print(f"  fetching {url}")
    with urllib.request.urlopen(url) as response:  # noqa: S310 - a pinned https URL
        return response.read()


def members(tarball: bytes) -> dict[str, bytes]:
    """Every file in an npm tarball, keyed by its path under `package/`."""
    out: dict[str, bytes] = {}
    with tarfile.open(fileobj=io.BytesIO(tarball), mode="r:gz") as archive:
        for member in archive.getmembers():
            if not member.isfile():
                continue
            handle = archive.extractfile(member)
            if handle is None:
                continue
            name = member.name
            out[name[len("package/") :] if name.startswith("package/") else name] = (
                handle.read()
            )
    return out


def npm(package: str, version: str) -> dict[str, bytes]:
    # The registry's own tarball path. `@scope/name` becomes
    # `@scope/name/-/name-version.tgz`, which is why the leaf is split off.
    leaf = package.split("/")[-1]
    return members(fetch(f"{REGISTRY}/{package}/-/{leaf}-{version}.tgz"))


def collect(version: str, language: str) -> dict[str, bytes]:
    """Everything the page needs, keyed by its name under vendor/tesseract."""
    library = npm("tesseract.js", version)
    manifest = json.loads(library["package.json"])
    # The core's version comes out of what the library itself depends on,
    # rather than being a second thing to pin and get out of step. A
    # dependency range is stripped to its number: the library ships an
    # exact one, and anything looser is refused rather than resolved.
    wanted = manifest.get("dependencies", {}).get("tesseract.js-core", "")
    core_version = wanted.lstrip("^~=v ").strip()
    if not core_version or any(c in core_version for c in "<>|* "):
        raise SystemExit(
            f"tesseract.js {version} depends on tesseract.js-core '{wanted}', which is"
            " not one version; pin it here rather than resolving a range"
        )
    core = npm("tesseract.js-core", core_version)

    files: dict[str, bytes] = {}
    for source, name in (
        ("dist/tesseract.min.js", "tesseract.min.js"),
        ("dist/worker.min.js", "worker.min.js"),
    ):
        if source not in library:
            raise SystemExit(f"tesseract.js {version} has no {source}")
        files[name] = library[source]

    # The core comes in several builds (with and without SIMD, single and
    # multi-threaded); the loader picks by feature detection, so all of
    # what it may ask for is copied.
    wanted_core = [name for name in core if name.startswith("tesseract-core")]
    if not wanted_core:
        raise SystemExit(f"tesseract.js-core {core_version} has no core builds in it")
    for name in wanted_core:
        files[name] = core[name]

    data = fetch(TESSDATA_URL.format(tag=TESSDATA_TAG, lang=language))
    # tesseract.js asks for `<lang>.traineddata.gz` unless it is told
    # otherwise; gzipping here rather than turning that off keeps the
    # page's own configuration to the three paths.
    files[f"{language}.traineddata.gz"] = gzip.compress(data, mtime=0)

    print(f"  tesseract.js {version}, core {core_version}, tessdata {TESSDATA_TAG}")
    return files


def digests_of(files: dict[str, bytes]) -> str:
    lines = [
        f"{hashlib.sha256(body).hexdigest()}  {name}"
        for name, body in sorted(files.items())
    ]
    return "\n".join(lines) + "\n"


def main() -> int:
    version = read_pin(".tesseract-js-version")

    parser = argparse.ArgumentParser(
        description="Fetch the pinned tesseract.js and put it beside the web host."
    )
    parser.add_argument(
        "--into",
        type=Path,
        help="the directory the page is served from (the one holding amberfolio.mjs)",
    )
    parser.add_argument(
        "--language",
        default="eng",
        help="which language's data to fetch (default: eng)",
    )
    parser.add_argument(
        "--print-plan",
        action="store_true",
        help="say what would be fetched and from where, and fetch nothing",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="overwrite files whose digests do not match the recorded ones",
    )
    parser.add_argument(
        "--digests",
        type=Path,
        help="verify against this digest record instead of one left in the"
        " target directory (default: scripts/ocr-engine.sha256sums if it"
        " exists, else the target's own)",
    )
    args = parser.parse_args()

    if args.print_plan:
        print(f"tesseract.js {version} from {REGISTRY}")
        print(f"  its tesseract.js-core, at whichever version it depends on")
        print(f"  {TESSDATA_URL.format(tag=TESSDATA_TAG, lang=args.language)}")
        print("into <served directory>/vendor/tesseract/")
        return 0

    if args.into is None:
        parser.error("--into is required (or use --print-plan)")

    where = args.into / "vendor" / "tesseract"
    print(f"amberfolio: fetching the OCR engine into {where}")
    files = collect(version, args.language)
    sums = digests_of(files)

    # Three places a record may be, in the order they are believed: the one
    # a run was pointed at, the one committed beside this script, and the
    # one a previous fetch left in the target directory. The committed one
    # is what makes a CI runner - which has no previous fetch - check
    # anything at all.
    recorded = args.digests or COMMITTED_DIGESTS
    if not recorded.exists():
        recorded = where / DIGESTS
    if recorded.exists() and not args.force:
        # Strict about the digests, not about line endings - the same rule
        # `journal_store` learned the hard way. A record that has been
        # through an editor on Windows, or checked out with `core.autocrlf`
        # on, is still that record, and refusing it would be a correct
        # refusal and a useless one.
        was = recorded.read_text(encoding="utf-8").replace("\r\n", "\n")
        if was != sums:
            print(
                "amberfolio: what was fetched does not match the digests"
                f" recorded in {recorded}.\n"
                "  Nothing was written. Either the pin moved and the recorded"
                " digests are stale, or something served different bytes.\n"
                "  Look at both, then re-run with --force if the new ones are"
                " the ones you want.",
                file=sys.stderr,
            )
            return 1
    elif not recorded.exists():
        print(
            f"amberfolio: no digest record at {COMMITTED_DIGESTS}; what was"
            " fetched is trusted on first use and pinned afterwards.",
            file=sys.stderr,
        )

    if where.exists():
        shutil.rmtree(where)
    where.mkdir(parents=True)
    for name, body in sorted(files.items()):
        (where / name).write_bytes(body)
    recorded.write_text(sums, encoding="utf-8")

    total = sum(len(body) for body in files.values())
    print(f"amberfolio: {len(files)} files, {total // 1024} KiB, digests in {DIGESTS}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
