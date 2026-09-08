# Release Notes

## 1.8.0

- **Low-activity nudge** — if you're well behind pace on steps for the
  time of day, Robi gets a little impatient while idle (a foot-tap, and
  an occasional "let's get moving!").

## 1.7.0

- **Smarter shake reactions** — shake Robi while it's reading, working,
  eating, cycling, or sleeping and it now reacts to being interrupted
  ("lost my page!", "wait, working!") instead of the same generic
  greeting every time.

## 1.6.0

- **Dancing & stretching** — two rare idle easter eggs: every so often
  Robi breaks into a quick dance wiggle or a slow stretch instead of the
  usual reading/working/cycling rotation.

## 1.5.0

- **Step-goal streaks** — hitting your daily step goal now builds a
  streak day over day, and Robi calls it out ("Goal hit!" or "3-day
  streak!") that survives app restarts.

## 1.4.0

- **Calm mood** — when your heart rate is nice and low, Robi settles
  into a slower, sleepier sway with half-lidded eyes during idle
  activities, the relaxed counterpart to the excited hop.

## 1.3.0

- **Low-battery look** — when your watch battery is low and not
  charging, Robi visibly dims (eyes, antenna, chest light) and blinks
  less often, a subtle low-power cue built right into the character.

## 1.2.0

- **Excited hop** — when your heart rate spikes (same threshold that
  turns the heart readout red), Robi throws in an extra bounce while
  walking, cycling, or reading, on top of whatever it was already doing.

## 1.1.3

- Fixed a permanent shiver/shake that could show up right when Robi
  settled at the edge of its idle wander range.

## 1.1.2

- Robi no longer gets stuck in one pose for as long as the weather holds.
  Sun/rain/cold reactions now layer on top of whatever Robi is already
  doing, so reading, working, cycling, and wandering off keep happening
  rain or shine.

## 1.1.1

- Fixed Robi standing completely frozen in sunny or rainy weather — those
  two reactions were missing their body-language animation entirely.
  Robi now has a calm bob in the sun and a slow droop in the rain, same
  as its other idle behaviors.

## 1.1.0

- **Heart rate readout** — a color-coded heart (green normally, red at or
  above 120 bpm) plus BPM number, shown right next to the temperature.
  Day and date are now merged onto one line to make room for it.
- **Smoother animation** — fixed a per-second HealthService query that
  was stealing CPU from the animation loop and causing visible
  trembling/stuttering in Robi's movements.

## 1.0.0

Meet Robi — a robot companion that lives on your wrist and reacts to your
day.

- **Time, date, and weather** — weather updates automatically, no
  account or API key needed.
- **Live weather icon** — a sun, cloud, raindrop, snowflake, fog, or storm
  icon next to Robi always shows current conditions.
- **Step tracker** — a fill-up progress bar toward your daily goal, plus
  the raw step count.
- **Heart rate** — a color-coded heart (green normally, red if it spikes)
  shows your current BPM right next to the temperature.
- **A robot with a life of its own** — Robi walks and runs along with you,
  reads a book, works at a laptop, grabs a snack, hops on a bike for a
  ride, wanders around, and occasionally wanders off-screen entirely
  before walking back in.
- **Shake to say hi** — give your wrist a firm shake and Robi waves back
  with a "Hi!", "Hello!", or "Hoi!".
- **Sleeps at night** — Robi tucks into bed between 22:00 and 06:00
  instead of standing idle.
- Hits its daily step goal? Confetti. It's 11:11? Robi makes a wish.

Built for Pebble Time 2 (emery).
