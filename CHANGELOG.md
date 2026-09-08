# Changelog

All notable changes to the Robi watchface are documented in this file.

## 1.12.0 - 2026-09-08

### Added
- Wake-up transition: coming out of the sleep scene (22:00-06:00) now
  plays a short groggy `ROBOT_WAKING` stretch (~2.5s: slow lean, one arm
  drifting up, half-lidded eyes) with a "good morning!"/"yaaawn~" speech
  bubble, instead of snapping straight into a normal idle activity.
  `evaluate_state()` intercepts the `ROBOT_SLEEPY` -> not-night
  transition specifically (not just any non-idle state, so this doesn't
  fire after e.g. `ROBOT_GOAL_REACHED`), and `anim_timer_callback()`
  settles it into `ROBOT_IDLE` after 25 phases, mirroring the existing
  `ROBOT_GOAL_REACHED` pattern. `ROBOT_WAKING` is deliberately excluded
  from `is_idle_family()` and from `tick_handler`'s evaluate-again gate
  (same as `ROBOT_GOAL_REACHED`/mid-`ROBOT_AWAY`), so a stray
  minute-tick or weather update can't reset it mid-transition.

  This was the last of the ten originally-suggested feature ideas
  (mood animations, battery dimming, streaks, easter eggs, context-aware
  shake, activity nudge, tap-to-cycle row, dusk/dawn dimming, wind/UV,
  and this wake-up scene), each shipped as its own release (1.3.0 to
  1.12.0). Verified the state-machine transitions (night -> sleepy,
  sleepy -> waking, tick_handler's gate correctly excluding waking, and
  the phase-25 settle-out) with a direct simulation; a live emulator
  capture of the transition was attempted (as with sunrise/sunset and
  wind/UV) but `emu-set-time` jumps appear to deliver catch-up ticks that
  don't preserve real per-second pacing, making the ~2.5s window
  unreliable to catch via screenshot regardless of jump size - a sandbox
  limitation rather than an application defect, per the logic trace of
  tick_handler's gate above.

## 1.11.0 - 2026-09-08

### Added
- Wind and UV reactions, layered on top of whatever mood/activity is
  already showing rather than becoming their own exclusive states (same
  pattern as weather/excited/calm). `src/pkjs/index.js` now also
  requests `wind_speed_10m,uv_index` in the same `current=` Open-Meteo
  call (confirmed both are supported directly, no separate `hourly`
  fetch needed) and sends them as `WIND_SPEED_KMH`/`UV_INDEX`. On the
  watch: `is_windy()` (>= `WIND_HIGH_KMH`, 25 km/h) adds a gust-tilt sway
  to any idle-family activity, stacking with rain/cold if both apply;
  `is_uv_high()` (>= `UV_HIGH`, 6) squints the eyes independently of the
  `CONDITIONS` text, since UV can be high without literal "Clear" skies.

  Verified with a direct simulation of both threshold checks, and
  visually in the emulator: disabling both the watch-side
  `request_weather()` and PKJS's auto-fetch-on-ready (reverted before
  this commit) to avoid the real fetch racing a forced override -
  learned from chasing the same race on sunrise/sunset - a held idle
  frame showed clearly squinted eyes, and consecutive screenshots 1s
  apart showed a consistent ~754px diff (tilt affects the whole figure)
  at the fast animation cadence, confirming the sway renders and
  animates.

## 1.10.0 - 2026-09-08

### Added
- Sunrise/sunset-based dimming, independent of the hardcoded 22:00-06:00
  `ROBOT_SLEEPY` window. `src/pkjs/index.js` now requests Open-Meteo's
  `daily=sunrise,sunset&timezone=auto` alongside the existing current
  weather, converts the returned local ISO timestamps to minutes-since-
  midnight, and sends them as new `SUNRISE_MINUTES`/`SUNSET_MINUTES`
  AppMessage keys. On the watch, `is_dusk_or_dawn()` returns true within
  `DUSK_DAWN_WINDOW_MIN` (30 min) of either time, and `current_accent_color()`
  now dims for dusk/dawn the same way it already does for low battery,
  reusing the existing visual language rather than a full palette swap.

  Verified the boundary arithmetic (no data yet, at/just-outside the
  window on both sides, midday) with a direct simulation, and the OR
  logic combining battery + dusk in `current_accent_color()`. Chasing
  this live in the emulator hit two dead ends worth noting for later: a
  forced dusk override in `window_load()` was silently overwritten
  moments later by the real (non-dusk) sunrise/sunset from the actual
  Open-Meteo fetch racing it, and `pebble logs` produced no output at
  all in this sandbox across every attempt this session (unrelated to
  app behavior) - not something a future change should rely on for
  verification here.

## 1.9.0 - 2026-09-08

### Added
- Tap-to-cycle info row: the existing shake gesture now also flashes
  battery % (an outlined battery shape with proportional fill - green
  while charging, red at/below `LOW_BATTERY_PCT`, accent color otherwise)
  in place of the weather+heart row for `BATTERY_ROW_DURATION_MS` (4s),
  then reverts on its own via `battery_row_hide_callback()`. Reuses the
  single existing gesture rather than adding a button click-config
  override, which would take over the launcher-open behavior users
  expect from a watchface's SELECT button.

