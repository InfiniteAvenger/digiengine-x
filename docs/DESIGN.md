# Design

## Pillars

1. **One unified game.** A home screen modeled on 3-button v-pets (feed, clean,
   sleep, train, daily dice roll, evolve), plus an RPG quest/battle mode.
2. **Nothing hidden.** A status screen shows stats and care mistakes openly.
3. **Three buttons only.** A / B / C drive every screen on every platform.
4. **Small and cohesive.** Must fit an ESP32-S3: fixed 120x120 canvas, pixel
   art, lean data.
5. **Data-driven.** Creatures, evolution trees, items and rosters come from
   packs. The engine ships no third-party content.
6. **No money, no shops.** Everything is hunted, grown, cooked or crafted.

## Architecture

```
+-----------------------------+
|  engine/ (portable C++17)   |  game rules, scenes, pack loading, rendering
+-------------+---------------+  to a 120x120 RGB565 framebuffer
              | Platform interface (platform.h)
   +----------+-----------+-------------------+
   | platform/sdl (PC)    | platform/sdl      | platform/esp32
   |                      | (Android)         | (S3 + SPI LCD + 3 GPIO)
```

The `Platform` interface is the only way the engine touches the outside world:
clock, buttons, framebuffer present, file read/write (later: sound, link/net).

### Time-driven simulation

All pet state is computed from timestamps, never from "the app was running".
Opening the game after 8 hours replays those 8 hours instantly. This is
required because Android kills background apps and the ESP32 deep-sleeps.
Evolution already works this way (`simulate()` in `pet.h`): an evolution that
came due while closed is applied at the exact moment it was due.

### Determinism

Battles will use a seeded RNG so a fight is fully reproducible from its seed
and the button inputs. Link/online PvP then only needs to exchange inputs.

### Content pipeline

Packs are authored as JSON + sprite files (see `PACK_FORMAT.md`). The desktop
and Android builds load them directly. A future *baker* tool will convert a
pack into a compact binary (palette-indexed sprites, packed tables) that the
ESP32 reads straight from flash.

## Care rules (Phase 2)

- **Hunger / strength:** meters grow with each stage: 1 heart as an
  infant, 2 as a child, 3 adult, 4 perfect, 5 ultimate (packs can override). Hunger moves in half hearts, strength in
  whole hearts, each dropping on a per-form timer that only runs while awake.
  Kibble (free, unlimited) adds half a heart and 1g; food items add whole hearts.
  Training (stop the meter in the gold zone) adds a strength heart on a hit
  and burns 1g either way.
- **Calls:** an empty hunger or strength meter, or lights left on at bedtime,
  starts a call (the `!` icon). Ignoring a call for 10 minutes is one care
  mistake. Hunger and strength calls pause while asleep.
- **Poop:** one per timer, up to 4. The 4th makes the pet sick. Medicine (free)
  cures it; a sick pet can't train.
- **Sleep:** by local clock per form. The lights turn back on at wake-up.
- **Saves:** written after every action, every 60 seconds and on exit. Loading
  replays the time the game was closed (up to 30 days) one second at a time.
- **Status:** three pages show every stat, the exact countdown to each need
  changing, and how long each open call has before it becomes a mistake.

## Quest, garden and economy

- **Quest:** each floor is one battle and costs 1 hunger heart. HP carries
  between floors; the player can go home between floors and resume later.
  Fainting resets the run to floor 1 and the pet comes home sick, keeping its
  level, items and home. Every 10th floor is a boss.
- **Battle:** turn order by speed. Picking a move starts its minigame; the
  result (x0.5 to x1.5) scales damage `power * mult * atk / def / 2 + 1`.
  Using an item gives the enemy a free turn.
- **Growth:** XP per floor cleared; 20 x level XP per level; stats +10% per
  level; new moves by level, newest three equipped.
- **Garden:** 2x4 plots, real-time growth from the planting timestamp.
- **Economy:** no money, no shops. Kibble and medicine are free; everything
  better is grown, dropped by enemies, or cooked/crafted from those.

## Fixed numbers

| Thing                 | Value                                  |
|-----------------------|----------------------------------------|
| Canvas                | 120x120, RGB565 (2x on a 240x240 LCD)  |
| Creature sprites      | 16x16, drawn at 2x                      |
| Home menu slots       | 8                                       |
| Equipped moves        | up to 3                                 |
| Items / materials     | 16 / 16, rarity tiers C / U / R / E     |
| Garden                | 2x4 plots                               |

## Roadmap

| Phase | Goal                                                           | Status   |
|-------|----------------------------------------------------------------|----------|
| 0     | Repo, docs, pack schema, PC window with an idling demo pet     | **done** |
| 1     | Engine shell: scene stack, sprites, font, menus                | **done** |
| 2     | Home loop: hunger, strength, poop, sleep, care mistakes, saves | **done** |
| 3     | Early ESP32 port to prove memory/speed budget                  | compiles, untested on hardware |
| 4     | Evolution conditions, cold storage, Forever Partner item       | **done** |
| 5     | Infinite quest floors, moves, 4 attack minigames               | **done** |
| 6     | Ecological drops, garden, cooking, crafting                    | **done** |
| 7     | Android build, local link PvP, then online PvP                 |          |

## Open decisions

- ESP32 display panel (canvas assumes 240x240; others letterbox).
- ESP32 clock source: RTC with backup battery vs. Wi-Fi time sync.
- Project name and code license.
- Floor scaling formula for the infinite quest.

## Settings, packs and versions

- Hold **A+C for 3 seconds** on any screen to open settings. There is no icon.
- **Pack:** every folder in `packs/` is a pack; switching saves the current one,
  and each pack keeps its own save. The last pack used opens on launch.
- **Version:** a pack's `rosters`; new eggs hatch from the chosen one.
- **Moves:** equip any 1-3 learned moves (also from Status page 4). With
  hand-picked moves and a free slot, newly learned moves are equipped too.
- **Cryo:** two slots (snowflake icon); frozen pets' timers all pause.
