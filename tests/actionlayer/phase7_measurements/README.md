# phase7_measurements

Golden-file / smoke-test fixtures for the Seamly2D JSON action layer's Phase 7
(`measurements.load`, `measurements.recompute`, `measurements.sync`), run
against the standalone `actiond` host built by `src/app/actiond/actiond.pro`.

These three actions mirror `MainWindow::updateMeasurements()`,
`MainWindow::syncMeasurements()`, and `MainWindow::checkRequiredMeasurements()`
(`src/app/seamly2d/mainwindow.cpp`) without any `MainWindow` dependency — see
`src/libs/actionlayer/handlers/measurements_sync_handlers.cpp` for the
line-by-line trace of which real function each check/step reuses.

## Layout

```
phase7_measurements/
  fixtures/
    base_pattern.val            a tiny pattern (basePoint A + endLine A1, A1's
                                  length formula is the measurement
                                  "shoulder_length") -- see "Regenerating
                                  fixtures/base_pattern.val" below
    measurements_a.smis          individual file, shoulder_length = 13 cm
                                  (the value base_pattern.val was baked with)
    measurements_b.smis          individual file, shoulder_length = 20 cm
                                  (same name, different value -- proves a
                                  recompute actually picks up the change)
    measurements_incomplete.smis individual file that omits shoulder_length
                                  entirely (drives the missing-measurement
                                  error case)
    measurements_multisize.smms  a real multisize sample (copied verbatim from
                                  src/app/share/samples/measurements/multisize/
                                  gost_man_ru.smms) -- drives the type-mismatch
                                  error case
  cases/
    01_load_only.json             ...through...
    06_type_mismatch_error.json  six multi-action batches; see "What each case
                                   proves" below
  check_output.py                 per-case assertion functions run_tests.sh
                                   calls after each case
  run_tests.sh                    drives actiond over each case, saves
                                   output/<case>.out.json, prints PASS/FAIL
  output/                         actual output of each run (gitignored;
                                   scratch)
```

## Running

```bash
./run_tests.sh                          # run all six cases
./run_tests.sh 05_missing_measurement_error   # run just one
```

Every case is invoked the same way:

```
actiond --pattern fixtures/base_pattern.val \
        --measurements fixtures/measurements_a.smis \
        --actions cases/<case>.json \
        --save-pattern output/<case>.pattern.val
```

