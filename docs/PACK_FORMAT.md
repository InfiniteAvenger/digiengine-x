# Pack format (format 1)

A pack is a folder containing `pack.json` plus the sprite files it references.
Drop it into `packs/` and point the game at it.

```
packs/my_pack/
  pack.json
  sprites/
    egg.spr
    blob.spr
```

## pack.json

```json
{
  "format": 1,
  "id": "demo",
  "name": "Demo Pack",
  "version": "0.1.0",
  "author": "you",
  "license": "CC0-1.0",
  "theme": { "bg_top": "9bd4ff", "bg_bottom": "6bbf59",
             "ui": "1a1c2c", "ui_text": "f4f4f4", "accent": "ffcd75" },
  "rosters": [
    { "id": "demo_v1", "name": "Demo Ver.1", "eggs": ["pixegg"] }
  ],
  "species": [
    { "id": "pixegg", "name": "Pixegg", "stage": "egg",
      "sprite": "sprites/pixegg.spr",
      "evolve": [ { "to": "blip", "after_minutes": 0.5 } ] }
  ]
}
```

| Field                 | Meaning                                                  |
|-----------------------|----------------------------------------------------------|
| `format`              | Must be `1`.                                             |
| `theme`               | Hex `rrggbb` colors for the home screen and menus.       |
| `rosters`             | Selectable "device versions". `eggs` lists starter IDs.  |
| `species[].stage`     | `egg`, `baby1`, `baby2`, `child`, `adult`, `perfect`, `ultimate`, `super_ultimate` (old `infant`/`baby` load as `baby1`) |
| `species[].sprite`    | Path to a sprite file, relative to the pack folder.      |
| `species[].evolve`    | Rules checked in order; the first one that passes wins.  |
| `species[].family`    | Ecological family (`beast`, `machine`, `plant`, `dark`, ...) used for quest drops |
| `species[].attribute` | One of the pack's `attributes` (see below); blank = neutral |
| `species[].care`      | Optional. How fast needs change; see below.              |

### care

Every field is optional; missing ones use the defaults for the species' stage.

| Field              | Meaning                                         | Child default |
|--------------------|-------------------------------------------------|---------------|
| `hunger_minutes`   | Awake minutes per hunger heart lost             | 48            |
| `strength_minutes` | Awake minutes per strength heart lost           | 48            |
| `poop_minutes`     | Awake minutes between poops                     | 120           |
| `sleep`, `wake`    | Local bedtime and wake time, `"HH:MM"`. Equal values mean the creature never sleeps. | 21:00 / 08:00 |
| `weight`           | Base and minimum weight in grams                | 20            |
| `hearts`           | Size of the hunger and strength meters (1-7)    | 2             |

Stage defaults (hunger / strength / poop minutes, bedtime, weight):

| Stage    | Needs (min) | Bedtime     | Weight |
|----------|-------------|-------------|--------|
| baby1    | 3 / 3 / 3   | 20:00-09:00 | 5      |
| baby2    | 30 / 30 / 60 | 20:00-09:00 | 10    |
| child    | 48 / 48 / 120 | 21:00-08:00 | 20   |
| adult    | 59 / 59 / 120 | 22:00-08:00 | 30   |
| perfect  | 66 / 66 / 120 | 23:00-09:00 | 40   |
| ultimate | 75 / 75 / 120 | 23:00-09:00 | 50   |
| super_ultimate | 85 / 85 / 150 | 23:00-09:00 | 60 |

### Combat fields on a species

```json
"stats":  { "hp": 20, "atk": 5, "def": 4, "spd": 5 },
"learns": [ { "move": "tackle", "level": 1 }, { "move": "bubble", "level": 2 } ]
```

Stats are level-1 values and grow 10% per level. Missing stats use stage
defaults. The three most recently learned moves are equipped.

## moves

```json
{ "id": "tackle", "name": "TACKLE", "power": 10, "game": "timing" }
```

`game` picks the charge minigame when the player uses it: `timing` (stop the
meter in the gold zone), `mash` (press A/B fast), `sequence` (repeat 4 buttons)
or `dice` (roll for luck). Each gives a multiplier of about x0.5 to x1.5.
Enemies use moves too; for them `game` is ignored.

## enemies

```json
{ "id": "cogbot", "name": "COGBOT", "sprite": "sprites/cogbot.spr", "boss": false,
  "stats": { "hp": 22, "atk": 4, "def": 5, "spd": 3 },
  "moves": ["gear_toss"],
  "drops": [ { "item": "scrap", "chance": 0.6 } ] }
```

Stats are for floor 1 and grow 15% per floor. Every 10th floor uses a `boss`
enemy (with +50% HP). Drop chances rise 2% per floor. Make drops fit the
creature: beasts drop meat and hide, machines drop scrap and circuits, plants
drop vines and seeds.

## materials and items

Two lists sharing one id space. Materials are ingredients only; items are used.

```json
"materials": [ { "id": "berry", "name": "BERRY", "rarity": "common", "info": "SWEET GARDEN BERRY." } ],
"items":     [ { "id": "patch", "name": "PATCH", "rarity": "common", "info": "HEALS 50% HP.",
                 "effect": { "heal": 50 } } ]
```

`rarity`: `common`, `uncommon`, `rare`, `epic`. `icon` is a frame number in
the pack's icon sheet, set at the top level with
`"item_icons": "sprites/items.spr"` (a sprite file of 8x8 frames). Items
without an icon just show their name. `effect` fields:

