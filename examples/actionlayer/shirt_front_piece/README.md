# Shirt front piece (action-layer demonstration)

This example builds a simple front shirt/bodice piece using only ops documented in
[`docs/action-layer-schema.md`](../../../docs/action-layer-schema.md), runs it for real through
`seamly2d-actiond`, and commits the resulting rendered PNG and `.val` pattern file. It is a
**demonstration/validation deliverable for the action layer's pipeline**, not a golden test — it
lives outside `tests/actionlayer/` and is not wired into `make check`.

## What it builds

A half-front bodice/shirt outline (center front on the straight left edge, symmetric other half
implied by mirroring, not built here), following a standard simplified front bodice block layout:
`A` is a top reference corner (the `basePoint` origin, *not* itself a garment point), with every
other point placed via `endLine` `length`/`angle` **formulas that reference body measurements**
loaded from [`measurements.smis`](measurements.smis) — not literal placeholder numbers — plus
straight `line`s for the shoulder, side seam, hem, and center-front edges, two `spline` curves for
the neckline and armhole, and one `piece.addPatternPiece` closing the six garment points into a
single named piece (`"Front"`) with a seam allowance.

## Body measurements ([`measurements.smis`](measurements.smis))

Five measurements, loaded via `{ "op": "measurements.load", "path": "...measurements.smis" }` as
the script's first action. Names are the standard Seamly2D/SeamlyMe measurement names (see
`src/libs/vpatterndb/measurements_def.cpp`), not invented ones, so the file loads cleanly against
Seamly2D's own known-name validation:

| Measurement name | Value (cm) | Meaning |
|---|---|---|
| `neck_circ` | 38 | Neck circumference |
| `bust_circ` | 96 | Bust/chest circumference |
| `shoulder_length` | 13 | Shoulder seam length |
| `shoulder_tip_to_armfold_f` | 21 | Shoulder tip to armhole fold, front (used as the armhole/scye depth) |
| `neck_side_to_waist_f` | 42 | Neck side to waist, front (used as the total center-front bodice length) |

These are illustrative adult-size values, not tied to a specific person.

