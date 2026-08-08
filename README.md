# Pip-Bot — Pebble Time 2 watchface

A robot-pet watchface for Pebble Time 2 (and Time/Time Steel/Round), inspired
by "Pip", but with a boxy little robot head instead of just eyes, plus:

- Time (big digits)
- Day name + date at the bottom
- Step count with progress toward a daily goal
- Weather (temperature) fetched from OpenWeatherMap via your phone
- Tap the watch to make the robot blink and hop

## 1. Add your weather API key

1. Get a free key at https://openweathermap.org/api
2. Open `src/pkjs/index.js`
3. Replace `PASTE_YOUR_OPENWEATHERMAP_KEY_HERE` with your key

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

## Double-tap to greet you

Quickly tap the watch twice (physically knock it, the way you'd already
tap it once to wake the robot) and it reacts. With an Anthropic API key
configured (see below), it asks Claude for a short, situational one-liner
based on the time of day, current weather, and your steps so far — a
different line each time instead of a fixed script. Without a key, or if
the request doesn't come back in time, it falls back to one of two
built-in reactions chosen at random:

- **Says "Hi!"** — a speech bubble pops up and it raises an arm in a wave.
- **Just smiles** — no bubble, its mouth curves into a big smile and its
  eyes brighten for a moment, then it settles back down.

Either way it settles back down after 1.8–2.8 seconds.

**Important:** Pebble watches don't have a touchscreen — Pebble Time 2 is
buttons + accelerometer. "Double tap" here means physically tapping/
knocking the watch case twice quickly, the same gesture the original Pip
uses for its single-tap reactions. The HTML preview simulates this with a
double-click on the screen since a browser has no equivalent gesture.

## Optional: AI-generated greetings (Anthropic API)

1. Get a developer API key at https://console.anthropic.com — **this is
   separate from a claude.ai account.** It's not "signing in with Claude,"
   it's a billed developer API key with no access to any chat history or
   memory. Each request is a fresh, one-off call with only the context this
   code sends it (time, weather, steps) — the robot doesn't "remember" you
   between taps.
2. Open `src/pkjs/index.js` and paste your key in place of
   `PASTE_YOUR_ANTHROPIC_API_KEY_HERE`.
3. That's it — double-tapping now asks for a short greeting instead of
   using the built-in one. Leave the placeholder in place to skip this
   entirely; nothing else changes.

**How it works:** the watch sends your current step count and the hour to
the phone over Bluetooth (weather it already knows from its own periodic
fetch). The phone calls the Anthropic API with a short prompt built from
that context and asks for one line under 30 characters, then relays it
back to the watch to show in the speech bubble. If nothing comes back
within 4 seconds — no signal, no key, API hiccup — the watch just falls
back to its normal "Hi!"/smile reaction on its own; nothing hangs or
breaks.

**Cost:** a prompt and reply this short costs a small fraction of a cent
on the model used (`claude-haiku-4-5-20251001`). Even frequent double-taps
all day would run pennies a month, not dollars.

**Security note:** the key sits in plaintext in `index.js`, the same way
the OpenWeatherMap key does. That's fine for your own personal build, but
don't publish this project publicly (GitHub, app stores, etc.) with a real
key baked in — anyone who gets the file gets the key and can spend your
API credits.

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

A note on expectations: the browser preview (`pip-bot-preview.html`) runs at
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
