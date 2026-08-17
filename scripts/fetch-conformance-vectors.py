#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
#
# Fetch the CPU conformance oracle and condense it (issue #14).
#
# The oracle is SingleStepTests/8088 — MIT-licensed JSON vectors captured
# from real 8088 silicon. It is pinned to a commit here and never
# committed to this repository: it is 726 MB, every file is far over the
# content guard's 256 KiB cap, and vendoring a third-party corpus is not
# something this project does. It is fetched into a cache directory
# outside the source tree instead.
#
#     python3 scripts/fetch-conformance-vectors.py            # all 323
#     python3 scripts/fetch-conformance-vectors.py --stems 00 90 80.0
#     python3 scripts/fetch-conformance-vectors.py --print-dir
#     python3 scripts/fetch-conformance-vectors.py --print-key
#
# Re-entrant: a file already in the cache is left alone, so a second run
# downloads nothing. Set AMBERFOLIO_CONFORMANCE_VECTORS to put the cache
# somewhere else; the harness reads the same variable and computes the
# same default (tests/conformance/vectors.cpp — keep the two in step).
#
#                              CONDENSING
#
# Between 51% and 96% of each raw file is a per-cycle bus trace, and this
# emulator has no prefetch queue and does not count cycles (PLAN.md §3),
# so it consumes none of it. Each file is therefore rewritten on the way
# into the cache, keeping the initial and final architectural state and
# dropping the rest. Measured: 17-60% of the original size per file, and
# the set falls from 726 MB to roughly 200 MB — but the real win is the
# 8-10 GB of JSON the harness then never has to parse.
#
# One exception, and it is the reason this is not a pure delete: the port
# value an IN instruction reads exists *only* in the trace. So for the
# eight port opcodes the transactions are extracted from the cycles into
# a compact per-test "io" list before the trace is dropped.
#
# The condensed form is versioned by CONDENSER_VERSION, and the cache
# directory name carries both that and the suite commit — so changing
# either is a new cache, never a stale one silently reused.

import argparse
import gzip
import json
import os
import random
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

# The pin. Suite version 2.0.1; the v2 set, not v1 (superseded), not
# v2_binary (carries the cycles too, so no win) and not v2_undefined
# (out of M1 scope — see issue #35).
SUITE_REPO = "SingleStepTests/8088"
SUITE_SHA = "aea84484abc79d09639d855b7b0ab32bc9e4dbeb"
SUITE_SET = "v2"
BASE_URL = f"https://raw.githubusercontent.com/{SUITE_REPO}/{SUITE_SHA}/{SUITE_SET}"

# Bump when the condensed layout changes in any way the harness can see.
# It is half the cache key, so a bump invalidates every cached file
# rather than leaving the harness to read an older shape.
CONDENSER_VERSION = 1

# The opcodes whose tests touch the port bus, and the only ones whose
# cycle traces are mined before being dropped.
PORT_OPCODES = frozenset({"E4", "E5", "E6", "E7", "EC", "ED", "EE", "EF"})

MANIFEST = Path(__file__).resolve().parent.parent / "tests" / "conformance" / "vector-files.txt"

# Seconds to wait before each retry. Long enough that a rate limit
# actually expires rather than being walked into five times in a row.
RETRY_DELAYS = (5, 15, 30, 60, 60, 60)

# Downloading and condensing happen in the same worker, so a worker
# spends most of its time parsing rather than fetching and the requests
# spread themselves out. Four is enough to keep the CPUs busy and few
# enough not to look like a burst; --jobs overrides it.
DEFAULT_JOBS = 4

# Cycle-row fields, from the suite's README. Only these four are read.
CYCLE_PINS = 0  # bit 0 is ALE: the address latch is valid this cycle
CYCLE_ADDRESS = 1
CYCLE_IO_STATUS = 4  # "R--" / "-A-" / "-AW" / "---"
CYCLE_DATA = 6
CYCLE_BUS_STATUS = 7  # CODE / IOR / IOW / MEMR / MEMW / PASV / HALT / INTA


