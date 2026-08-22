# `piece_groups` — real multi-piece pattern example

Demonstrates `piece.list` and `render.snapshot`'s `target: "piece"` (see
[`docs/action-layer-schema.md`](../../../docs/action-layer-schema.md)) against a real,
interactively-authored, multi-piece pattern instead of a from-scratch single-piece fixture.

## Files

- `Aldrich-Womens-6th-Ed-Basic-Blocks.sm2d` — the pattern draft: 7 pieces (skirt back/front,
  trousers front/back, bodice back/front, a one-piece sleeve), several with real `NodeArc`/
  `NodeSpline` main-path nodes alongside plain point nodes.
- `Aldrich-Womens-MultiSize-06-14.smms` — the paired multisize measurement file.
- `actions.json` — the action-layer script run against the two files above: one `piece.list`
  (discover every piece — name, id, node count, seam allowance) followed by one
  `render.snapshot {"target": "piece", "piece": "<name>"}` per piece, each cropped tight to that
  one piece's own outline.
- `output/response.json` — the actual JSON response from the command below, pretty-printed.
- `output/*.png` — the eight rendered snapshots `actions.json` produces (one per piece).

**Provenance/licensing note:** these two fixture files were added to this repository outside of
this example's own commit; Aldrich is a long-published, widely reproduced drafting-book block set,
but this was not independently re-verified — treat `output/` here as a demonstration of the
action-layer feature working against a real file, not a licensing determination.

## Reproducing this

From the repository root, after building `actiond` (see `build_actiond.bat` / the top-level
`README`/`docs/ARCHITECTURE.md`):

```
cd examples/actionlayer/piece_groups
../../../out/src/app/actiond/bin/actiond.exe ^
  --pattern Aldrich-Womens-6th-Ed-Basic-Blocks.sm2d ^
  --measurements Aldrich-Womens-MultiSize-06-14.smms ^
  --actions actions.json ^
  > output/response.json
```

(`actiond.exe`'s own `bin/` directory is self-contained — `windeployqt` already copied every Qt
DLL it needs there, so nothing needs to be added to `PATH` first.) `render.snapshot`'s own `"path"`
values in `actions.json` are relative to `actiond`'s working directory, hence running from inside
this folder so the `output/*.png` files land here.

To inspect just the piece listing on its own, write a one-line actions file containing
`{"actions":[{"op":"piece.list"}]}` and pass it as `--actions` the same way as above.
