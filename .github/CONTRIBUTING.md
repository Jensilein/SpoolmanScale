# Contributing to SpoolmanScale

Thank you for helping. Every kind of help is welcome: a bug report with a log,
a photo of a screen that looks wrong, a translation, an idea, or code. Large
parts of SpoolmanScale were built by people who started with exactly that.

SpoolmanScale runs on a small ESP32-S3 with a nearly full display memory, and
every change is only really proven on a real scale. Most of the rules below
exist because a past pull request broke something on the device, so each one
comes with its reason.

**Working with a coding agent?** Let it read [AGENTS.md](../AGENTS.md) first.
It holds the exact house rules, where new code goes, and the checks to run.

## Before you start

- **Small fix** (a typo, an obvious bug, a few lines): open a pull request directly.
- **Anything bigger** (a feature, a new screen, a change to NFC, the scale or a
  backend): open an issue or ask in the [Discord](https://discord.gg/K3KmKnrWhP)
  first and wait for a short OK. It saves you work: sometimes the problem is
  already being solved on `dev`, or it needs a different layer than it seems.

Sometimes a reported problem gets fixed directly on `dev`, or solved a
different way. That is not a judgement of your work.

## How contributions land here

Most pull requests are not merged with the button. Usually your commits are
cherry-picked onto `dev` **with your authorship kept**, anything that still
needs hardening follows as a separate commit, and the pull request is then
closed with a note on what went in. This is normal here and not a rejection:
it keeps `dev` testable on hardware one step at a time.

## Branch and scope

- **Target `dev`, never `main`.** `main` only holds released versions; a pull
  request against it is refused automatically.
- **Start from a fresh `dev`** and rebase before you open the pull request.
  `dev` moves fast: pull requests 90 or more commits behind had to be redone.
- **One topic per pull request.** No stacked pull requests that depend on each
  other, and no unrelated clean-ups in the same diff.
- **Only change what your topic needs.** Never revert or reformat code you did
  not mean to touch.
- Branch names: `feature/...`, `fix/...`, `docs/...`.
- **Do not bump the version.** The maintainer does that when your change lands,
  otherwise every pull request conflicts with every other.

## Build and check

Setup: [PlatformIO](https://platformio.org/), plus `python3` and `node` for the
checks. Hardware and wiring: [BUILDING.md](../BUILDING.md).

```bash
scripts/check.sh      # house rules, the same script CI runs
pio run               # firmware build (env wt32-sc01-plus)
pio run -t upload     # flash over USB
```

`scripts/check.sh` must be green. Some of its numbers are a ratchet against
`scripts/conventions_baseline.json`: old debt is frozen, a count may stay or
fall, never rise. `python3 scripts/check_conventions.py --verbose` shows every
finding. CI runs the same script plus the build on every pull request.

## House rules

The house rules, with their reasons, are in [AGENTS.md](../AGENTS.md#house-rules):
texts through `T()` in three languages, colours from `src/ui/theme.h`, no HTTP
in an event handler, the NFC bus belongs to the main loop, and more.
`scripts/check.sh` checks most of them.

## Testing

Say what you tested and on what: board, firmware base, backend (Spoolman,
FilaMan or BamBuddy). A successful build proves little on this device. If you
had no hardware for a part of the change, say so; that is fine, it just tells
the maintainer what to test.

**Screenshots:** for a change on the display, take screenshots before and after
on the web interface (Logs page) and attach them. For a web page, a browser
screenshot.

## Commits and the pull request text

- [Conventional Commits](https://www.conventionalcommits.org/) in English:
  `fix: ...`, `feat: ...`, `docs: ...`. The message says what changes for the
  user, not the path you took.
- Author and `Co-authored-by` lines name people only. You are responsible for
  every line you submit.
- The pull request text must match the diff: name every file you touched and
  why, and nothing that is not in it.
- Before/after with real numbers where you can (time, bytes, grams).
- Did you search for **every** caller of what you changed? Say so.

## Documentation

The user manual lives in its own repository,
[SpoolmanScale-Docs](https://github.com/Niko11111/SpoolmanScale-Docs). If your
change needs a manual update, mention it in the pull request; a companion pull
request there is welcome but not required.

## License

SpoolmanScale is licensed under GPL-3.0-or-later, see [`LICENSE`](../LICENSE)
and [issue #45](https://github.com/Niko11111/SpoolmanScale/issues/45). By
submitting a contribution you agree to license it under GPL-3.0-or-later, and
you grant Nikolai Herrmann the right to also license it under other terms, so
the project is not locked into one license for good. The GPL version stays free
either way, and you keep the copyright on your work and stay credited as its
author.

## Thank you

Everyone whose work went into the firmware is named in the
[Credits](../README.md#credits) of the README. Your name belongs there too.
