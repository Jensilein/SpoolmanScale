# Rules for coding agents

This file is for AI coding agents working on SpoolmanScale. People start with
[.github/CONTRIBUTING.md](.github/CONTRIBUTING.md), which points here.

SpoolmanScale is ESP32-S3 firmware for a filament scale: NFC tags, an LVGL
touch screen of 480 x 320, a web interface, and three inventory backends. It
runs with a nearly full display memory, and a change is only proven on a real
scale. Most rules below exist because an earlier change broke something on the
device.

## Before you write code

1. Read the code you are about to change, and every caller of it.
2. **Small fix:** go ahead. **Anything bigger** (a feature, a new screen, NFC,
   the scale, a backend): the person you work for opens an issue first and
   waits for a short OK. Work is often already under way on `dev`.
3. Work on a fresh `dev`. One topic per change; never reformat or revert code
   the topic does not need.
4. Do not bump the version. The maintainer does that when the change lands.

## Think beyond your own setup

Most forks start as "it works on my scale". A change here has to hold for
everyone. Before you build, name what else the change touches:

- **Three backends, four modes:** Spoolman, FilaMan, BamBuddy with its own
  inventory, and BamBuddy with Spoolman behind it. A feature goes through
  `src/services/backend_api.h`. Where a backend cannot do it, return
  `BACKEND_NOT_SUPPORTED` and say so; never leave it out silently.
- **Many tag and spool makers:** Bambu Lab, Creality, Snapmaker, Anycubic,
  Qidi, Elegoo, OpenSpool, blank NTAGs. A rule for one maker's tag must not
  change what happens to another's.
- **Several printers and label printers.**
- **Existing features** the change may meet: tag writing, AMS view, drying,
  automatic weighing, the web pages, the first setup, three languages.
- Where it is reasonable, build the feature for every backend and maker, not
  only for yours. Where it is not, say why in the pull request.

## Where new code goes

| New code | Place |
|---|---|
| Backend call from UI or state code | `src/services/backend_api.h` only |
| Request specific to one backend | `src/services/spoolman_actions.cpp`, `filaman_api.cpp`, `bambuddy_api.cpp` |
| New screen or popup | a file of its own in `src/ui/`, built on `src/ui/theme.h` and the shared parts in `ui_common` |
| A number entry | `src/ui/num_pad` (`showNumPad`) |
| Colour | a line in `src/ui/theme_palette.h`, one value per palette |
| Radius, font, button size | `src/ui/theme.h`, never inline |
| Writing to an NFC tag | `src/services/tag_write.cpp` |
| NVS access | `src/services/prefs_store.cpp` |
| Work an event handler must not do | a `*_pending` flag in `src/app/deferred_actions.h`, done in `src/app/app_loop.cpp` |
| Web page | a file in `src/web/pages/` plus its line in `src/web/web_pages.cpp` |
| Pure logic (compute, decide, format) | a unit without Arduino, with a host test in `test/` |
| UI text | the end of `src/lang.h` and `src/lang.cpp` |

## House rules

`scripts/check.sh` checks most of these. Some of its numbers are a ratchet
against `scripts/conventions_baseline.json`: a count may stay or fall, never
rise. `python3 scripts/check_conventions.py --verbose` lists every finding.

**Display (LVGL)**

- Colours, radii and fonts come from `src/ui/theme.h`; a new colour is named in
  `theme_palette.h` first. No six-digit hex anywhere else.
- Build on the shared parts: `uiPopupBox()` for a popup frame,
  `styleLeaveButton()` for Cancel and Back, `makeListBtn()` for a settings row
  (the whole row is the button, ON/OFF on the right), `showNumPad()` for
  numbers. A screen that draws its own versions of these does not fit in.
- An event handler never does HTTP, never blocks and never deletes its own
  object: it sets a pending flag, the loop does the work.
- Never `lv_scr_load()`: it reboots the scale. Screens are overlays.
- The display memory is nearly full. Build a screen when it opens, release it
  when it closes, keep permanent objects off the main screen.
- Debounce with `millis()` in the loop, not with a short `lv_timer`.

**Texts and languages**

- Every caption goes through `T(STR_XXX)`, also one that reads the same in
  every language. Copy a text only when formatting, with `copyT()` or
  `snprintf()`, never `strncpy` from `T()`. Cut names with `utf8Cut()`.
- A new text is a row at the end of `src/lang.h` and `src/lang.cpp` with
  German and English, then French with `tools/lang_fr.py` (`seed`, `apply`,
  `emit`), see [tools/FRENCH_TRANSLATION.md](tools/FRENCH_TRANSLATION.md).
  Translate from where the text sits in the code, and keep it no wider than
  German and English.
- German uses real umlauts. Comments are English.
- A conflict in `src/lang.cpp` is never resolved by keeping both sides.

**NFC and I2C**

- The reader and the load cell share one I2C bus that belongs to the loop.
  A web handler never touches it: it parks a request.
- Writing to a tag goes through `src/services/tag_write.cpp`, MIFARE blocks
  through `src/services/mifare_write.cpp`. Pages below 4 are never written.
- A change to the NFC or I2C path is tested with a real tag on a real scale.

**Web interface**

- Every route checks its gate with `webRequire()`.
- A page script sits inside a C++ string literal: bind listeners after
  rendering, never through HTML attributes, and give every `fetch` a `.catch`.

**Code**

- A function does one thing: at most 50 lines, 5 parameters, 3 levels of
  nesting. A new file stays under 1000 lines.
- No magic numbers: a named constant with its unit.
- Every external call can fail (HTTP, NVS, SD card, I2C): log it and tell the
  user which step failed.
- No em-dash (U+2014) anywhere: the compiler and the display both fail on it.
  Use " - ".
- ArduinoJson 7: `filter.to<JsonArray>().add<JsonObject>()`, never
  `createNestedObject()`.
- Every dependency is pinned in `platformio.ini`.

## Check before you hand over

```bash
scripts/check.sh            # house rules and the ratchet, as CI runs it
pio run                     # firmware build
for t in test/test_*.py; do python3 "$t"; done   # host tests
```

All three green. A build alone proves little on this device: say what was
tested on hardware and what was not. For a display change, the web interface
takes screenshots of the screen (Logs page).

## Review before the pull request

Before the person you work for opens the pull request, have the diff reviewed
by a second, independent reviewer: another, stronger model or a person, given
the code but not your conclusions. Fix what holds up, and say in the pull
request that the review ran and what came out of it.

## Commits and the pull request

- Conventional Commits in English (`fix:`, `feat:`, `docs:`). The message
  says what changes for the user.
- Author and `Co-authored-by` lines name people only; no tool attribution in
  commits, comments or pull requests. The person who submits is responsible
  for every line.
- The pull request text matches the diff: every file touched and why, nothing
  that is not in it, before and after with real numbers where possible.
