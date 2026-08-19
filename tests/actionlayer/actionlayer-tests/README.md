# actionlayer-tests

Golden-file / smoke-test fixtures for the Seamly2D JSON action layer (Phase 6:
formula-bearing point tools), run against the standalone `seamly2d-actiond` host.

## Layout

```
actionlayer-tests/
  fixtures/         JSON action-batch inputs, numbered in increasing complexity
  measurements/      real .smis measurement files copied from Seamly2D's own samples
  expected/           golden .val / .png references (generate once, then diff against)
  output/             actual output of each run (gitignored; scratch)
  run_tests.sh         drives seamly2d-actiond over each fixture, diffs against expected/
```

## Fixture naming

`NN_short-description.json` — run in numeric order; each roughly builds on the
constructs proven by the previous one. `08_error_*` fixtures are expected to
produce a structured JSON error, not a crash — the runner checks for that
explicitly rather than diffing output.

## Running

```bash
./run_tests.sh                 # run all fixtures, compare against expected/
./run_tests.sh 03_endline_with_measurement_formula.json   # run just one
UPDATE_EXPECTED=1 ./run_tests.sh   # (re-)generate expected/ from current output
                                     # -- only do this after manually verifying
                                     # the .val and .png are actually correct
```

Each fixture's response and rendered PNG land in `output/<fixture-name>/`:
- `response.json`   — full daemon response for the batch
- `pattern.val`      — saved pattern state (only if the fixture includes `session.save`)
- `snapshot.png`     — rendered scene (only if the fixture includes `render.snapshot`)

## Why real measurement files

`measurements/male_shirt.smis` is copied verbatim from
`src/app/share/samples/measurements/individual/male_shirt.smis` in the Seamly2D
repo itself, not hand-written — so formula fixtures that reference
`neck_back_to_waist_b` / `shoulder_length` / etc. are guaranteed to be valid,
real measurement names rather than invented ones that might not match the
actual `.smis` schema.

## Coordinate units (pattern.dump / render.snapshot)

Every coordinate `pattern.dump` reports (`x`/`y` on a point object) and every
pixel `render.snapshot` renders is in Seamly2D's internal **pixel** space, at
the fixed `PrintDPI = 96.0` constant (`src/libs/vmisc/def.cpp`) — this is a
plain scale factor, not a screen DPI query, so it's identical across machines.
`ToPixel(value, Unit::Mm)` (also `src/libs/vmisc/def.cpp`) converts as
`px = (mm / 25.4) * 96.0`, i.e. **1 mm ≈ 3.7795 px**. All of this fixture set's
pattern files declare `<unit>mm</unit>` (see `../fixtures/empty_pattern.val`),
so e.g. fixture 02's `A1 = endLine(A, length="10", angle="0")` should dump as
`x ≈ 37.795, y = 0` — not `x = 10`. If you add a fixture against a pattern
file declaring a different `<unit>`, the same 96 px/inch constant still
applies, just via the `Cm`/`Inch` branches of `ToPixel()` instead.

## CLI contract actually used by this script

`src/app/actiond/main.cpp` (as of Phase 6) takes `--pattern`, `--measurements`,
and `--actions` as **file paths** (no stdin, no `--output-dir`), and writes one
compact JSON line to stdout. `run_tests.sh` extracts each fixture's own
`"measurements"` field (a plain grep/sed, not a real JSON parse — good enough
since it's always a flat string) to pick `--measurements`, always passes
`../fixtures/empty_pattern.val` as `--pattern`, and always passes
`--save-pattern <output-dir>/pattern.val` so every run's mutated pattern is
saved regardless of whether the fixture's own JSON happens to include a
(currently unimplemented) `"session.save"` action — see `run_tests.sh`'s own
comments for why fixture 09's trailing `session.save` action is expected to
show up as one ordinary failed entry in `response.json`'s `"results"`, not a
whole-run failure.

## Two pre-existing bugs this phase's fixture run surfaced (and fixed)

Running this fixture set for the first time against a real build exposed two
bugs that had no earlier test coverage:

1. **Every fixture's `basePoint` action was missing `"draftBlock"`.**
   `handleBasePoint` (`point_handlers.cpp`, Phase 5) requires a non-empty
   `"draftBlock"` — VToolBasePoint always anchors a brand-new pattern piece,
   never merges into an existing one. Every fixture here now passes
   `"draftBlock": "Front"` on its one `basePoint` action.
2. **`render.snapshot` crashed the whole process** the first time it painted
   any point with its name label shown (the default). `action_host.cpp`
   constructs an offscreen `VMainGraphicsView` and calls
   `qApp->setSceneView(&sceneView)`, but never attached it to `draftScene` via
   `sceneView.setScene(&draftScene)`. `VGraphicsSimpleTextItem::paint()`
   (`vgraphicssimpletextitem.cpp`) unconditionally does
   `scene->views().at(0)` to find a view to reference — with zero attached
   views, that's an out-of-range `QList::at()` call, which is undefined
   behavior (not bounds-checked) in a release build, and it segfaulted the
   daemon. Fixed by adding the missing `sceneView.setScene(&draftScene)` call
   in `action_host.cpp`.

Neither is part of the six formula-point-tool handlers this phase adds, but
both blocked every fixture from running end-to-end, so they're fixed here
rather than worked around.
