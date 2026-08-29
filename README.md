# Robi — Pebble Time 2 watchface

A robot-pet watchface for Pebble Time 2 (and Time/Time Steel/Round), inspired
by "Pip", but with a boxy little robot head instead of just eyes, plus:

- Time (big digits)
- Day name + date at the bottom
- Step count with progress toward a daily goal
- Weather (temperature) fetched from OpenWeatherMap via your phone
- Tap the watch to make the robot blink and hop

## 1. Add your weather API key

1. Get a free key at https://openweathermap.org/api
2. Install the watchface, then open it in the Pebble phone app and tap
   **Settings** — this opens Robi's config page, where you can paste the
   key (and pick Celsius/Fahrenheit) without touching any code.

Until you do this, the watchface still works fine — it just shows `--°`
for weather.

## 2. Set your step goal (optional)

Open `src/c/main.c` and change:

```c
#define STEP_GOAL 10000
```

to whatever your daily step target is.

## 3. Build it

The easiest path is **CloudPebble** (no local toolchain needed):

1. Go to https://cloudpebble.net and log in with your Rebble account
   (Pebble's original cloud services were shut down; Rebble runs the
   community replacement — cloudpebble.net and the Rebble app store
   shown in your screenshots are part of that).
2. Create a new project → "Import" → upload this whole folder as a zip.
3. Click **Build**, then **Install** to push it to your phone/watch over
   the Pebble app.

Or locally with the Pebble SDK / pebble-tool (also via Rebble's
rebble-sdk / `pebble` CLI):

```bash
pebble build
pebble install --phone <your-phone-ip-or-emulator>
```

## Robot behavior

The robot now reacts to what you're doing, in priority order:

1. **Goal reached** — plays once per day the moment you cross `STEP_GOAL`
   (confetti + big happy eyes), then settles back down. Also replays as a
   little "make a wish" moment at 11:11, same spirit as the original Pip.
2. **Running** — detected via Pebble Health's current-activity flag; quick
   bouncy animation.
3. **Walking** — also from Pebble Health; gentle rhythmic bob.
4. **Stairs up / down** — Pebble Time 2 has no barometer or floor counter,
   so this is a best-effort heuristic built from sudden vertical
   accelerometer swings *while you're already registered as walking*. It
   works reasonably well but isn't as reliable as the other states — treat
   it as a fun approximation rather than exact stair-counting.
5. **Idle reactions** — when you're not moving, the robot randomly reflects
   conditions: squints in sun, shivers/holds up against rain, droops
   sleepy-eyed late at night. These come from the weather condition string
   sent from your phone and the watch's own clock, so no extra setup is
   needed beyond the API key from step 1.
6. **Idle activities** — otherwise, instead of standing still, the robot
   randomly cycles through a few time-of-day activities every 10-19
   seconds: reading a book or working at a laptop during the day, eating
   around lunchtime (12:00-13:59), or just a plain idle pose the rest of
   the time. It occasionally comments on what it's doing in a small speech
   bubble.

## Shake to greet you

Give the watch a firm shake and it reacts: a speech bubble pops up with a
random greeting — "Hi!", "Hello!", or "Hoi!" — and it raises an arm in a
wave, then settles back down after about 1.8 seconds.

**Important:** Pebble watches don't have a touchscreen — Pebble Time 2 is
buttons + accelerometer. On this hardware, the accelerometer's tap
detector needs a wrist-shake-level hit to register at all — a normal
finger tap/knock on the case doesn't reliably cross its threshold, so
shake is the supported gesture rather than a tap or double-tap. The HTML
preview simulates this with a double-click on the screen since a browser
has no equivalent gesture.

**Security note:** the weather key is entered through the phone app's
Settings page and stored in the watchface's local storage on your phone —
it is never baked into the source or the compiled `.pbw`, so it's safe to
publish this project's code publicly.

## Character design

The robot moved from flat gray blocks with a uniform cyan outline to a
shaded, toy-robot look, built from a few pixel-art tricks that work well on
e-paper (no real gradients or blur available):

- **Two-tone panel shading** — each body part (head, torso, arms, legs) is a
  dark base shape with a lighter rounded panel over its top portion,
  faking a light source from above instead of looking flat.
- **A visor**, not floating eyes — the eyes now sit inside a recessed dark
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
- Accent cyan is now reserved for *functional* elements (eyes, chest light,
  cuffs) instead of outlining every single shape, which is what was making
  it read as "primitive" before — outlining everything in the same color
  flattens the depth cues instead of adding them.

## Scenery & motion quality

- **Walking / running** — a road with scrolling dashed lane markings appears
  under its feet, scrolling faster while running. Limb motion uses a
  continuous sine wave (`sine_wave()` in `main.c`, built on Pebble's
  `sin_lookup`) instead of a blocky two-frame flip, so it reads as an actual
  swing rather than a twitch.
- **Stairs up / down** — instead of sliding the whole robot across a fixed
  flight of steps (which had to snap-reset once it ran off screen), the
  robot now stays in place, leans, and steps in rhythm while a diagonal
  striped "treadmill" background scrolls behind it. Same idea as the road,
  just on a diagonal — it loops forever with no visible jump.
- **Sleepy (night)** — instead of standing, the robot lies down on a small
  drawn bed (frame, mattress, pillow, blanket) with closed eyes and floating
  "z"s. This is a separately-drawn scene rather than a rotated standing
  robot, since Pebble's graphics API doesn't cleanly rotate composite
  rounded-rect shapes — full lying-down rotation would need custom vector
  paths for every body part, which isn't worth the CPU/battery cost on
  e-paper for a cosmetic flourish.

A note on expectations: the browser preview (`robi-preview.html`) runs at
a full 60fps with gradients, shadows, and particle physics because a browser
has no meaningful power budget. The real watch redraws only while something
is actually animating, at a modest ~10 frames/sec, using flat colors and
simple shapes — that's a deliberate trade for battery life on an always-on
e-paper display, not a corner cut. The motion curves (sine-based swings,
scrolling treadmills) are the same *idea* in both places, just budgeted
differently.

## Notes

- Steps require Health permission — you'll be prompted the first time it
  runs; Health data comes straight from the watch, no phone needed.
- Weather requires your phone's Pebble app to be connected (it does the
  actual network request on the watchface's behalf) and location
  permission granted to the Pebble app.
- Color theme is cyan to match the look in your screenshots — change
  `ACCENT_COLOR` near the top of `src/c/main.c` (it's a hex color) if you
  want green/amber/pink/custom instead.
