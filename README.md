# Robi — Pebble Time 2 watchface

A robot-pet watchface for Pebble Time 2 (emery), inspired by "Pip", with a
boxy little robot head instead of just eyes, plus:

- Time (big digits) and day/date
- Step count with a pixel-filled progress bar toward a daily goal
- Weather (temperature + a live sun/cloud/rain/snow/fog/storm icon) fetched
  automatically via your phone — no API key needed
- Shake the watch to make the robot wave and greet you
- A whole cast of idle behavior: reading, working, eating, cycling,
  wandering around, and occasionally wandering off-screen and back

## Build it

```bash
pebble build
pebble install --emulator emery      # test in the QEMU emulator
pebble install --phone                # or push to a paired phone/watch
```

## Weather — zero setup

Weather comes from [Open-Meteo](https://open-meteo.com/) via
`src/pkjs/index.js`, using your phone's location — no API key, no signup,
no settings page. It refreshes automatically every 30 minutes and also
whenever the watchface starts up.

The condition is shown two ways:
- As text next to the temperature (`22°`)
- As a small icon in the top-right corner, always visible regardless of
  what Robi is currently doing — sun for clear skies, a cloud for
  overcast, a cloud with drops for rain/drizzle, a cloud with snowflakes,
  fog lines, or a cloud with a lightning bolt for storms.

## Steps

Requires Health permission (you'll be prompted the first time it runs;
step data comes straight from the watch, no phone needed). The bar under
the time fills up as you approach `STEP_GOAL` (10000 by default — change
the `#define` near the top of `src/c/main.c` to adjust it), with the raw
step count shown underneath.

## Robot behavior

In priority order:

1. **Goal reached** — plays once per day the moment you cross `STEP_GOAL`
   (confetti + big happy eyes), then settles back down. Also replays as a
   little "make a wish" moment at 11:11.
2. **Running / walking** — detected via Pebble Health's current-activity
   flag; a bouncy run or a gentle rhythmic walk, road scrolling underfoot.
3. **Sleepy (night, 22:00–06:00)** — instead of standing, the robot lies
   down on a small drawn bed with closed eyes and floating "z"s.
4. **Weather mood** — when otherwise idle, the robot's eyes/posture react
   to current conditions (squints in sun, lowers in rain, shivers in
   snow), on top of the persistent weather icon described above.
5. **Idle activities** — otherwise, the robot cycles through a handful of
   behaviors every 10–19 seconds instead of standing still: reading a
   book or working at a laptop during the day, eating around lunchtime,
   hopping on a bicycle for a ride, wandering sideways to a new spot, or
   occasionally walking off one edge of the screen, **disappearing
   entirely** for a few seconds, and walking back in from the other side.
   It occasionally comments on what it's doing in a small speech bubble.

## Shake to greet you

Give the watch a firm shake and it reacts: a speech bubble pops up with a
random greeting — "Hi!", "Hello!", or "Hoi!" — and it raises an arm in a
wave, then settles back down after about 1.8 seconds.

**Important:** Pebble watches don't have a touchscreen — Pebble Time 2 is
buttons + accelerometer. On this hardware, the accelerometer's tap
detector needs a wrist-shake-level hit to register at all — a normal
finger tap/knock on the case doesn't reliably cross its threshold, so
shake is the supported gesture rather than a tap or double-tap.

## Character design

The robot uses a shaded, toy-robot look built from a few pixel-art tricks
that work well on e-paper (no real gradients or blur available):

- **Two-tone panel shading** — each body part (head, torso, arms, legs) is
  a dark base shape with a lighter rounded panel over its top portion,
  faking a light source from above instead of looking flat.
- **A visor**, not floating eyes — the eyes sit inside a recessed dark
  visor strip on the head, like a HUD, rather than glowing directly on the
  body color.
- **Ball-joint shoulders and hips** — small dark circles sit at the torso's
  arm/leg attachment points so the limbs read as articulated rather than
  just stuck on.
- **Cuffs and caps** — a thin accent band plus a rounded end-cap near the
  bottom of each arm and leg, instead of limbs that just end abruptly.
- **Chest detail** — a bright core + dim halo (two flat circles standing in
  for a glow, since e-paper can't blur) inside a light panel, plus a couple
  of vent marks.
- Accent cyan is reserved for *functional* elements (eyes, chest light,
  cuffs, weather icon) instead of outlining every shape, which keeps the
  depth cues from flattening out.

## Motion quality

- **Walking / running / cycling** — a road with scrolling dashed lane
  markings appears underfoot, scrolling faster while running. Limb motion
  uses a continuous sine wave (`sine_wave()` in `main.c`, built on
  Pebble's `sin_lookup`) instead of a blocky two-frame flip, so it reads
  as an actual swing rather than a twitch.
- **Wandering / disappearing** — idle drift and the off-screen "away"
  sequence ease the robot's horizontal position a couple of pixels per
  animation frame rather than jumping, and reuse the same walking swing
  while on the move.
- **Sleepy (night)** — the robot lies down on a small drawn bed (frame,
  mattress, pillow, blanket) with closed eyes and floating "z"s, drawn as
  its own scene rather than a rotated standing robot, since Pebble's
  graphics API doesn't cleanly rotate composite rounded-rect shapes.

The watch redraws only while something is actually animating, at a modest
~10 frames/sec using flat colors and simple shapes — a deliberate trade
for battery life on an always-on e-paper display.

## Notes

- Target platform is **emery** (Pebble Time 2, 200×228 color).
- Weather requires your phone's Pebble app to be connected (it does the
  actual network request on the watchface's behalf) and location
  permission granted to the Pebble app.
- Color theme is cyan — change `ACCENT_COLOR` near the top of
  `src/c/main.c` (it's a hex color) if you want green/amber/pink/custom
  instead.
