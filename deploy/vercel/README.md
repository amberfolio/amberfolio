<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# Deploying the wasm host

Every push to `main` publishes the WebAssembly host to
<https://amberfolio.vercel.app>; every pull request from this repository
gets its own preview URL. The `deploy` job in
[`.github/workflows/ci.yml`](../../.github/workflows/ci.yml) does it, and
nothing about it is manual after the one-time setup below.

## Built here, deployed there

The wasm leg of the CI matrix already has the Emscripten toolchain pinned
by [`.emscripten-version`](../../.emscripten-version) and cached. Vercel's
build image has none of that, and pinning the toolchain a second time in
their settings is a second source of truth waiting to drift. So the build
job uploads `build/wasm/hosts/web/Debug/` as an Actions artifact and the
deploy job ships those exact bytes with `vercel deploy --prebuilt`.
Nothing is compiled on Vercel's side; their project has no build step to
run.

That is why the routing and header policy lives in `config.json` here
rather than in a `vercel.json` at the repository root. `vercel.json` is an
input to Vercel's *build*, which is compiled into
`.vercel/output/config.json`; a prebuilt deployment skips the build, and
with it any `vercel.json`. So this file is that compiled form, written
directly — the [Build Output API][bo] v3 configuration — and the deploy
job copies it to `.vercel/output/config.json` beside the downloaded
artifact.

[bo]: https://vercel.com/docs/build-output-api/v3

What it asserts:

- **`.wasm` is `application/wasm` and `.mjs` is `text/javascript`.**
  Asserted rather than trusted: a module script served as `text/plain` is
  refused by browsers, which is precisely the failure
  [`scripts/serve-web.py`](../../scripts/serve-web.py) exists to dodge
  locally. Getting it wrong leaves the page stuck on "loading…", so it is
  worth stating.
- **`Cache-Control: no-store` on everything.** The filenames are not
  content-hashed, so a cached `amberfolio.wasm` against a freshly
  deployed `host.mjs` is possible, and "stale wasm" is a miserable first
  bug to hit. When the page is worth caching — M6, at the earliest — this
  becomes a real policy with hashed asset names.

## One-time setup

Done once, from the repository root, with the CLI at the version the
workflow pins:

```sh
npm install --global vercel@59.1.3
vercel login
vercel project add amberfolio        # named so the domain is amberfolio.vercel.app
vercel link --yes --project amberfolio
cat .vercel/project.json             # orgId and projectId
```

Create it this way rather than through the dashboard's "Add New →
Project", which imports a Git repository and gives Vercel a build of its
own — the second source of truth this design exists to avoid. A
CLI-created project has no repository connected and no build, install or
output overrides, which is exactly the wanted state; `vercel project
inspect amberfolio` confirms it.

Then three repository secrets (Settings → Secrets and variables →
Actions):

- `VERCEL_TOKEN` — Vercel account settings → Tokens. Tokens are scoped
  to an account or team, not to a single project, so pick the narrowest
  scope that owns this one and set an expiry you are willing to renew.
  This is the one step with no CLI equivalent.
- `VERCEL_ORG_ID` — `orgId` from `.vercel/project.json`.
- `VERCEL_PROJECT_ID` — `projectId` from the same file.

Until those secrets exist the deploy job still runs, reports that it has
nothing to deploy with, and passes. Forks never see them: a pull request
from a fork is not deployed at all, by design, and says so in its log.

## What ships

Only the contents of the wasm build tree — the module, its glue, the JS
host, and the page — minus the node smoke check, which is test apparatus.
That directory is `build/`-derived and gitignored, so no repository
content can ride along, and the deploy job lists every file it is about
to upload into the job summary. The page hosts the emulator and nothing
else: no game artifacts, ever, here or anywhere.