## 1.8.0 - 2026-09-08

### Added
- Low-activity nudge: `is_behind_pace()` compares today's steps against
  a simple linear pace target for the hour of day (`STEP_GOAL` spread
  over 24h, with a pass for hour < 12 so it doesn't nag before most
  people have had a chance to walk anywhere). When well behind pace and
  otherwise just standing in plain `ROBOT_IDLE` (not reading/working/
  etc.), the robot gets an impatient foot-tap layered on top of the
  normal idle bob via `restless_mood_active()`, plus a 1-in-3 chance per
  idle reroll of a nudge line ("let's get moving!") independent of
  whether the activity actually changed, since plain idle recurring
  without a state change would otherwise rarely trigger it.

## 1.7.0 - 2026-09-08

### Added
- Context-aware shake reactions: `trigger_speech()` used to always show
  a generic greeting ("Hi!"/"Hello!"/"Hoi!") regardless of what Robi was
  doing. It now checks `s_state` and shows an interrupted-activity remark
  for reading, working, eating, cycling, and sleepy instead (e.g. "lost
  my page!" while reading), falling back to the generic greeting for
  every other state (idle, dancing, stretching, away, walking, running,
  goal-reached).

## 1.6.0 - 2026-09-08

### Added
- Two rare idle easter eggs: `ROBOT_DANCING` (a quick upbeat wiggle -
  fast tempo, tilt + arm/leg swing) and `ROBOT_STRETCHING` (a slow
  side-to-side lean). `pick_idle_activity()` has a 1-in-8 chance per
  reroll to pick one of these instead of the regular pool, so they read
  as an occasional treat rather than a normal rotation option. Both are
  full idle-family members (weather/calm mood layer onto them normally,
  blink/smile micro-expressions apply, wander drift works).

## 1.5.0 - 2026-09-08

### Added
- Persistent step-goal streak: hitting the daily step goal now tracks a
  day-over-day streak via `persist_write_int`/`persist_read_int` (survives
  app restarts), shown in a speech bubble ("Goal hit!" or "N-day
  streak!") instead of just the existing confetti celebration. Day is
  tracked as a whole-day count (`time_start_of_today() / SECONDS_PER_DAY`)
  so streak continuation/reset is a simple integer comparison regardless
  of month/year boundaries. A restart on the same day after already
  hitting the goal re-shows the current streak without double-counting.

  Verified with a direct simulation of the day-transition logic (first
  hit, same-day restart, consecutive days, missed-day reset) rather than
  the emulator's HealthService step injection, which did not reliably
  propagate to `health_service_sum_today()` in this sandbox (same
  category of flakiness observed with heart-rate injection earlier).

## 1.4.0 - 2026-09-08

### Added
- Calm mood: the resting counterpart to the excited hop. When heart rate
  is a real reading at/below `HEART_RATE_LOW` (55bpm), idle-family
  activities (reading, working, eating, cycling, away, idle) get a
  slower, smaller-amplitude sway and slightly drowsy half-lidded eyes,
  via new `is_calm()` / `calm_mood_active()`. Unlike the additive
  weather/excited modifiers, this one replaces the idle bob (reduced
  energy, not extra motion) but still runs before the weather block so
  rain/cold continue to layer normally on top of the calmer base.

## 1.3.0 - 2026-09-08

### Added
- Battery-aware visuals: at/below `LOW_BATTERY_PCT` (20%) and not
  charging, the robot's own accent lighting (eyes, antenna tip, chest
  core) dims from `ACCENT_COLOR` to `ACCENT_DIM`, and idle blinking slows
  to half its normal cadence - a visible "powering down" look via
  `battery_state_service_subscribe()` / `current_accent_color()`, doubling
  as a low-battery cue without a text warning competing with the face.

## 1.2.0 - 2026-09-08

### Added
- Excited mood: when heart rate is at/above `HEART_RATE_HIGH` (same
  threshold that turns the heart readout red), walking, cycling, and
  reading get an extra hop layered on top of their own motion (stride,
  pedal, page-turn) - reusing `ROBOT_GOAL_REACHED`'s big sine bob via a
  new `is_excited()` / `jump_mood_active()` pair, following the same
  "layer on top, don't replace" pattern as weather mood.

## 1.1.3 - 2026-09-08

### Fixed
- Idle wander shiver at the destination: `s_wander_target` (picked by
  `pick_idle_activity()`) can land on any integer from -50 to 50, but
  `s_robot_x_offset` only ever eased toward it in fixed steps of 2px from
  an even starting point, so it could never exactly reach an odd target -
  it overshot by 1px each direction and oscillated back and forth forever
  once it got there, showing as a permanent shake right as the robot
  settled at the edge of its wander range. The easing now clamps its last
  step so it lands exactly on the target and stops.

## 1.1.2 - 2026-09-08

### Changed
- Weather reactions (sun squint, rain droop, cold shiver) are no longer
  exclusive states. `evaluate_state()` used to force the robot into a
  dedicated `ROBOT_WEATHER_SUN`/`RAIN`/`COLD` state every second while
  that weather held, which completely blocked the random idle activities
  (reading, working, eating, cycling, wandering off) from ever showing -
  in effect, the robot was stuck in a single near-static pose for as long
  as it was sunny, rainy, or cold. Weather is now a mood applied as an
  eye/tilt/bob modifier on top of whichever idle-family activity is
  already playing, so the random idle variety keeps happening regardless
  of weather. `ROBOT_WEATHER_SUN`/`RAIN`/`COLD` are removed from
  `RobotState`; added `WeatherMood` + `current_weather_mood()` instead.

## 1.1.1 - 2026-09-08

### Fixed
- Robot stood completely frozen whenever the weather-reactive states
  (`ROBOT_WEATHER_SUN`, `ROBOT_WEATHER_RAIN`) were active - the drawing
  switch in `robot_layer_update_proc()` had no `case` for either of them,
  so `bob`/`tilt`/`x_offset` stayed at 0 (only the eye height changed).
  `evaluate_state()` forces one of these states any time it's sunny or
  raining and re-checks every second, so the robot would latch there and
  never move. Added a calm idle bob for sun and a slow downward "droop"
  bob/tilt for rain, matching the "sun squint, rain droop" body language
  described for these states.

## 1.1.0 - 2026-09-08

### Fixed
- Animation trembling/stuttering: `evaluate_state()` was re-querying
  `health_service_sum_today()` every second (via the SECOND_UNIT tick
  handler) just to check the step goal, for almost every state the watch
  is normally in. That HealthService DB query running 60x/minute stole
  CPU from the 100ms animation timer. It now reads the step count already
  cached by `update_steps()` (refreshed every minute and on health
  events) instead of re-querying HealthService on every tick.

### Added
- Heart rate readout next to the temperature: a small heart icon plus BPM
  number, colored green normally and red at/above `HEART_RATE_HIGH` (120
  bpm by default). Shows a dim "--" heart when no reading is available.
- `health_service_set_heart_rate_sample_period(60)` requested at startup
  so the heart rate updates roughly once a minute instead of relying on
  the system's default (much less frequent) sampling.

### Changed
- Day and date merged onto a single line ("Sun, Sep 06") instead of two
  separate rows, freeing vertical space for the heart rate readout.
- Step count font enlarged (14pt → 18pt bold) for better readability.
- Row layout re-tuned: steps (28px), weather+heart (34px), day/date (28px)
  in place of the previous four 25px rows.

## 1.0.0 - Merge animated robot with key-free weather

### Added
- Animated robot character with idle behaviors: reading, working, eating,
  cycling (with a drawn bicycle and rolling wheels), and an occasional
  "wander off-screen, disappear, and walk back in" sequence.
- Idle wandering: the robot drifts sideways to a new spot on each activity
  reroll instead of standing frozen at center.
- Shake-to-greet: a firm wrist shake pops up a speech bubble ("Hi!",
  "Hello!", or "Hoi!") with a wave animation.
- Goal-reached celebration (confetti) once the daily step goal is hit, and
  an 11:11 "make a wish" easter egg.
- Weather-reactive body language (sun squint, rain droop, cold shiver) on
  top of a persistent top-right weather icon (sun/cloud/rain/snow/fog/
  storm) that reflects live conditions regardless of what the robot is
  doing.
- Sleeping scene (22:00-06:00): the robot lies down on a drawn bed with
  closed eyes and floating "z"s instead of standing idle.
- Steps progress bar (pixel-filled, toward `STEP_GOAL`) with the raw step
  count shown underneath, replacing the previous "(NN%)" text suffix.
- Weather via [Open-Meteo](https://open-meteo.com/) — no API key, no
  settings page, refreshes automatically every 30 minutes.

### Changed
- Time font switched to `FONT_KEY_LECO_42_NUMBERS`.
- AppMessage keys switched from a hand-rolled enum to the SDK's
  auto-generated `MESSAGE_KEY_*` macros.

### Removed
- Accelerometer-based stairs up/down detection (unreliable heuristic, and
  its always-on raw accelerometer streaming fought the tap/shake
  detector's low-power mode).
- OpenWeatherMap API key + phone settings/config page (replaced by the
  key-free Open-Meteo integration).
- `robi-preview.html` browser mockup.

### Fixed
- Weather-refresh check only compared minutes (`tm_min % 30 == 0`), so it
  fired on every one of the 60 seconds within minute :00 and :30,
  flooding the AppMessage outbox and crash-looping the app. Now also
  requires `tm_sec == 0`.

## Earlier history

Prior to the merge above, Robi went through several iterations documented
in individual commit messages: renaming from the original template,
shrinking the robot and enlarging the bottom text, switching the greeting
gesture from double-tap to a single shake (the OS's tap detector needs a
wrist-shake-level hit to register on this hardware), forcing the backlight
on when a tap is detected, and adding randomized idle activities
(reading/working/eating/smiling). See `git log` for the full history.