`--measurements fixtures/measurements_a.smis` establishes the *starting*
state every case begins from: it's the same file `base_pattern.val`'s A1
point was originally baked with, so a case's first `pattern.dump` (or the
whole run, for cases that never call any of the three Phase 7 actions on
purpose) shows exactly the baked-in geometry, and `qApp->patternType()` gets
set to `Individual` for the duration of that process (see
`action_host.cpp`'s Phase 7 comment on why this matters for
`measurements.load`'s type-consistency guard).

`check_output.py` reads each `output/<case>.out.json`, runs that case's
assertion function, and prints `PASS` or `FAIL: <reason>`; `run_tests.sh`
relays that and exits non-zero if any case failed. There is no golden
`expected/` directory here (unlike `tests/actionlayer/actionlayer-tests/`'s
diff-against-golden-files approach) — Phase 7's fixtures assert on *specific
computed values* (an exact pixel coordinate, an exact missing-measurement
name, byte-identical before/after dumps) rather than diffing whole response
files, since that is a more direct proof of the actual behavior described
below.

## What each case proves

| Case | Proves |
| --- | --- |
| `01_load_only` | `measurements.load` does **not** recompute geometry. Loads `measurements_b.smis` (shoulder_length=20cm) but never calls `measurements.recompute`; A1's dumped `x` must still equal the value computed from the *original* baked-in 13cm formula result. |
| `02_load_then_recompute` | `measurements.recompute`, called separately after a `measurements.load`, *does* pick up the new value — A1's `x` now equals the 20cm result. |
| `03_sync_single_action` | `measurements.sync` alone produces the **identical** end-state as case 02's two separate actions — proving it's a genuine composition of `load` + `recompute`, not a divergent reimplementation. (Verified by diffing case 02's and case 03's final `pattern.dump` directly — see the repo's Phase 7 self-verification notes.) |
| `04_switch_measurement_sets` | The full "give action, verify, give next action" loop shape, with two real state transitions inside **one** `actiond` invocation: load set A → dump (baseline) → load set B + recompute → dump (changed). |
| `05_missing_measurement_error` | The `checkRequiredMeasurements()`-equivalent check runs *before* any mutation: loading a file that omits `shoulder_length` (a measurement `base_pattern.val`'s only formula needs) fails with a structured `missingMeasurements` error naming it, and a `pattern.dump` taken before vs. after the failed load are byte-identical (after sorting by id — see `check_output.py`'s `normalized_objects()`, matching the same QHash-iteration-order caveat `tests/actionlayer/actionlayer-tests/run_tests.sh` already documents from Phase 6). |
| `06_type_mismatch_error` | Loading a multisize file against a pattern whose established measurement type is `Individual` fails with a structured `measurementTypeMismatch` error (not a crash), and, like case 05, mutates nothing. |

## Regenerating `fixtures/base_pattern.val`

`base_pattern.val` isn't hand-written — it was generated by running the
already-built `actiond` binary against
`tests/actionlayer/fixtures/empty_pattern.val` (the Phase 5 seed pattern) and
`fixtures/measurements_a.smis`, with this one-shot action batch:

```json
{
  "actions": [
    { "op": "basePoint", "name": "A", "x": 0, "y": 0, "draftBlock": "Front" },
    { "op": "endLine", "name": "A1", "basePoint": "A",
      "length": "shoulder_length", "angle": "0", "lineType": "solidLine" }
  ]
}
```

```bash
actiond --pattern ../fixtures/empty_pattern.val \
        --measurements fixtures/measurements_a.smis \
        --actions <the batch above, saved to a file> \
        --save-pattern fixtures/base_pattern.val
```

**Gotcha worth knowing if you regenerate this file (or write a new fixture
pattern the same way):** the earlier Phase 6 fixtures all use
`"lineType": "hair"` in their *action* JSON, and that's accepted fine when
the point is created — but `"hair"` is **not** one of the XSD schema's
enumerated `linePenStyle` values (`src/libs/ifc/schema/pattern/v0.7.4.xsd`;
valid values are `byGroup`, `none`, `solidLine`, `dashLine`, `dotLine`,
`dashDotLine`, `dashDotDotLine`). Nothing validates that at *creation* time,
so a `--save-pattern`'d file with `lineType="hair"` writes out fine — but
fails to *re-load* as a `--pattern` input the next time, with
`{"error":"Exception: value 'hair' not in enumeration"}`. Phase 6's own
fixtures never round-trip their own saved `.val` back in as `--pattern`, so
this never surfaced there; Phase 7's fixtures do exactly that (that's the
whole point of `base_pattern.val`), so this file uses `"solidLine"` instead.

## Adding a new case

1. Add `fixtures/your_measurements.smis` (or reuse an existing one) if the
   case needs a new measurement file.
2. Add `cases/NN_short_description.json`: a multi-action batch, in the same
   `{"actions": [...]}` shape `ActionEngine::run()` expects. Put a
   `"_comment"` field at the top explaining what real behavior it's meant to
   exercise and what the expected outcome is — every case here does this, and
   it doubles as documentation when someone reads the fixture directly. Chain
   a realistic sequence (load → dump → change → recompute → dump) rather than
   one action per file wherever a sequence better demonstrates the behavior.
3. Add a `check_NN_your_case(results)` function to `check_output.py`, and
   register it in the `CHECKS` dict under the case's exact filename stem
   (without `.json`). `results` is the parsed `"results"` array from
   `output/NN_your_case.out.json` — one entry per action, in order, each
   shaped like `{"op", "ok", "value", "error"}`.
4. Run `./run_tests.sh NN_your_case` to iterate on just the new case, then
   `./run_tests.sh` (no argument) to confirm the full set still passes.
