# Contributing to Amber Folio

Thank you for your interest! Contributions are welcome. This document is
short, but the licensing part is load-bearing — please read it once.

## Licensing of contributions

*(In force since the repository's first commit, 2026-08-15.)*

- **Outbound**: Amber Folio is distributed under **AGPL-3.0-only**.
- **Inbound**: by submitting a contribution (pull request, patch, or
  otherwise) you license your contribution under the
  [Apache License 2.0](LICENSES/Apache-2.0.txt) to the
  project maintainer and to all recipients of the software. You keep your
  copyright. No CLA, no paperwork — the pull-request template asks you to
  acknowledge this in one checkbox.
- **Sign-off**: every commit must carry a `Signed-off-by:` line
  (`git commit -s`), certifying the
  [Developer Certificate of Origin](https://developercertificate.org/) —
  that you wrote the change or otherwise have the right to submit it.

### Why Apache-2.0 inbound — what it means, honestly

The maintainer distributes Amber Folio under the AGPL and also offers
commercial license exceptions (see [COMMERCIAL.md](COMMERCIAL.md)). The
permissive inbound rule is what makes that possible without asking anyone
to sign a CLA. What you should know, plainly:

- Your contribution remains part of the AGPL-licensed project **for
  everyone, forever** — the open version never loses anything.
- The maintainer may also license the combined work, including your
  contribution, under other terms to commercial licensees.
- You retain full rights to your own work and can reuse it anywhere.
- The project's releases will always remain available under an
  OSI-approved open-source license — that is a standing commitment.

If that trade isn't acceptable to you, that's a legitimate position — in
that case please don't submit code, but bug reports, testing, and ideas
are just as valuable and carry no licensing terms.

## The clean-content rule (non-negotiable)

Amber Folio must contain **no material from the original games**: no game
code — original, disassembled, or translated — no game data, no assets,
no copyrighted byte sequences. Contributions may rely on *facts*
(addresses, offsets, format descriptions, checksums) but never on
*expression* from the games. A content guard scans every commit in the
history in CI on every push — a tripwire for obvious artifacts, while the
full public history keeps the deeper claim open to anyone's inspection.
Maintainers will reject anything that crosses this line, however useful
it would be. This applies beyond git, too: keep game files out of issues,
CI logs, and screenshots.

## Practical bits

- Use `git commit -s` (DCO sign-off, see above).
- New source files start with `// SPDX-License-Identifier: AGPL-3.0-only`.
- Early days: please open an issue before starting large changes — the
  architecture is still settling.