| Field                     | Where  | Meaning                               |
|---------------------------|--------|---------------------------------------|
| `hunger`, `strength`      | home   | Hearts added                          |
| `cure`                    | home   | `true` cures sickness                 |
| `hp`, `atk`, `def`, `spd` | home   | Permanent stat bonus (chips)          |
| `heal`                    | quest  | Percent of max HP restored            |
| `atk_buff`, `def_buff`    | quest  | Floors of +25% attack / defense       |

## recipes

```json
{ "makes": "berry_jam", "count": 1, "needs": { "berry": 3 } }
```

## crops

```json
{ "name": "BERRY BUSH", "seed": "data_seed", "yields": "berry", "count": 3,
  "minutes": 20, "sprite": "sprites/crop_berry.spr" }
```

Crop sprites are 12x12 with exactly 4 frames: planted, sprout, growing, ripe.
Harvesting has a 50% chance to return a seed.

## start_items

```json
"start_items": { "data_seed": 3, "patch": 2 }
```

Given to every new egg.

Unknown fields are ignored, so packs can add fields for future features early.

## PNG sprites

Anywhere a sprite path is accepted, a `.png` works too. Frames are read left
to right (or as a grid, row by row) and default to squares as tall as the
image; add `"frame_size": [w, h]` to the species, enemy or crop for other
sizes. Pixels under 50% alpha are transparent; up to 255 colors.

To build a whole pack from PNGs and spreadsheets, see `tools/baker/`.

## Sprite files (.spr)

A plain-text, diff-friendly format. PNG sprite sheets will be supported by the
baker tool later; `.spr` stays available for hand-made and generated art.

```
# Lines starting with # are comments.
size 16 16
palette
k 1a1c2c
w f4f4f4
frame
................
......kkkk......
...(16 rows of 16 characters)...
frame
...(next animation frame)...
```

- `size W H`: frame size in pixels. Creatures are 16x16.
- `palette`: one entry per line, a single character then a hex color.
  `.` is always transparent and `#` is reserved, so neither can be listed.
- `frame`: followed by exactly `H` rows of exactly `W` palette characters.
  Repeat for each animation frame. Frame 0 and 1 are the idle animation.

## Evolution rules

```json
"evolve": [
  { "to": "sprocket", "hours_in_stage": 0.25, "max_care_mistakes": 0, "min_effort": 1 },
  { "to": "mudlump",  "hours_in_stage": 0.25 }
]
```

Rules are tried top to bottom every second; the **first** whose conditions all
hold wins. If none hold, the creature stays in its form and keeps checking, so
put a fallback with only a time last if it should always evolve.

| Field               | Condition                                              |
|---------------------|--------------------------------------------------------|
| `hours_in_stage`    | Time in the current form (`after_minutes` also works)  |
| `max_care_mistakes` | Care mistakes made **in this form** at most this        |
| `min_level`         | Pet level at least this                                 |
| `min_effort`        | Successful trainings **in this form** at least this     |
| `min_quest_floor`   | Best quest floor ever cleared at least this             |

Mistakes and effort in the current form reset when the creature evolves.

## Quest enemies, tiers and families

Creatures double as quest enemies automatically. Each creature whose stage
has a tier appears on that tier's floors, with its own art, stats and moves:

```json
"quest": {
  "species_enemies": true,
  "tiers": [
    { "stage": "child",    "min_floor": 1,  "max_floor": 9 },
    { "stage": "adult",    "min_floor": 10, "max_floor": 19 },
    { "stage": "perfect",  "min_floor": 20, "max_floor": 29 },
    { "stage": "ultimate", "min_floor": 30, "max_floor": 49 },
    { "stage": "super_ultimate", "min_floor": 40 }
  ]
}
```

These tiers are the default when `quest` is left out. Set `"enemy": false`
on a creature to keep it out of quests. Hand-written `enemies` still work and
may set `"min_floor"` / `"max_floor"`. Enemy stats grow 15% per floor above
the first floor they appear on.

Every 10th floor is a boss: a `"boss": true` enemy whose range covers the
floor, or, if there is none, a regular enemy promoted to boss (+50% HP).
Floors deeper than every range keep using the deepest tier that exists.

Drops come from shared family tables, so a big roster needs no per-creature
drop lists. An enemy's own `drops` override its family's.

```json
"families": {
  "beast":   { "drops": [ { "item": "meat",  "chance": 0.6 } ] },
  "machine": { "drops": [ { "item": "scrap", "chance": 0.6 } ] }
}
```

Set a creature's or enemy's `"family"` to one of these names.

## Lifespan and the Forever Partner

```json
"lifespan_days": 30
```

Creatures pass on of old age this many days after birth (0 or missing = never).
Time spent in cryo storage doesn't count. When the active pet passes on, a new
egg arrives; the bag, garden and cryo slots are kept.

An item with `"effect": { "forever": true }` makes the pet a Forever Partner:
it never passes on. The demo pack's FOREVER CHIP is crafted from 2 core
shards, 2 glow roots and 2 power cells.

## Attributes

A pack may define a rock-paper-scissors style table. A creature (or enemy)
whose attribute beats its target's deals **1.25x** damage; against an attribute
that beats it, **0.75x**. Anything else, including a blank attribute, is 1x.

```json
"attributes": {
  "sun":  { "beats": ["moon"] },
  "moon": { "beats": ["star"] },
  "star": { "beats": ["sun"] },
  "void": { "beats": [] }
}
```

When a pack defines attributes, every creature's and enemy's `attribute`
must be one of them (or blank). The status screen and battle HUD show it.