def cache_key():
    """The cache directory's name, and CI's actions/cache key."""
    return f"{SUITE_SET}-{SUITE_SHA[:12]}-c{CONDENSER_VERSION}"


def default_cache_dir():
    """Where condensed vectors live when nothing says otherwise.

    Kept identical, deliberately, in tests/conformance/vectors.cpp: the
    harness has to find what this wrote without being told.
    """
    override = os.environ.get("AMBERFOLIO_CONFORMANCE_VECTORS")
    if override:
        return Path(override)
    if sys.platform == "win32":
        root = os.environ.get("LOCALAPPDATA") or (Path.home() / "AppData" / "Local")
    else:
        root = os.environ.get("XDG_CACHE_HOME") or (Path.home() / ".cache")
    return Path(root) / "amberfolio" / "conformance" / cache_key()


def read_manifest():
    stems = []
    with MANIFEST.open(encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line and not line.startswith("#"):
                stems.append(line)
    if not stems:
        raise SystemExit(f"no vector stems in {MANIFEST}")
    return stems


def download(url, attempts=6):
    """GET, patiently.

    Three hundred and twenty-three requests in a row is enough for
    raw.githubusercontent.com to answer 429 Too Many Requests, and one
    flaky connection must not cost the other 322 files their run. So:
    back off properly, honour a Retry-After when the server sends one,
    and jitter so that four workers that were rate-limited together do
    not come back together.
    """
    last = None
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(url, timeout=180) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            last = error
            wait = error.headers.get("Retry-After") if error.headers else None
            delay = float(wait) if wait and wait.isdigit() else RETRY_DELAYS[attempt]
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            last = error
            delay = RETRY_DELAYS[attempt]
        time.sleep(delay + random.uniform(0, 2))
    raise RuntimeError(f"{url}: {last}")


def each_test(text):
    """Yield the objects of a top-level JSON array one at a time.

    json.load on the largest file would build a 120 MB document's worth
    of Python objects at once; walking it with raw_decode keeps one test
    alive at a time and the peak down to the text itself.
    """
    decoder = json.JSONDecoder()
    index = text.index("[") + 1
    limit = len(text)
    while True:
        while index < limit and text[index] in " \t\r\n,":
            index += 1
        if index >= limit or text[index] == "]":
            return
        value, index = decoder.raw_decode(text, index)
        yield value


def extract_io(cycles):
    """The port transactions of one test, in the order they happened.

    The suite's README: the address latch is valid on the cycle ALE is
    asserted, and an I/O read or write happens on T3 or the last wait
    state — the last cycle of the run in which the bus controller's I/O
    status lines are active. So: latch the port when ALE goes up on an
    IOR/IOW cycle, and take the data bus from the final cycle of the
    status run that follows.
    """
    transactions = []
    port = 0
    active = False
    direction = ""
    data = 0
    for cycle in cycles:
        if (cycle[CYCLE_PINS] & 1) and cycle[CYCLE_BUS_STATUS] in ("IOR", "IOW"):
            port = cycle[CYCLE_ADDRESS] & 0xFFFF
        status = cycle[CYCLE_IO_STATUS]
        if status != "---":
            active = True
            direction = "r" if status[0] == "R" else "w"
            data = cycle[CYCLE_DATA]
        elif active:
            transactions.append([port, data, direction])
            active = False
    if active:
        transactions.append([port, data, direction])
    return transactions


def condense(stem, raw):
    """Raw .gz bytes in, condensed .gz bytes out."""
    text = gzip.decompress(raw).decode("utf-8")
    wants_io = stem in PORT_OPCODES

    records = []
    for test in each_test(text):
        record = {
            "name": test["name"],
            "idx": test["idx"],
            "bytes": test["bytes"],
            # queue is dropped: half the vectors start with a full
            # prefetch queue that duplicates the bytes already in ram,
            # and a non-prefetching emulator ignores it (README, "Using
            # The Tests"). cycles and hash go with it.
            "initial": {
                "regs": test["initial"]["regs"],
                "ram": test["initial"]["ram"],
            },
            "final": {
                "regs": test["final"]["regs"],
                "ram": test["final"]["ram"],
            },
        }
        if wants_io:
            transactions = extract_io(test["cycles"])
            if transactions:
                record["io"] = transactions
        records.append(json.dumps(record, separators=(",", ":")))

    header = json.dumps(
        {
            "suite": SUITE_REPO,
            "set": SUITE_SET,
            "sha": SUITE_SHA,
            "condenser": CONDENSER_VERSION,
            "stem": stem,
            "count": len(records),
        },
        separators=(",", ":"),
    )
    body = f'{header[:-1]},"tests":[{",".join(records)}]}}'.encode("utf-8")

    # mtime=0: the same input must produce the same bytes, so that a
    # cache built on one machine is the cache built on another.
    return gzip.compress(body, compresslevel=6, mtime=0)


def fetch_one(job):
    stem, directory = job
    target = Path(directory) / f"{stem}.json.gz"
    if target.exists():
        return stem, target.stat().st_size, 0

    raw = download(f"{BASE_URL}/{stem}.json.gz")
    condensed = condense(stem, raw)

    # Write beside the target and rename: an interrupted run must leave
    # no half-written file for the next one to treat as cached.
    scratch = target.with_suffix(".part")
    scratch.write_bytes(condensed)
    scratch.replace(target)
    return stem, len(condensed), len(raw)


def main():
    parser = argparse.ArgumentParser(
        description="Fetch and condense the SingleStepTests/8088 v2 vectors."
    )
    parser.add_argument("--out", help="cache directory (default: see --print-dir)")
    parser.add_argument(
        "--stems",
        nargs="+",
        metavar="STEM",
        help="only these files, e.g. 00 90 80.0 (default: all 323)",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=min(DEFAULT_JOBS, (os.cpu_count() or 2)),
        help="parallel download+condense workers",
    )
    parser.add_argument(
        "--force", action="store_true", help="re-fetch files already cached"
    )
    parser.add_argument(
        "--print-dir", action="store_true", help="print the cache directory and exit"
    )
    parser.add_argument(
        "--print-key", action="store_true", help="print the cache key and exit"
    )
    args = parser.parse_args()

    directory = Path(args.out) if args.out else default_cache_dir()

    if args.print_key:
        print(cache_key())
        return 0
    if args.print_dir:
        print(directory)
        return 0

    known = read_manifest()
    if args.stems:
        unknown = [s for s in args.stems if s not in known]
        if unknown:
            print(f"not vector files of this pin: {' '.join(unknown)}", file=sys.stderr)
            return 2
        stems = args.stems
    else:
        stems = known

    directory.mkdir(parents=True, exist_ok=True)
    if args.force:
        for stem in stems:
            (directory / f"{stem}.json.gz").unlink(missing_ok=True)

    print(f"{len(stems)} vector files -> {directory}")
    started = time.monotonic()
    condensed_bytes = 0
    raw_bytes = 0
    cached = 0
    done = 0

    with ProcessPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        jobs = [(stem, str(directory)) for stem in stems]
        for stem, size, raw in pool.map(fetch_one, jobs):
            done += 1
            condensed_bytes += size
            raw_bytes += raw
            if raw == 0:
                cached += 1
            else:
                print(
                    f"  [{done}/{len(stems)}] {stem}: "
                    f"{raw / 1e6:.1f} MB -> {size / 1e6:.1f} MB"
                )

    elapsed = time.monotonic() - started
    print(
        f"done in {elapsed:.0f}s: {len(stems)} files, {cached} already cached, "
        f"{raw_bytes / 1e6:.0f} MB downloaded, {condensed_bytes / 1e6:.0f} MB on disk"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
