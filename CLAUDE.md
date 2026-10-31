# Notes for Claude

This is a **personal fork** of OpenRCT2 for adding features. The builds are only for the owner, who plays on
**Windows** using the **Windows-portable-x64** build. Nothing is distributed, so pragmatic or hacky fixes are fine,
and there's no need to keep compatibility with official OpenRCT2 multiplayer or replays. Still keep changes safe
for save files.

## Repository layout

- `main` holds a snapshot of upstream OpenRCT2 `develop` (commit `3b7114c`, just after v0.5.6), imported **without
  upstream history**. The `upstream` remote (https://github.com/OpenRCT2/OpenRCT2) has to be added in each new
  session. Without shared history, upstream updates have to be applied as patches, for example
  `git diff <old-upstream-sha> <new-upstream-sha> | git apply -3`.
- Each feature goes on its own branch, merged into `main` when the owner is happy with it.
- The repo has no git tags. Pushing tags (and force-pushing) is blocked from cloud sessions.
- If a clone only tracks `main`, fetching another branch needs a refspec, for example
  `git config --add remote.origin.fetch '+refs/heads/<branch>:refs/remotes/origin/<branch>'`.

## Features

| Feature | Branch (merged) | Setting (`config.ini`, `[general]`) | Code |
|---|---|---|---|
| Guests spread across all lanes of wide paths (any width up to 13 tiles), and up to ±7 from a lane's centre line (±6 next to path additions; stock ±3) | `wide-path-lanes` | `guests_spread_across_wide_paths` (default `true`; `false` restores stock behaviour exactly) | `src/openrct2/peep/GuestPathfinding.cpp` ("Wide path lanes" region, `kPathCentreLineClamp*`), tests in `test/tests/WidePathLanes.cpp` |

Keep this table up to date when adding features.

How the wide path lanes work: the regular pathfinding avoids tiles with the wide flag, so it is run from the nearest
non-wide tile across the path (the "main lane"), and the guest walks in their preferred lane alongside it (chosen
from their id, counted from the left of their walking direction), moving diagonally one lane per tile.
`PeepMoveOneTile` can be put in "capture" mode to ask the regular pathfinding which way it would go without moving
the guest. The owner tests with 10,000+ guests, so keep per-guest work cheap (no allocations per step).

## Building and testing (Linux cloud session)

Install the dependencies (takes a few minutes):

```sh
apt-get update && apt-get install -y libzip-dev libpng-dev zlib1g-dev libzstd-dev libicu-dev \
  libcurl4-openssl-dev libssl-dev libfreetype-dev libfontconfig-dev nlohmann-json3-dev libgtest-dev \
  libgmock-dev libsdl2-dev libspeexdsp-dev libflac-dev libvorbis-dev pkg-config ccache
```

Configure and build headless (no GUI), with tests (the full build takes about 20 minutes on 4 cores):

```sh
mkdir -p build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDISABLE_GUI=ON -DWITH_TESTS=ON -DDISABLE_DISCORD_RPC=ON \
  -DDOWNLOAD_TITLE_SEQUENCES=OFF -DDOWNLOAD_OPENSFX=OFF -DDOWNLOAD_OPENMUSIC=OFF
ninja openrct2-cli OpenRCT2Tests
```

The tests need game data (language files, objects, `g2.dat`). Build it and install it into `build/` once:

```sh
ninja graphics && DESTDIR=. ninja install   # creates build/data -> usr/local/share/openrct2
./OpenRCT2Tests                             # or --gtest_filter='WidePathLanes*'
```

Known pre-existing failure: `RideRatings.EverythingPark` fails because its test data isn't available here
(it loads 0 of the 529 rides it expects). It's unrelated to our changes.

Tests read `~/.config/OpenRCT2/config.ini`, so tests that depend on a setting should set it explicitly
(e.g. `Config::Get().general.guestsSpreadAcrossWidePaths = ...`). `ReplayTests` turns gameplay changes off,
because the replays were recorded with vanilla behaviour.

The original RCT2 game data isn't available here, so the game can't be played or watched in a cloud session.
Behaviour has to be checked with tests (load a test park, build a layout, step guests with
`performNextAction()`, measure the result).

## Formatting

CI checks formatting with **clang-format 20**. The system one may be older and formats differently. Get 20 with:

```sh
python3 -m venv /tmp/cfvenv && /tmp/cfvenv/bin/pip install "clang-format==20.*"
/tmp/cfvenv/bin/clang-format -i <changed files>
```

## Getting a Windows build

Pushing a branch runs GitHub Actions CI (`.github/workflows/ci.yml`). It has been cut down for this fork: version
info, formatting checks, and a single Windows x64 job that builds the portable zip and runs the tests (about 10
minutes). The owner downloads the **`OpenRCT2-<version>-Windows-portable-x64`** artifact from the run's page
(https://github.com/Fashe27/openrct2-claude/actions). Check runs with `gh run list -R Fashe27/openrct2-claude`.

- The "Git describe" step falls back to the version in `src/openrct2/Version.h`, because the repo has no tags.
- All other upstream workflows (issue/PR management, translations, releases) have been removed.
- **Ask the owner before pushing changes to CI**, and in general confirm before pushing.