**Derived formulas** (every `endLine`'s `length` in [`action.json`](action.json) is one of these,
evaluated by Seamly2D's own formula engine — not pre-resolved by the action layer):

| Point | Formula | Meaning |
|---|---|---|
| `A1` (`A`→`A1`) | `neck_circ/6+15` | Front neck drop = neck width + a fixed 15mm-equivalent extra drop |
| `A2` (`A`→`A2`) | `neck_circ/6` | Neck width, the classic "1/6 of neck circumference" shirt-drafting rule (matches `src/app/share/samples/patterns/male_shirt.sm2d`'s own `#NecWidth = neck_circ/6`) |
| `A3` (`A2`→`A3`) | `shoulder_length` | Shoulder seam, direct measurement, at a fixed -20° slope |
| `A4h` (`A`→`A4h`) | `bust_circ/4+20` | Quarter-bust helper width + a fixed ease allowance |
| `A4` (`A4h`→`A4`) | `shoulder_tip_to_armfold_f` | Armhole (scye) depth, direct measurement |
| `A5` (`A4`→`A5`) | `neck_side_to_waist_f-shoulder_tip_to_armfold_f` | Side-seam length = total front length minus armhole depth |
| `A6` (`A`→`A6`) | `neck_side_to_waist_f` | Total center-front bodice length, direct measurement |

Changing a value in `measurements.smis` and re-running the script reshapes the whole piece — the
point layout is driven entirely by these five numbers plus two fixed drafting constants (the -20°
shoulder slope and the two small ease/drop offsets above), not by hard-coded point coordinates.

**Points**, all relative to `A = (0, 0)`:

| Point | Role | Definition |
|---|---|---|
| `A` | Top reference corner (not a garment point) | `basePoint` origin |
| `A1` | Center-front neck point | straight down from `A` |
| `A2` | Neck/shoulder point | straight right from `A` |
| `A3` | Shoulder tip | from `A2`, at -20° below horizontal |
| `A4h` | Helper point (not a garment point) | straight right from `A` — top of the side-seam vertical guide |
| `A4` | Underarm point | straight down from `A4h` |
| `A5` | Side-seam hem point | straight down from `A4` |
| `A6` | Center-front hem point | straight down from `A` |

`A1` and `A2` are deliberately placed via two *different* relations from the shared corner `A`
(a vertical drop vs. a horizontal offset) rather than one chained from the other — that separation
is what gives the neckline curve room to actually read as a curve, distinct from the shoulder line
below it. Likewise `A4` is set independently on its own vertical guide (`A4h`) rather than derived
directly from `A3`, giving the armhole curve room to bulge between `A3` and `A4` instead of
hugging a near-straight hop.

Edges: `spline A1-A2` (neckline, concave, tangent horizontal at `A1` — perpendicular to the
straight center-front edge, so a mirrored copy would meet without a cusp — and rotated back toward
`A2` at the other end so the curve stays tucked under the shoulder line, not poking above it),
`line A2-A3` (shoulder), `spline A3-A4` (armhole, curving inward from the shoulder tip — concave
toward center front, hollowing into the body silhouette rather than bulging out past the side-seam
guide — then swinging back to meet `A4` with a near-vertical tangent, flush with the side seam
below it), `line A4-A5` (side seam), `line A5-A6` (hem), `line A6-A1` (center front) — then
`piece.addPatternPiece` closes `["A1","A2","A3","A4","A5","A6"]` into one piece.

## Running it

From the repo root, with a built `actiond.exe` and Qt's `bin/` directory on `PATH` (see
`tests/actionlayer/README.md` for the one-time build steps):

```
out\src\app\actiond\bin\actiond.exe --pattern tests\actionlayer\fixtures\patterns\blank.val --measurements examples\actionlayer\shirt_front_piece\measurements.smis --actions examples\actionlayer\shirt_front_piece\action.json
```

This is one-shot mode: it loads the empty `blank.val` fixture, runs every action in
`action.json`, prints the JSON result to stdout, and exits. The script's own
`render.snapshot`/`session.save` actions carry explicit relative paths
(`examples/actionlayer/shirt_front_piece/...`), so run the command from the repo root as shown —
one-shot mode resolves those paths against the process's current working directory, not the
actions file's location.

**The `--measurements` flag is required**, even though `action.json`'s own first action is
`{ "op": "measurements.load", "path": "...measurements.smis" }`: `measurements.load` only verifies
that a *later* file still matches the measurement type (`individual`/`multisize`) already
established for the process, it doesn't establish that type itself — that happens once, from the
CLI's own initial `--pattern`/`--measurements` load (see `measurements_sync_handlers.cpp`'s own
comment on this). Against a pattern with no measurement type set yet (`blank.val`'s empty
`<measurements/>` tag), running the script's `measurements.load` action alone hits a
`measurementTypeMismatch` error ("expected": "unknown", "actual": "individual"). Passing the same
file via `--measurements` up front avoids that: it establishes `individual` as the process's
measurement type before any action runs, so the in-script `measurements.load` (kept so the script
stays self-documenting about which measurements it needs) then simply reloads the same,
already-matching file.

## Output files

- [`shirt_front_piece.png`](shirt_front_piece.png) — rendered draft view (600x800px), produced by
  the script's `render.snapshot` action, with `"showPointNames": true` so every point's label
  (`A`, `A1`, `A2`, ...) is visible in the image — useful for a demo/reference render like this one,
  where matching a rendered point back to the action script that created it matters. See
  `docs/action-layer-schema.md`'s `render.snapshot` entry for the full parameter.
