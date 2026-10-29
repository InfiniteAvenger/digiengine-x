# vpet_baker

Builds a pack from a folder of PNG sprite sheets and two spreadsheets, so a
big roster doesn't mean hand-writing JSON.

```bash
vpet_baker build <source_dir> <pack_dir> [--placeholders]
vpet_baker png2spr <in.png> <out.spr> [frame_w frame_h]
vpet_baker spr2png <in.spr> <out.png>
```

`build` validates everything first and lists every problem with its file and
line. Nothing is trusted silently: unknown stages, missing or unreadable
sprites, duplicate ids and evolutions to unknown creatures are all errors.

## Source folder

```
my_pack_src/
  species.csv        required
  evolutions.csv     optional
  base.json          optional: theme, items, moves, enemies, crops, recipes...
  sprites/*.png      sprite strips (or .spr files)
```

See `sample/` for a complete, working example made from original CC0 art.

### species.csv

| Column | Meaning |
|---|---|
| `id` | Unique id (required) |
| `name` | Display name (defaults to id) |
| `stage` | `egg`, `baby1`, `baby2`, `child`, `adult`, `perfect`, `ultimate`, `super_ultimate` (required) |
| `attribute` | One of the pack's attributes (base.json), or blank |
| `family` | `beast`, `machine`, `plant`, `dark`, or any family the pack defines |
| `sprite` | Path to a PNG strip or `.spr` file (required) |
| `frame_w`, `frame_h` | Frame size, only for non-square frames |
| `hp`, `atk`, `def`, `spd` | Level-1 stats; blank uses stage defaults |
| `learns` | `move@level;move@level` |
| `hunger_minutes`, `strength_minutes`, `poop_minutes` | Care timers; blank uses stage defaults |
| `sleep`, `wake` | `HH:MM`; the same time for both means it never sleeps |
| `weight`, `hearts` | Base weight; meter size (1-5) |

PNG strips hold frames left to right (or a grid, read row by row). Frame 0
and 1 are the idle animation. Pixels under 50% alpha are transparent.

### evolutions.csv

| Column | Meaning |
|---|---|
| `from`, `to` | Species ids |
| `hours_in_stage` | Time in the current form before this rule can apply |
| `max_care_mistakes` | Mistakes made in this form must be at most this |
| `min_level` | Pet level at least this |
| `min_effort` | Successful trainings in this form at least this |
| `min_quest_floor` | Best quest floor at least this |

Rules for the same creature are tried top to bottom; the first whose
conditions all pass wins. Put the fallback (no conditions) last.

## Output

- `pack.json` with `base.json` merged in, plus generated `species` and (if
  `base.json` has none) a `rosters` entry hatching every `egg`.
- `sprites/<id>.spr` for each creature. A PNG with more than 92 colors is
  copied as-is instead (the engine loads PNGs directly too).
- Every other file `base.json` refers to, copied unchanged.

## Placeholders

`--placeholders` draws a stand-in sprite for any creature whose sprite file is
missing: a simple blob tinted by family, sized by stage, with the name's
initials. The pack is playable right away; drop real PNGs into the source
`sprites/` folder and re-bake to replace them one at a time. Without the
flag, a missing sprite is an error.

## Checking a pack

```bash
vpet_snapshot --validate <pack_dir>
```

Loads the pack through the engine and checks that every creature has a
sprite and moves, that quest floors 1-60 all find enemies, and that each
roster's egg hatches.
