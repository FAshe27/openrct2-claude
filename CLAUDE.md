# Notes for Claude

> **This repo is an archive.** It's where the wide-path lanes started. All work now happens in the owner's
> shipping fork, **FAshe27/OpenRCT2**, on branches from **`guest-spread-v1`**. Don't add features or push builds
> here; read on for how to work there.

The owner (Franklin) plays on **Windows** using the **Windows-portable-x64** build. Builds are only for him, so
pragmatic fixes are fine and compatibility with official OpenRCT2 multiplayer or replays doesn't matter. Keep
changes safe for save files.

## Working in FAshe27/OpenRCT2 (the shipping fork)

`guest-spread-v1` is one build containing everything: Muse's features (Workers, Break Rooms, two-tier breakdowns,
restock / Stockrooms, lane spread, keep-right, corner cutting, distance-field routing) and Claude's wide-path lanes
(merged from `claude-wide-lanes` @ `ef34600f`). It is based on OpenRCT2 v0.5.6.

### Team and process

- **Franklin** decides and playtests. His playtest outranks every test harness: crowd behaviour can only be judged
  in his park. Nothing merges to `guest-spread-v1` without his explicit go.
- **Muse** (another AI agent) audits every change line by line against the sources, watches CI, and does the merges.
  Muse also builds features on that side.
- **Claude** writes code on branches in that fork, pushes them for builds, and hands over to Muse.
- **Messages:** Claude and Muse communicate through `.md` files that Franklin relays by hand. Claude writes
  `reply-to-muse-N.md`; Muse's come back as `reply-to-claude-N.md`. Keep handoffs few and complete (Franklin's
  budget): one per round, with the branch, exact head SHA, the delta, mechanism, evidence and numbers, stated
  precisely enough to audit in one pass.
- **Pushes:** a push to a work branch is a staging step, not a ship signal. Freeze the branch at the head Franklin is
  playtesting; a playtest fix moves the freeze and needs a new audit.

### Rules (each one earned)

1. **RNG:** `ScenarioRand` draw counts per decision must equal stock. New randomness comes from hashes (peepId,
   tile, side) or by re-using existing draws. The wide-lane code evaluates only the guest's own tile (capture mode),
   then makes exactly one move. `WidePathLanes.cpp` has draw-count tests that compare every decision with the
   setting off and on.
2. **No save-format (ParkFile) changes** without Franklin's ruling. Runtime-only state is the default.
3. **Franklin's tuned constants stay as they are** unless he says otherwise: in-tile lane ±8, ±5 next to furniture,
   65% keep-right, `kWideLaneInnerMax = 14`. Flag anything that would change them as his call.
4. **Lane changing that reacts to other guests is out:** it caused death spirals in crowds.
5. **Check the current sources** before describing or pitching anything; Franklin's hunches about what's in the game
   keep being right.

### Building it here (Linux cloud session)

The fork's own build is as below (dependencies, cmake, `ninja graphics`, `DESTDIR=. ninja install`), with two
differences:

- GCC with `-Werror` rejects a spot in Muse's code that MSVC (the only CI) accepts, `Peep.cpp`'s
  `PeepState::operating` switch. Build locally with warnings non-fatal; the project adds `-Werror` after
  `CMAKE_CXX_FLAGS`, so put it in the build-type flags:
  `cmake .. ... -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g -DNDEBUG -Wno-error"`, and check your own files' warnings.
- **Baseline: 11 known test failures** (RideRatings ×3, ReplayTests ×2, SimplePathfindingTest ×6). A run is
  good if and only if exactly those fail. Linux lists one test more than Windows (`platform.sanitise_filename`).

CI on that fork runs on push and uploads the Windows portable x64 build **before** running the tests, so a run
shows red (the 11 baseline failures) even when the build is fine. Neither Claude nor Muse can push
`.github/workflows/*`; Franklin does that himself.

### Diagnosing with Franklin's park saves

Ask for his `.park` save whenever he reports something in his park; it's by far the fastest route. His saves use
Muse's custom objects (`fashe.ride.breakroom1`, `fashe.ride.breakroom2`, `fashe.ride.stockroom1`), which aren't
in this environment: ask him for them (they're in `Documents/OpenRCT2/object`) and put them in
`~/.config/OpenRCT2/object/`.

A throwaway gtest (not committed) that loads the save, runs `gameStateUpdateLogic()` for a few thousand ticks and
tracks each guest's tiles finds stuck guests reliably. Flag guests whose last N tile changes cover very few distinct
tiles: 80 changes within 14 tiles, and a stricter 24 within 4 for short loops. Compare against the setting off, and
tag the code paths a looping guest takes to find the cause. Fields are built at most two per game tick, so a test
that never advances ticks only gets fields for its first couple of destinations.

## This repo (archive)

- `main` holds a snapshot of upstream OpenRCT2 `develop` (`3b7114c`, just after v0.5.6), imported without upstream
  history, plus the first version of the wide-path lanes (`wide-path-lanes`, merged) and a slimmed CI. The branch
  `fix-lost-guests` (@ `3756da2c9`) is where the code was ported from; the shipping fork's version has moved well
  beyond it (field-directed steps, the exit-field fix, single-position lanes, a simpler aimless drift).
- The repo has no git tags; pushing tags and force-pushing are blocked from cloud sessions.

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
./OpenRCT2Tests                             # or --gtest_filter='WidePath*'
```

Tests read `~/.config/OpenRCT2/config.ini`, so tests that depend on a setting should set it explicitly.
The original RCT2 game data isn't available here, so the game can't be played or watched in a cloud session.
Behaviour has to be checked with tests or by running Franklin's saves headless.

## Formatting

CI checks formatting with **clang-format 20**. The system one may be older and formats differently. Get 20 with:

```sh
python3 -m venv /tmp/cfvenv && /tmp/cfvenv/bin/pip install "clang-format==20.*"
/tmp/cfvenv/bin/clang-format -i <changed files>
```

Format only your own changes; the shipping fork's existing code is already clean.