- [`shirt_front_piece.val`](shirt_front_piece.val) — the saved pattern, Seamly2D's native XML
  format, produced by the script's final `session.save` action. Its formulas still reference the
  measurement names above, but `session.save` does not persist a link back to
  `measurements.smis` (the pattern's own `<measurements/>` tag stays empty) — reopening this file
  standalone (in the GUI or via `actiond` without `--measurements`) will not resolve those formulas
  on its own. Reopen it the same way it was built: with `measurements.smis` loaded alongside it.
- [`measurements.smis`](measurements.smis) — the body measurements the whole draft is derived
  from (see table above). SeamlyMe/Seamly2D individual-measurement XML format.

Both generated files are committed as real output of an actual `actiond` run against
[`action.json`](action.json) — not hand-authored.

## Notes / caveats

- **The -20° shoulder slope and the two small ease/drop constants (`+15`, `+20`) are illustrative
  drafting constants**, not measurements — they demonstrate mixing a literal constant into an
  otherwise measurement-driven formula, same as real shirt drafts do (see the `+1`/`+5`-style ease
  constants throughout `src/app/share/samples/patterns/male_shirt.sm2d`'s own `<increments>`).
- **The piece's neckline and armhole edges are straight, not curved**, even though the *draft*
  view (the rendered PNG) shows real `spline` curves for both. This is a genuine current gap, not
  a design choice: `piece.addPatternPiece`'s `"nodes"` array only accepts point names — arc/curve
  piece nodes (`NodeArc`/`NodeElArc`/`NodeSpline`/`NodeSplinePath`) are not implemented yet (see
  `docs/action-layer-schema.md`'s Pieces section and its coverage-summary table). So the piece
  polygon itself connects `A1-A2` and `A3-A4` with straight edges, even though the same two point
  pairs are also joined by real spline curves in the draft. `render.snapshot`'s `target` parameter
  only ever renders the draft scene (`"draft"` is the only accepted value today), which is why the
  committed PNG shows the curves at all.
- **GUI load verification**: a display was available in the environment this was built in, so the
  unmodified interactive `seamly2d.exe` binary was launched directly against the committed
  `.val` file and ran without an immediate crash/exit. No automated screenshot of that GUI window
  was captured for a pixel-level comparison against the rendered PNG, so a person reviewing this
  should still open `shirt_front_piece.val` in Seamly2D themselves (alongside `measurements.smis`,
  per the caveat above) to confirm it loads cleanly and the piece looks as expected.

## Findings from earlier versions

The first version of this example (built for the original task) rendered with two visible defects:
the neckline curve was nearly coincident with the shoulder line, and the armhole curve was a thin
sliver barely distinguishable from a straight line. Root cause, diagnosed by computing exact point
coordinates from that version's `action.json`: the neck/shoulder point was derived as a small
downward-right offset *chained from* the center-front neckline point itself (rather than the two
being placed via two separate relations from a shared, non-garment reference corner), leaving the
three points `A`/`N`/`S` only 11% off from perfectly collinear — no spline tangent tuning could
make a curve between two nearly-collinear points read as a distinct dip. Separately, the armhole
spline's tangent angles were within ~14° of the chord's own bearing (i.e., nearly "flat"/no-bulge
tangents), which combined with a modest handle length to produce almost no visible curvature.

The second version (all-literal proportions, no measurements) fixed that collinearity problem but
introduced a new one: the neckline spline's `point4` (`A2`) tangent angle pointed the curve's
handle *upward*, which pulled the curve above the top A-A4h guide line right before it reached
`A2` — a visible hump poking outside the garment's own top edge. This version's neckline spline
instead uses a `point4` tangent angle (`250°`) that points down-and-back, keeping the curve tucked
under the top edge for its whole length, matching the tangent-direction convention used by real
curves in `src/app/share/samples/patterns/male_shirt.sm2d` (e.g. its own front-neckline spline,
`id="42"`, uses a comparably "pointing back along the incoming direction" `angle2` of `267°`).
Both prior issues were data/parameter problems in the action script, not a bug in the
`spline`/`endLine` handlers.

The third version fixed a shape problem, not an overshoot: that armhole spline's tangents
(`angle1: -15`, `angle2: 115`) made the curve bulge *outward* from the shoulder tip — bowing past
the `A4h`-`A4` side-seam guide line before hooking back in to `A4` — the opposite of a real
armhole, which hollows *inward* into the body silhouette. Fixed by steepening `point1`'s (`A3`)
departure angle well past the chord's own bearing (`-110`, versus the chord's roughly `-66°`) so
the curve heads almost straight down first, pulling it inside the chord line instead of outside
it, then rejoining `A4` on a near-vertical tangent (`angle2: -90`) flush with the side seam below.
