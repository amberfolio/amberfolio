# Amber Folio

A low-level emulator for the SSI Gold Box games.

**Status: early development.** There is nothing to build or run yet — this
repository currently holds the project's governance documents and the
[project plan](PLAN.md). Code follows.

## What this will be

Amber Folio is a purpose-built, low-level emulator for the machine the SSI
Gold Box CRPGs (Pool of Radiance and its family) ran on — real-mode x86,
EGA graphics, PC-speaker sound — running in the browser via WebAssembly and
natively on desktop. You bring your own legally-owned copy of a game; the
emulator runs the unmodified original program. Quality-of-life enhancements
are applied as opt-in runtime patches to the machine's memory ("seams"),
leaving the original bytes on disk untouched.

## Principles

- **Bring your own game.** Amber Folio ships none of the original games'
  code, data, or artwork — ever. It runs your own copies (the same titles
  sold today in the official archive collections).
- **Nothing derived ships.** No original code, no disassembly, no
  translated routines. The machine and the hooks are original work, and
  this repository's full public history — from its very first commit —
  exists to make that verifiable by anyone. A content guard scans every
  commit of that history in CI on every push.
- **Fidelity first.** The goal is the real machine, faithful to the
  original's behavior, with enhancements strictly opt-in.

## License

Amber Folio is free software under the **GNU Affero General Public License,
version 3.0 only** (`AGPL-3.0-only`) — see [LICENSE](LICENSE).

- Contributions are accepted under a lightweight Apache-2.0 inbound rule
  with a DCO sign-off — **no CLA**. See [CONTRIBUTING.md](CONTRIBUTING.md).
- Commercial licensing outside the AGPL is available — see
  [COMMERCIAL.md](COMMERCIAL.md).
- The project's releases will always remain available under an OSI-approved
  open-source license.

## Trademarks

"Amber Folio" names this project and its official builds; see
[TRADEMARK.md](TRADEMARK.md). Amber Folio is an independent project, not
affiliated with or endorsed by Wizards of the Coast, Hasbro, SNEG, or any
rights holder of the referenced games. "Gold Box", "Dungeons & Dragons",
"Forgotten Realms", and the game titles are used only nominatively, to
describe compatibility; all trademarks are the property of their respective
owners.
