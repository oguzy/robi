# Changelog

All notable changes to the Robi watchface are documented in this file.

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
