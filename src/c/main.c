#include <pebble.h>

// ---------- CONFIG ----------
#define STEP_GOAL 10000
#define HEART_RATE_HIGH 120  // bpm - heart icon/number turn red at or above this
#define ACCENT_COLOR GColorFromHEX(0x55EFEF)
#define ACCENT_DIM GColorFromHEX(0x2F8F8F)
#define BODY_LIGHT GColorFromHEX(0x8A9096)
#define BODY_MID GColorFromHEX(0x52565C)
#define BODY_DARK GColorFromHEX(0x26282B)
#define VISOR_COLOR GColorFromHEX(0x0C1012)
#define BG_COLOR GColorBlack
#define TEXT_COLOR GColorWhite
#define SPEECH_DURATION_MS 1800

typedef enum {
  ROBOT_IDLE,
  ROBOT_READING,
  ROBOT_WORKING,
  ROBOT_EATING,
  ROBOT_CYCLING,
  ROBOT_AWAY,
  ROBOT_WALKING,
  ROBOT_RUNNING,
  ROBOT_GOAL_REACHED,
  ROBOT_SLEEPY
} RobotState;

// Weather reactions (sun squint, rain droop, cold shiver) are no longer
// exclusive states - they used to completely replace whatever idle
// activity was showing (and lock the robot into a near-static pose for as
// long as that weather held), which fought the "random moves" idle
// wander. Instead they're a mood layered as an eye/tilt/bob modifier on
// top of whichever idle-family activity is already playing.
typedef enum {
  WEATHER_MOOD_NONE,
  WEATHER_MOOD_SUN,
  WEATHER_MOOD_RAIN,
  WEATHER_MOOD_COLD
} WeatherMood;

// ROBOT_AWAY plays out as a scripted sub-timeline keyed off s_anim_phase:
// walk off-screen, vanish entirely for a bit, walk back in. Phase units are
// anim_timer_callback ticks (100ms while animating).
#define AWAY_GONE_AT 15
#define AWAY_RETURN_AT 40
#define AWAY_DONE_AT 55

// pick_idle_activity() (defined below, near trigger_speech) is called from
// tick_handler, which comes earlier in the file than its definition.
static void pick_idle_activity(void);

static Window *s_window;
static Layer *s_robot_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static Layer *s_steps_layer;
static Layer *s_weather_layer;

static GFont s_time_font;
static GFont s_small_font;
static GFont s_speech_font;
static GFont s_steps_font;

static char s_time_buffer[8];
static char s_date_buffer[24];
static char s_steps_buffer[32];
static char s_weather_buffer[16] = "--\xC2\xB0";
static char s_heart_buffer[12] = "--";
static char s_conditions[16] = "";
static int s_steps_count = 0;
static int s_heart_rate = 0;

static bool s_blink = false;
static bool s_bounce = false;

static RobotState s_state = ROBOT_IDLE;
static int s_anim_phase = 0;
static bool s_goal_celebrated_today = false;
static AppTimer *s_anim_timer = NULL;
static int s_idle_activity_countdown = 0;

// Idle wander: the robot drifts sideways to a new random spot every time
// pick_idle_activity() rerolls, easing there a couple px per animation
// frame rather than jumping.
static int s_robot_x_offset = 0;
static int s_wander_target = 0;

static bool s_show_speech = false;
static bool s_show_smile = false;
static const char *s_speech_text = "Hi!";
static AppTimer *s_speech_timer = NULL;

// ---------------- HELPERS ----------------
static bool is_night(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  return (t->tm_hour >= 22 || t->tm_hour < 6);
}

static bool is_special_time(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  return (t->tm_hour % 12 == 11 && t->tm_min == 11);
}

// The "nothing else going on" states, cycled through randomly by
// pick_idle_activity() instead of just standing still.
static bool is_idle_family(RobotState s) {
  return s == ROBOT_IDLE || s == ROBOT_READING || s == ROBOT_WORKING ||
         s == ROBOT_EATING || s == ROBOT_CYCLING || s == ROBOT_AWAY;
}

static WeatherMood current_weather_mood(void) {
  if (strcmp(s_conditions, "Rain") == 0 || strcmp(s_conditions, "Drizzle") == 0) {
    return WEATHER_MOOD_RAIN;
  }
  if (strcmp(s_conditions, "Snow") == 0) return WEATHER_MOOD_COLD;
  if (strcmp(s_conditions, "Clear") == 0) return WEATHER_MOOD_SUN;
  return WEATHER_MOOD_NONE;
}

// ---------------- STATE EVALUATION ----------------
static void evaluate_state(void) {
  // Goal check uses the cached count from update_steps() (refreshed once a
  // minute, plus on health events) rather than re-querying HealthService
  // here. evaluate_state() runs every second for most states (idle family,
  // sleepy, weather-reactive), and health_service_sum_today() is a real
  // HealthService DB query - calling it 60x/minute stole enough CPU from
  // the 100ms animation timer to cause visible stutter/trembling.
  int steps = s_steps_count;

  if (steps >= STEP_GOAL && !s_goal_celebrated_today) {
    s_state = ROBOT_GOAL_REACHED;
    s_goal_celebrated_today = true;
    s_anim_phase = 0;
    return;
  }

  HealthActivityMask activities = health_service_peek_current_activities();

  if (activities & HealthActivityRun) { s_state = ROBOT_RUNNING; return; }

  if (activities & HealthActivityWalk) {
    s_state = ROBOT_WALKING;
    return;
  }

  if (is_night()) { s_state = ROBOT_SLEEPY; return; }

  // Weather no longer forces its own exclusive state here - see
  // current_weather_mood() and its use in robot_layer_update_proc(),
  // which layers the reaction on top of whatever idle activity is active
  // instead of replacing it.

  // Nothing else applies. If we're arriving here fresh (previous state
  // wasn't already one of the idle-family states), drop into plain idle and
  // ask tick_handler to reroll a time-appropriate activity on its very next
  // tick. If we're already idle-family, leave the current activity alone -
  // tick_handler's countdown decides when to change it, not this function
  // (which runs far more often, and would otherwise make it flicker).
  if (!is_idle_family(s_state)) {
    s_state = ROBOT_IDLE;
    s_idle_activity_countdown = 0;
  }
}

// ---------------- ANIMATION TICK ----------------
static void anim_timer_callback(void *data) {
  s_anim_phase++;

  // Step size is fixed at 2px/tick, but s_wander_target (odd or even) isn't
  // guaranteed reachable in steps of 2 from the current offset - stepping
  // past it and never landing exactly caused a permanent 1px-each-way
  // shiver right at the destination. Clamp the last step to land exactly.
  int wander_diff = s_wander_target - s_robot_x_offset;
  if (wander_diff > 0) s_robot_x_offset += (wander_diff < 2) ? wander_diff : 2;
  else if (wander_diff < 0) s_robot_x_offset += (wander_diff > -2) ? wander_diff : -2;

  layer_mark_dirty(s_robot_layer);

  if (s_state == ROBOT_GOAL_REACHED && s_anim_phase > 20) {
    s_state = ROBOT_IDLE;
    s_anim_phase = 0;
    s_idle_activity_countdown = 0;
  }

  if (s_state == ROBOT_AWAY && s_anim_phase > AWAY_DONE_AT + 20) {
    // Scripted sequence finished a while ago and nothing rerolled it yet
    // (idle_activity_countdown can outlast the ~5.5s script) - just settle
    // into a plain idle look rather than sitting on a stale AWAY state.
    s_robot_x_offset = 0;
  }

  WeatherMood mood = current_weather_mood();
  bool weather_anim_active = is_idle_family(s_state) &&
      (mood == WEATHER_MOOD_RAIN || mood == WEATHER_MOOD_COLD);

  bool needs_smooth_anim =
      (s_state == ROBOT_WALKING || s_state == ROBOT_RUNNING ||
       s_state == ROBOT_CYCLING || s_state == ROBOT_AWAY ||
       s_state == ROBOT_GOAL_REACHED || s_show_speech ||
       weather_anim_active || s_robot_x_offset != s_wander_target);

  s_anim_timer = app_timer_register(needs_smooth_anim ? 100 : 600,
                                     anim_timer_callback, NULL);
}

// ---------------- MOTION HELPERS ----------------
// Smooth, continuous sine-based swing instead of a blocky two-value flip.
// angle_step controls cadence (bigger = faster cycle), amplitude is in px.
static int sine_wave(int amplitude, int angle_step) {
  int32_t angle = (s_anim_phase * angle_step) % TRIG_MAX_ANGLE;
  if (angle < 0) angle += TRIG_MAX_ANGLE;
  return (sin_lookup(angle) * amplitude) / TRIG_MAX_RATIO;
}

// ---------------- SCENERY ----------------
// Ground / road, used for walking + running
static void draw_road(GContext *ctx, GRect bounds, int speed) {
  int road_y = bounds.origin.y + bounds.size.h - 8;
  GRect road = GRect(bounds.origin.x, road_y, bounds.size.w, 5);
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, road, 0, GCornerNone);

  graphics_context_set_fill_color(ctx, ACCENT_COLOR);
  int dash_w = 10, gap = 14;
  int scroll = (s_anim_phase * speed) % (dash_w + gap);
  for (int x = -scroll; x < bounds.size.w; x += (dash_w + gap)) {
    GRect dash = GRect(bounds.origin.x + x, road_y + 2, dash_w, 2);
    graphics_fill_rect(ctx, dash, 0, GCornerNone);
  }
}

// Bed scene + a simplified "lying down" robot silhouette, used while sleepy.
// Drawn as its own self-contained scene rather than rotating the standing
// robot, since Pebble's 2D graphics API doesn't cleanly rotate composite
// rounded-rect shapes.
static void draw_sleeping_scene(GContext *ctx, GRect bounds) {
  int cx = bounds.origin.x + bounds.size.w / 2;
  int bed_w = 79, bed_h = 19;
  int bed_x = cx - bed_w / 2;
  int bed_y = bounds.origin.y + bounds.size.h - bed_h - 9;

  // headboard (taller panel behind the pillow end) - shaded like the
  // standing robot's body parts, no full accent outline
  GRect headboard = GRect(bed_x - 4, bed_y - 10, 9, bed_h + 17);
  graphics_context_set_fill_color(ctx, BODY_DARK);
  graphics_fill_rect(ctx, headboard, 4, GCornersAll);
  GRect headboard_hi = GRect(headboard.origin.x + 1, headboard.origin.y + 1, headboard.size.w - 1, 6);
  graphics_context_set_fill_color(ctx, BODY_LIGHT);
  graphics_fill_rect(ctx, headboard_hi, 3, GCornersTop);

  // frame
  GRect frame = GRect(bed_x - 1, bed_y - 1, bed_w + 2, bed_h + 6);
  graphics_context_set_fill_color(ctx, BODY_DARK);
  graphics_fill_rect(ctx, frame, 4, GCornersAll);

  // legs
  graphics_context_set_fill_color(ctx, BODY_MID);
  graphics_fill_rect(ctx, GRect(bed_x + 1, bed_y + bed_h + 5, 3, 5), 0, GCornersAll);
  graphics_fill_rect(ctx, GRect(bed_x + bed_w - 4, bed_y + bed_h + 5, 3, 5), 0, GCornersAll);

  // mattress with a couple of stitch lines for texture
  GRect mattress = GRect(bed_x, bed_y, bed_w, bed_h);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, mattress, 3, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorFromHEX(0xB5B7BA));
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(bed_x + 2, bed_y + bed_h / 3), GPoint(bed_x + bed_w - 2, bed_y + bed_h / 3));
  graphics_draw_line(ctx, GPoint(bed_x + 2, bed_y + bed_h * 2 / 3), GPoint(bed_x + bed_w - 2, bed_y + bed_h * 2 / 3));

  // pillow
  GRect pillow = GRect(bed_x + 2, bed_y - 5, 20, 11);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, pillow, 4, GCornersAll);

  // robot lying down: same shaded-panel body language as the standing robot
  int body_y = bed_y - 2;
  GRect body = GRect(bed_x + 20, body_y, 42, 12);
  graphics_context_set_fill_color(ctx, BODY_DARK);
  graphics_fill_rect(ctx, body, 6, GCornersAll);
  GRect body_hi = GRect(body.origin.x + 1, body.origin.y + 1, body.size.w - 2, 4);
  graphics_context_set_fill_color(ctx, BODY_LIGHT);
  graphics_fill_rect(ctx, body_hi, 5, GCornersTop);
  // dim chest light (not the bright pulse used while awake)
  graphics_context_set_fill_color(ctx, ACCENT_DIM);
  graphics_fill_circle(ctx, GPoint(body.origin.x + 11, body.origin.y + 6), 2);

  GRect head = GRect(bed_x + 5, body_y - 6, 17, 16);
  graphics_context_set_fill_color(ctx, BODY_DARK);
  graphics_fill_rect(ctx, head, 6, GCornersAll);
  GRect head_hi = GRect(head.origin.x + 1, head.origin.y + 1, head.size.w - 2, 6);
  graphics_context_set_fill_color(ctx, BODY_LIGHT);
  graphics_fill_rect(ctx, head_hi, 5, GCornersTop);

  // mini visor with closed-eye slits, same idea as the standing robot's
  // visor rather than lines floating directly on the body panel
  GRect visor = GRect(head.origin.x + 2, head.origin.y + 7, 12, 6);
  graphics_context_set_fill_color(ctx, VISOR_COLOR);
  graphics_fill_rect(ctx, visor, 2, GCornersAll);
  graphics_context_set_stroke_color(ctx, ACCENT_DIM);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(visor.origin.x + 2, visor.origin.y + 2),
                           GPoint(visor.origin.x + 5, visor.origin.y + 2));
  graphics_draw_line(ctx, GPoint(visor.origin.x + 7, visor.origin.y + 2),
                           GPoint(visor.origin.x + 11, visor.origin.y + 2));

  // blanket with a fold line for a little dimension
  GRect blanket = GRect(bed_x + 17, bed_y + bed_h - 6, bed_w - 20, 6);
  graphics_context_set_fill_color(ctx, ACCENT_COLOR);
  graphics_fill_rect(ctx, blanket, 2, GCornersAll);
  graphics_context_set_stroke_color(ctx, ACCENT_DIM);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(blanket.origin.x + 4, blanket.origin.y + blanket.size.h - 2),
                           GPoint(blanket.origin.x + blanket.size.w - 4, blanket.origin.y + blanket.size.h - 2));

  // floating "z"s
  graphics_context_set_text_color(ctx, GColorLightGray);
  int bob = (s_anim_phase % 20) - 10;
  char z1[] = "z";
  char z2[] = "Z";
  graphics_draw_text(ctx, z1, s_speech_font, GRect(head.origin.x + 11, head.origin.y - 12 - bob / 2, 12, 12),
                      GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, z2, s_speech_font, GRect(head.origin.x + 17, head.origin.y - 20 - bob, 12, 12),
                      GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// ---------------- DECORATION OVERLAYS ----------------
static void draw_confetti(GContext *ctx, GRect head) {
  GColor colors[] = { ACCENT_COLOR, GColorRajah, GColorMagenta, GColorYellow };
  for (int i = 0; i < 6; i++) {
    int x = head.origin.x + (i * 17 + (s_anim_phase * 5)) % head.size.w;
    int y = head.origin.y - 6 - ((s_anim_phase * 3 + i * 9) % 20);
    graphics_context_set_fill_color(ctx, colors[i % 4]);
    graphics_fill_circle(ctx, GPoint(x, y), 2);
  }
}

// Persistent weather badge, top-right of the robot layer - always reflects
// s_conditions regardless of what the robot itself is doing, rather than
// only appearing when the robot happens to be idle in a matching weather
// reaction state.
static void draw_cloud_shape(GContext *ctx, GPoint c, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(c.x - 4, c.y + 1), 4);
  graphics_fill_circle(ctx, GPoint(c.x + 2, c.y - 2), 5);
  graphics_fill_circle(ctx, GPoint(c.x + 6, c.y + 1), 4);
  graphics_fill_rect(ctx, GRect(c.x - 8, c.y, 16, 5), 2, GCornersAll);
}

static void draw_weather_icon(GContext *ctx, GRect bounds) {
  if (s_conditions[0] == '\0') return;  // no weather data yet

  GPoint c = GPoint(bounds.origin.x + bounds.size.w - 14, bounds.origin.y + 13);

  if (strcmp(s_conditions, "Clear") == 0) {
    graphics_context_set_fill_color(ctx, GColorYellow);
    graphics_fill_circle(ctx, c, 5);
    graphics_context_set_stroke_color(ctx, GColorYellow);
    for (int i = 0; i < 8; i++) {
      int32_t angle = (TRIG_MAX_ANGLE / 8) * i;
      GPoint p1 = GPoint(c.x + sin_lookup(angle) * 7 / TRIG_MAX_RATIO,
                          c.y - cos_lookup(angle) * 7 / TRIG_MAX_RATIO);
      GPoint p2 = GPoint(c.x + sin_lookup(angle) * 10 / TRIG_MAX_RATIO,
                          c.y - cos_lookup(angle) * 10 / TRIG_MAX_RATIO);
      graphics_draw_line(ctx, p1, p2);
    }
  } else if (strcmp(s_conditions, "Cloudy") == 0) {
    draw_cloud_shape(ctx, c, GColorLightGray);
  } else if (strcmp(s_conditions, "Fog") == 0) {
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = 0; i < 3; i++) {
      int y = c.y - 5 + i * 5;
      graphics_draw_line(ctx, GPoint(c.x - 9, y), GPoint(c.x + 9, y));
    }
  } else if (strcmp(s_conditions, "Rain") == 0 || strcmp(s_conditions, "Drizzle") == 0 ||
             strcmp(s_conditions, "Fz. Rain") == 0 || strcmp(s_conditions, "Fz. Drizzle") == 0 ||
             strcmp(s_conditions, "Showers") == 0) {
    draw_cloud_shape(ctx, GPoint(c.x, c.y - 2), GColorLightGray);
    graphics_context_set_stroke_color(ctx, GColorVividCerulean);
    for (int i = 0; i < 3; i++) {
      int x = c.x - 6 + i * 6;
      int y = c.y + 3 + ((s_anim_phase * 3 + i * 5) % 6);
      graphics_draw_line(ctx, GPoint(x, y), GPoint(x - 2, y + 5));
    }
  } else if (strcmp(s_conditions, "Snow") == 0 || strcmp(s_conditions, "Snow Grains") == 0 ||
             strcmp(s_conditions, "Snow Shwrs") == 0) {
    draw_cloud_shape(ctx, GPoint(c.x, c.y - 2), GColorLightGray);
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int i = 0; i < 3; i++) {
      int x = c.x - 6 + i * 6;
      int y = c.y + 4 + ((i + s_anim_phase / 3) % 2) * 3;
      graphics_fill_circle(ctx, GPoint(x, y), 2);
    }
  } else if (strcmp(s_conditions, "T-Storm") == 0) {
    draw_cloud_shape(ctx, GPoint(c.x, c.y - 2), GColorDarkGray);
    GPoint bolt[3] = {
      GPoint(c.x + 2, c.y + 3), GPoint(c.x - 2, c.y + 8), GPoint(c.x + 1, c.y + 8)
    };
    graphics_context_set_stroke_color(ctx, GColorChromeYellow);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, bolt[0], bolt[1]);
    graphics_draw_line(ctx, bolt[1], bolt[2]);
    graphics_draw_line(ctx, bolt[2], GPoint(c.x - 3, c.y + 13));
  }
  // "Unknown" or anything else: no icon.
}

// An open book held in front of the torso, for ROBOT_READING.
static void draw_book(GContext *ctx, GRect torso) {
  int bw = 18, bh = 8;
  int bx = torso.origin.x + torso.size.w / 2 - bw / 2;
  int by = torso.origin.y + torso.size.h - 5;

  GRect left_page = GRect(bx, by, bw / 2, bh);
  GRect right_page = GRect(bx + bw / 2, by, bw / 2, bh);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, left_page, 1, GCornersLeft);
  graphics_fill_rect(ctx, right_page, 1, GCornersRight);

  graphics_context_set_stroke_color(ctx, BODY_MID);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(bx + bw / 2, by), GPoint(bx + bw / 2, by + bh));

  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(bx + 2, by + 2, 5, 1), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bx + 2, by + 5, 5, 1), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bx + bw / 2 + 2, by + 2, 5, 1), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bx + bw / 2 + 2, by + 5, 5, 1), 0, GCornerNone);
}

// A little laptop/tablet in front of the torso, for ROBOT_WORKING. The
// on-screen line length shifts with phase for a subtle "typing" feel.
static void draw_laptop(GContext *ctx, GRect torso, int phase) {
  int lx = torso.origin.x + torso.size.w / 2 - 9;
  int ly = torso.origin.y + torso.size.h + 1;

  GRect base = GRect(lx, ly, 18, 2);
  GRect screen = GRect(lx + 2, ly - 8, 14, 8);
  graphics_context_set_fill_color(ctx, BODY_MID);
  graphics_fill_rect(ctx, base, 1, GCornersAll);
  graphics_context_set_fill_color(ctx, VISOR_COLOR);
  graphics_fill_rect(ctx, screen, 1, GCornersAll);

  graphics_context_set_fill_color(ctx, ACCENT_COLOR);
  int line_len = 4 + (phase % 3) * 2;
  graphics_fill_rect(ctx, GRect(screen.origin.x + 2, screen.origin.y + 2, line_len, 1), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(screen.origin.x + 2, screen.origin.y + 5, 6, 1), 0, GCornerNone);
}

// A snack held up near the head, for ROBOT_EATING.
static void draw_snack(GContext *ctx, GRect head) {
  int fx = head.origin.x + head.size.w + 3;
  int fy = head.origin.y + head.size.h - 8;
  graphics_context_set_fill_color(ctx, GColorChromeYellow);
  graphics_fill_circle(ctx, GPoint(fx, fy), 4);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(fx - 1, fy - 1), 1);
  graphics_context_set_fill_color(ctx, BG_COLOR);
  graphics_fill_circle(ctx, GPoint(fx + 2, fy + 1), 2);
}

// Simple bicycle frame + wheels beneath the robot, for ROBOT_CYCLING.
static void draw_bicycle(GContext *ctx, GRect torso, GRect bounds) {
  int ground_y = bounds.origin.y + bounds.size.h - 8;
  int cx = torso.origin.x + torso.size.w / 2;
  int wheel_r = 7;
  GPoint left_wheel = GPoint(cx - 14, ground_y - wheel_r);
  GPoint right_wheel = GPoint(cx + 14, ground_y - wheel_r);
  GPoint seat = GPoint(cx, torso.origin.y + torso.size.h);

  graphics_context_set_stroke_color(ctx, BODY_MID);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, left_wheel, seat);
  graphics_draw_line(ctx, right_wheel, seat);
  graphics_draw_line(ctx, left_wheel, right_wheel);

  graphics_context_set_stroke_color(ctx, ACCENT_DIM);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, left_wheel, wheel_r);
  graphics_draw_circle(ctx, right_wheel, wheel_r);
  // spinning-spoke hint so the wheels don't look static
  int32_t spin = (s_anim_phase * (TRIG_MAX_ANGLE / 8)) % TRIG_MAX_ANGLE;
  GPoint spoke_end = GPoint(right_wheel.x + sin_lookup(spin) * wheel_r / TRIG_MAX_RATIO,
                             right_wheel.y - cos_lookup(spin) * wheel_r / TRIG_MAX_RATIO);
  graphics_draw_line(ctx, right_wheel, spoke_end);
}

static void draw_speech_bubble(GContext *ctx, GRect head, GRect bounds, const char *text) {
  int bubble_w = 64, bubble_h = 26;
  int bubble_x = head.origin.x + head.size.w - 15;
  if (bubble_x + bubble_w > bounds.origin.x + bounds.size.w - 4) {
    bubble_x = bounds.origin.x + bounds.size.w - 4 - bubble_w;
  }
  if (bubble_x < bounds.origin.x + 4) bubble_x = bounds.origin.x + 4;

  GRect bubble = GRect(bubble_x, head.origin.y - 4, bubble_w, bubble_h);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bubble, 5, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_round_rect(ctx, bubble, 5);

  int tail_y = bubble.origin.y + bubble.size.h / 2;
  if (tail_y < bubble.origin.y + 6) tail_y = bubble.origin.y + 6;
  if (tail_y > bubble.origin.y + bubble.size.h - 6) tail_y = bubble.origin.y + bubble.size.h - 6;
  GPoint tail[3] = {
    GPoint(bubble.origin.x, tail_y - 4),
    GPoint(bubble.origin.x, tail_y + 4),
    GPoint(head.origin.x + head.size.w - 2, head.origin.y + head.size.h / 2)
  };
  GPathInfo tail_info = { .num_points = 3, .points = tail };
  GPath *tail_path = gpath_create(&tail_info);
  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, tail_path);
  gpath_destroy(tail_path);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, text, s_speech_font, grect_inset(bubble, GEdgeInsets(3)),
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Shaded limb (arm or leg): dark base + a lighter highlight strip down one
// edge, an accent "cuff" band near the far end, and a rounded cap - same
// idea as the head/torso panels, just narrower.
static void draw_limb(GContext *ctx, GRect r) {
  graphics_context_set_fill_color(ctx, BODY_DARK);
  graphics_fill_rect(ctx, r, 2, GCornersAll);
  GRect hi = GRect(r.origin.x + 1, r.origin.y + 1, (r.size.w - 2) / 2, r.size.h - 2);
  graphics_context_set_fill_color(ctx, BODY_MID);
  graphics_fill_rect(ctx, hi, 2, GCornersLeft);

  GRect cuff = GRect(r.origin.x, r.origin.y + r.size.h - 5, r.size.w, 1);
  graphics_context_set_fill_color(ctx, ACCENT_DIM);
  graphics_fill_rect(ctx, cuff, 1, GCornersAll);

  GRect cap = GRect(r.origin.x - 1, r.origin.y + r.size.h - 2, r.size.w + 2, 4);
  graphics_context_set_fill_color(ctx, BODY_DARK);
  graphics_fill_rect(ctx, cap, 2, GCornersAll);
}

// ---------------- MAIN ROBOT DRAWING ----------------
static void robot_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int cx = bounds.size.w / 2;

  // Sleeping gets its own dedicated scene entirely
  if (s_state == ROBOT_SLEEPY) {
    draw_sleeping_scene(ctx, bounds);
    return;
  }

  // Mid-way through ROBOT_AWAY the robot has walked fully off one edge and
  // hasn't walked back in yet - draw nothing at all for this stretch so it
  // genuinely looks like it's gone, rather than just off in a corner.
  if (s_state == ROBOT_AWAY && s_anim_phase >= AWAY_GONE_AT && s_anim_phase < AWAY_RETURN_AT) {
    draw_weather_icon(ctx, bounds);
    return;
  }

  int bob = 0, tilt = 0, top_shift = 0, x_offset = 0;
  int arm_swing = 0, leg_swing = 0;

  switch (s_state) {
    case ROBOT_WALKING: {
      draw_road(ctx, bounds, 2);
      int swing = sine_wave(4, TRIG_MAX_ANGLE / 11);
      bob = -(abs(swing) / 2);
      arm_swing = swing;
      leg_swing = swing / 2;
      break;
    }
    case ROBOT_RUNNING: {
      draw_road(ctx, bounds, 5);
      int swing = sine_wave(6, TRIG_MAX_ANGLE / 6);
      bob = -(abs(swing) / 2) - 1;
      tilt = swing / 5;
      arm_swing = swing;
      leg_swing = swing * 2 / 3;
      break;
    }
    case ROBOT_CYCLING: {
      draw_road(ctx, bounds, 3);
      int pedal = sine_wave(5, TRIG_MAX_ANGLE / 8);
      bob = -(abs(pedal) / 3);
      leg_swing = pedal;
      arm_swing = pedal / 4;
      x_offset = s_robot_x_offset;
      break;
    }
    case ROBOT_AWAY: {
      // Only the leaving (0..AWAY_GONE_AT) and returning
      // (AWAY_RETURN_AT..AWAY_DONE_AT) legs reach this switch - the fully
      // "gone" middle stretch already returned above.
      if (s_anim_phase < AWAY_GONE_AT) {
        draw_road(ctx, bounds, 2);
        int swing = sine_wave(4, TRIG_MAX_ANGLE / 11);
        bob = -(abs(swing) / 2);
        arm_swing = swing;
        leg_swing = swing / 2;
        x_offset = (95 * s_anim_phase) / AWAY_GONE_AT;
      } else if (s_anim_phase < AWAY_DONE_AT) {
        draw_road(ctx, bounds, 2);
        int swing = sine_wave(4, TRIG_MAX_ANGLE / 11);
        bob = -(abs(swing) / 2);
        arm_swing = swing;
        leg_swing = swing / 2;
        int t = s_anim_phase - AWAY_RETURN_AT;
        int span = AWAY_DONE_AT - AWAY_RETURN_AT;
        x_offset = -95 + (95 * t) / span;
      } else {
        // Script's done and nothing rerolled the state yet - settle into a
        // calm idle bob instead of walking in place forever.
        bob = -(abs(sine_wave(1, TRIG_MAX_ANGLE / 30)));
      }
      break;
    }
    case ROBOT_GOAL_REACHED:
      bob = -(int)(abs(sine_wave(5, TRIG_MAX_ANGLE / 8)));
      arm_swing = -6;
      break;
    case ROBOT_IDLE:
      bob = -(abs(sine_wave(1, TRIG_MAX_ANGLE / 30)));
      x_offset = s_robot_x_offset;
      break;
    case ROBOT_READING:
      bob = -(abs(sine_wave(1, TRIG_MAX_ANGLE / 30)));
      tilt = sine_wave(1, TRIG_MAX_ANGLE / 40);
      x_offset = s_robot_x_offset;
      break;
    case ROBOT_WORKING:
      bob = -(abs(sine_wave(1, TRIG_MAX_ANGLE / 30)));
      arm_swing = sine_wave(2, TRIG_MAX_ANGLE / 8);
      x_offset = s_robot_x_offset;
      break;
    case ROBOT_EATING:
      bob = -(abs(sine_wave(1, TRIG_MAX_ANGLE / 30)));
      arm_swing = sine_wave(2, TRIG_MAX_ANGLE / 20);
      x_offset = s_robot_x_offset;
      break;
    default:
      break;
  }

  // Weather mood layered on top of whichever idle-family activity is
  // playing, rather than replacing it - so reading/working/cycling/etc.
  // keep happening in the rain instead of the robot getting stuck in a
  // single "weather reaction" pose for as long as that weather holds.
  WeatherMood mood = current_weather_mood();
  if (is_idle_family(s_state)) {
    if (mood == WEATHER_MOOD_RAIN) {
      int droop = sine_wave(2, TRIG_MAX_ANGLE / 14);
      bob += abs(droop) / 2;
      tilt += droop / 6;
    } else if (mood == WEATHER_MOOD_COLD) {
      tilt += sine_wave(1, TRIG_MAX_ANGLE / 6);
    }
  }

  if (s_show_speech) arm_swing = -6;
  if (s_bounce) top_shift -= 2;
  int top = bounds.origin.y + top_shift;
  cx += x_offset;

  // ---- antenna ----
  graphics_context_set_fill_color(ctx, BODY_DARK);
  graphics_fill_circle(ctx, GPoint(cx + tilt, top + 6 + bob), 2);
  graphics_context_set_stroke_color(ctx, ACCENT_COLOR);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(cx + tilt, top + 1), GPoint(cx + tilt, top + 6 + bob));
  graphics_context_set_fill_color(ctx, ACCENT_COLOR);
  graphics_fill_circle(ctx, GPoint(cx + tilt, top + bob), 2);

  // ---- head: shaded panel instead of a flat block with a uniform outline ----
  int head_w = 40, head_h = 29;
  GRect head = GRect(cx - head_w / 2 + tilt, top + 6 + bob, head_w, head_h);
  graphics_context_set_fill_color(ctx, BODY_DARK);
  graphics_fill_rect(ctx, head, 9, GCornersAll);
  GRect head_hi = GRect(head.origin.x + 1, head.origin.y + 1, head_w - 2, head_h * 3 / 5);
  graphics_context_set_fill_color(ctx, BODY_LIGHT);
  graphics_fill_rect(ctx, head_hi, 7, GCornersTop);
  // small shine
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(head.origin.x + 9, head.origin.y + 5), 2);

  // ---- visor housing the eyes ----
  GRect visor = GRect(head.origin.x + 4, head.origin.y + 9, head_w - 7, 12);
  graphics_context_set_fill_color(ctx, VISOR_COLOR);
  graphics_fill_rect(ctx, visor, 6, GCornersAll);

  // ---- eyes ----
  int eye_w = 9, eye_h_open = 10, eye_h = eye_h_open;
  if (s_blink) eye_h = 2;
  else if (s_state == ROBOT_GOAL_REACHED || s_show_speech || s_show_smile) eye_h = 11;
  else if (is_idle_family(s_state) && mood == WEATHER_MOOD_SUN) eye_h = 4;
  else if (is_idle_family(s_state) && mood == WEATHER_MOOD_RAIN) eye_h = 6;

  int eye_y = visor.origin.y + (visor.size.h - eye_h) / 2;
  int eye_gap = 6;
  GRect left_eye = GRect(cx - eye_gap / 2 - eye_w + tilt, eye_y, eye_w, eye_h);
  GRect right_eye = GRect(cx + eye_gap / 2 + tilt, eye_y, eye_w, eye_h);

  graphics_context_set_fill_color(ctx, ACCENT_COLOR);
  graphics_fill_rect(ctx, left_eye, 2, GCornersAll);
  graphics_fill_rect(ctx, right_eye, 2, GCornersAll);

  // ---- mouth: a curved smile arc for the smile reaction, otherwise the
  // usual flat line (wider when talking or celebrating) ----
  if (!s_blink && s_show_smile) {
    GRect smile_rect = GRect(cx - 6 + tilt, head.origin.y + head_h - 12, 12, 10);
    graphics_context_set_stroke_color(ctx, ACCENT_DIM);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_arc(ctx, smile_rect, GOvalScaleModeFitCircle,
                       DEG_TO_TRIGANGLE(120), DEG_TO_TRIGANGLE(240));
  } else if (!s_blink) {
    int mouth_w = (s_state == ROBOT_GOAL_REACHED || s_show_speech) ? 14 : 9;
    GRect mouth = GRect(cx - mouth_w / 2 + tilt, head.origin.y + head_h - 6, mouth_w, 1);
    graphics_context_set_fill_color(ctx, ACCENT_DIM);
    graphics_fill_rect(ctx, mouth, 1, GCornersAll);
  }

  // ---- torso: same shaded-panel treatment as the head ----
  int torso_w = 31, torso_h = 21;
  GRect torso = GRect(cx - torso_w / 2 + tilt, head.origin.y + head_h - 1, torso_w, torso_h);
  graphics_context_set_fill_color(ctx, BODY_DARK);
  graphics_fill_rect(ctx, torso, 8, GCornersAll);
  GRect torso_hi = GRect(torso.origin.x + 1, torso.origin.y + 1, torso_w - 2, torso_h * 3 / 5);
  graphics_context_set_fill_color(ctx, BODY_LIGHT);
  graphics_fill_rect(ctx, torso_hi, 6, GCornersTop);

  // chest panel + light (dim halo behind a bright core, no true blur on
  // e-paper so this is faked with two flat circles)
  GRect chest_panel = GRect(torso.origin.x + torso_w / 2 - 10, torso.origin.y + 3, 20, 7);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, chest_panel, 4, GCornersAll);
  graphics_context_set_fill_color(ctx, ACCENT_DIM);
  graphics_fill_circle(ctx, GPoint(torso.origin.x + torso_w / 2, torso.origin.y + 8), 4);
  graphics_context_set_fill_color(ctx, ACCENT_COLOR);
  graphics_fill_circle(ctx, GPoint(torso.origin.x + torso_w / 2, torso.origin.y + 8), 2);

  // vent marks
  graphics_context_set_fill_color(ctx, BODY_DARK);
  graphics_fill_rect(ctx, GRect(torso.origin.x + 6, torso.origin.y + torso_h - 5, 4, 1), 1, GCornersAll);
  graphics_fill_rect(ctx, GRect(torso.origin.x + torso_w - 11, torso.origin.y + torso_h - 5, 4, 1), 1, GCornersAll);

  // ---- arms ----
  int arm_w = 6, arm_h = 16;
  GRect left_arm = GRect(torso.origin.x - arm_w - 1, torso.origin.y + 2 + arm_swing, arm_w, arm_h);
  GRect right_arm = GRect(torso.origin.x + torso_w + 1, torso.origin.y + 2 - arm_swing, arm_w, arm_h);
  draw_limb(ctx, left_arm);
  draw_limb(ctx, right_arm);
  // shoulder joint balls sit at the torso attachment point, not the limb
  // itself, so they read as a fixed pivot even while the arm swings
  graphics_context_set_fill_color(ctx, BODY_MID);
  graphics_fill_circle(ctx, GPoint(torso.origin.x - 1, torso.origin.y + 3), 3);
  graphics_fill_circle(ctx, GPoint(torso.origin.x + torso_w + 1, torso.origin.y + 3), 3);

  // ---- legs ----
  int leg_w = 7, leg_h = 15, leg_gap = 4;
  GRect left_leg = GRect(cx - leg_gap / 2 - leg_w + tilt + leg_swing,
                          torso.origin.y + torso_h - 1, leg_w, leg_h);
  GRect right_leg = GRect(cx + leg_gap / 2 + tilt - leg_swing,
                           torso.origin.y + torso_h - 1, leg_w, leg_h);
  draw_limb(ctx, left_leg);
  draw_limb(ctx, right_leg);
  graphics_context_set_fill_color(ctx, BODY_MID);
  graphics_fill_circle(ctx, GPoint(cx - leg_gap / 2 - leg_w / 2 + tilt, torso.origin.y + torso_h + 1), 3);
  graphics_fill_circle(ctx, GPoint(cx + leg_gap / 2 + leg_w / 2 + tilt, torso.origin.y + torso_h + 1), 3);

  // ---- state decorations ----
  if (s_state == ROBOT_GOAL_REACHED) draw_confetti(ctx, head);
  if (s_state == ROBOT_READING) draw_book(ctx, torso);
  if (s_state == ROBOT_WORKING) draw_laptop(ctx, torso, s_anim_phase);
  if (s_state == ROBOT_EATING) draw_snack(ctx, head);
  if (s_state == ROBOT_CYCLING) draw_bicycle(ctx, torso, bounds);
  draw_weather_icon(ctx, bounds);
  if (s_show_speech) draw_speech_bubble(ctx, head, bounds, s_speech_text);
}

// ---------------- TIME / DATE ----------------
static void update_time(struct tm *tick_time) {
  strftime(s_time_buffer, sizeof(s_time_buffer),
            clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  strftime(s_date_buffer, sizeof(s_date_buffer), "%a, %b %d", tick_time);
  text_layer_set_text(s_date_layer, s_date_buffer);

  if (tick_time->tm_hour == 0 && tick_time->tm_min == 0) {
    s_goal_celebrated_today = false;
  }
}

// ---------------- STEPS ----------------
// Custom-drawn (not a TextLayer) so it can show a pixel-filled progress bar
// above the step count instead of a "(NN%)" suffix.
static void steps_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  int bar_w = bounds.size.w - 50;
  int bar_h = 6;
  int bar_x = bounds.origin.x + (bounds.size.w - bar_w) / 2;
  int bar_y = bounds.origin.y + 1;
  GRect bar = GRect(bar_x, bar_y, bar_w, bar_h);

  graphics_context_set_stroke_color(ctx, ACCENT_DIM);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, bar, 3);

  int pct = (s_steps_count * 100) / STEP_GOAL;
  if (pct > 100) pct = 100;
  int fill_w = ((bar_w - 2) * pct) / 100;
  if (fill_w > 0) {
    GRect fill = GRect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2);
    graphics_context_set_fill_color(ctx, ACCENT_COLOR);
    graphics_fill_rect(ctx, fill, 2, GCornersAll);
  }

  GRect text_rect = GRect(bounds.origin.x, bar_y + bar_h + 1, bounds.size.w,
                           bounds.size.h - bar_h - 1);
  graphics_context_set_text_color(ctx, ACCENT_COLOR);
  graphics_draw_text(ctx, s_steps_buffer, s_steps_font, text_rect,
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void update_steps(void) {
  if (!health_service_metric_accessible(HealthMetricStepCount,
        time_start_of_today(), time(NULL))) {
    s_steps_count = 0;
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "Steps: n/a");
    layer_mark_dirty(s_steps_layer);
    return;
  }

  HealthValue steps = health_service_sum_today(HealthMetricStepCount);
  s_steps_count = (int)steps;

  if (steps >= STEP_GOAL) {
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%d steps - Goal! \xE2\x9C\x93", (int)steps);
  } else {
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%d steps", (int)steps);
  }
  layer_mark_dirty(s_steps_layer);

  evaluate_state();
}

// ---------------- HEART RATE ----------------
static void update_heart_rate(void) {
  HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
  s_heart_rate = (int)bpm;

  if (s_heart_rate > 0) {
    snprintf(s_heart_buffer, sizeof(s_heart_buffer), "%d", s_heart_rate);
  } else {
    snprintf(s_heart_buffer, sizeof(s_heart_buffer), "--");
  }
  layer_mark_dirty(s_weather_layer);
}

static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSignificantUpdate || event == HealthEventMovementUpdate) {
    update_steps();
  }
  if (event == HealthEventHeartRateUpdate) {
    update_heart_rate();
  }
}

// Small pixel heart: two round lobes plus a filled triangle for the point.
static void draw_heart(GContext *ctx, GPoint center, int size, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  int r = size / 3;
  graphics_fill_circle(ctx, GPoint(center.x - r, center.y - r / 2), r + 1);
  graphics_fill_circle(ctx, GPoint(center.x + r, center.y - r / 2), r + 1);

  GPoint tri[3] = {
    GPoint(center.x - size / 2, center.y - size / 10),
    GPoint(center.x + size / 2, center.y - size / 10),
    GPoint(center.x, center.y + size / 2)
  };
  GPathInfo info = { .num_points = 3, .points = tri };
  GPath *path = gpath_create(&info);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// Weather temperature + persistent heart-rate readout, sharing one row.
// Heart-rate: green normally, red at/above HEART_RATE_HIGH, dim gray "--"
// when no reading is available yet (e.g. watch not snug, or emulator with
// no HRM sensor).
static void weather_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int mid_y = bounds.origin.y + bounds.size.h / 2;

  GSize temp_size = graphics_text_layout_get_content_size(
      s_weather_buffer, s_small_font, GRect(0, 0, bounds.size.w, bounds.size.h),
      GTextOverflowModeFill, GTextAlignmentLeft);
  GSize heart_num_size = graphics_text_layout_get_content_size(
      s_heart_buffer, s_steps_font, GRect(0, 0, bounds.size.w, bounds.size.h),
      GTextOverflowModeFill, GTextAlignmentLeft);

  int heart_size = 14;
  int gap = 8;
  int total_w = temp_size.w + gap + heart_size + 6 + heart_num_size.w;
  int start_x = bounds.origin.x + (bounds.size.w - total_w) / 2;

  GRect temp_rect = GRect(start_x, bounds.origin.y, temp_size.w, bounds.size.h);
  graphics_context_set_text_color(ctx, ACCENT_COLOR);
  graphics_draw_text(ctx, s_weather_buffer, s_small_font, temp_rect,
                      GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  GColor heart_color = ACCENT_DIM;
  if (s_heart_rate > 0) {
    heart_color = (s_heart_rate >= HEART_RATE_HIGH) ? GColorRed : GColorGreen;
  }
  GPoint heart_center = GPoint(start_x + temp_size.w + gap + heart_size / 2, mid_y);
  draw_heart(ctx, heart_center, heart_size, heart_color);

  GRect num_rect = GRect(heart_center.x + heart_size / 2 + 6, bounds.origin.y,
                          heart_num_size.w, bounds.size.h);
  graphics_context_set_text_color(ctx, heart_color);
  graphics_draw_text(ctx, s_heart_buffer, s_steps_font, num_rect,
                      GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// ---------------- BLINK / SMILE ----------------
static void blink_timer_callback(void *data) {
  s_blink = false;
  layer_mark_dirty(s_robot_layer);
}

static void idle_smile_timer_callback(void *data) {
  s_show_smile = false;
  layer_mark_dirty(s_robot_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);

  if (units_changed & MINUTE_UNIT) update_steps();

  // Small idle-family micro-expressions: mostly a blink, occasionally a
  // smile instead, so the robot doesn't look frozen between activities.
  if (is_idle_family(s_state) && (tick_time->tm_sec % 9) == 0 && (rand() % 3 == 0)) {
    if (rand() % 4 == 0) {
      s_show_smile = true;
      layer_mark_dirty(s_robot_layer);
      app_timer_register(1500, idle_smile_timer_callback, NULL);
    } else {
      s_blink = true;
      layer_mark_dirty(s_robot_layer);
      app_timer_register(180, blink_timer_callback, NULL);
    }
  }

  if (is_special_time()) {
    s_state = ROBOT_GOAL_REACHED;
    s_anim_phase = 0;
  }

  // tick_handler runs every SECOND_UNIT tick, so this must also check
  // tm_sec - otherwise it re-sends REQUEST_WEATHER on every one of the 60
  // seconds within minute :00 and :30, flooding the AppMessage outbox.
  if (tick_time->tm_min % 30 == 0 && tick_time->tm_sec == 0) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
      app_message_outbox_send();
    }
  }

  if (is_idle_family(s_state) || s_state == ROBOT_SLEEPY) {
    evaluate_state();
  }

  // Reroll which idle activity is showing every so often, rather than on
  // every evaluate_state() call above (which would make it flicker). A
  // countdown of 0 (fresh arrival into idle, or right after a celebration)
  // rerolls immediately instead of waiting out a stale countdown.
  if (is_idle_family(s_state)) {
    if (s_idle_activity_countdown <= 0) {
      pick_idle_activity();
    } else {
      s_idle_activity_countdown--;
    }
  }
}

// ---------------- SPEECH BUBBLE / SMILE ----------------
static void speech_hide_callback(void *data) {
  s_show_speech = false;
  s_show_smile = false;
  layer_mark_dirty(s_robot_layer);
}

static void show_speech_text(const char *text) {
  s_speech_text = text;
  s_show_speech = true;
  s_show_smile = false;
  s_blink = false;
  layer_mark_dirty(s_robot_layer);

  if (s_speech_timer) app_timer_cancel(s_speech_timer);
  s_speech_timer = app_timer_register(SPEECH_DURATION_MS, speech_hide_callback, NULL);
}

// Shake reaction: pick a random greeting word and show it in a speech
// bubble alongside a wave.
static const char *const s_greetings[] = { "Hi!", "Hello!", "Hoi!" };

static void trigger_speech(void) {
  show_speech_text(s_greetings[rand() % (sizeof(s_greetings) / sizeof(s_greetings[0]))]);
}

// Small unprompted remarks for when the robot settles into a new idle
// activity - not shown every time (see pick_idle_activity()), just often
// enough that it doesn't feel scripted.
static const char *const s_reading_phrases[] = { "hmm...", "good part!" };
static const char *const s_working_phrases[] = { "busy...", "almost done" };
static const char *const s_eating_phrases[] = { "yum!", "so good" };
static const char *const s_cycling_phrases[] = { "let's ride!", "wheee!" };
static const char *const s_away_phrases[] = { "brb!", "be right back" };

// Called by tick_handler roughly every 10-19s while nothing else is going
// on (walking/weather/etc. all take priority in evaluate_state()). Picks a
// time-of-day-appropriate activity - lunch hours favor eating, the rest of
// the working day favors reading/working, otherwise it's a neutral idle -
// and occasionally has the robot comment on it.
static void pick_idle_activity(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int hour = t->tm_hour;

  // Every reroll also picks a new spot to wander to, so the robot drifts
  // sideways over time instead of sitting frozen in the center - even when
  // the activity itself doesn't change.
  s_wander_target = (rand() % 101) - 50;

  RobotState pool[8];
  int n = 0;
  pool[n++] = ROBOT_IDLE;
  pool[n++] = ROBOT_IDLE;
  if (hour >= 12 && hour < 14) {
    pool[n++] = ROBOT_EATING;
  } else if (hour >= 9 && hour < 18) {
    pool[n++] = ROBOT_READING;
    pool[n++] = ROBOT_WORKING;
  } else {
    pool[n++] = ROBOT_READING;
  }
  pool[n++] = ROBOT_CYCLING;
  pool[n++] = ROBOT_AWAY;

  RobotState next = pool[rand() % n];
  if (next != s_state) {
    s_state = next;
    s_anim_phase = 0;
    layer_mark_dirty(s_robot_layer);

    if (rand() % 5 < 2) {
      const char *phrase = NULL;
      if (next == ROBOT_READING) {
        phrase = s_reading_phrases[rand() % (sizeof(s_reading_phrases) / sizeof(s_reading_phrases[0]))];
      } else if (next == ROBOT_WORKING) {
        phrase = s_working_phrases[rand() % (sizeof(s_working_phrases) / sizeof(s_working_phrases[0]))];
      } else if (next == ROBOT_EATING) {
        phrase = s_eating_phrases[rand() % (sizeof(s_eating_phrases) / sizeof(s_eating_phrases[0]))];
      } else if (next == ROBOT_CYCLING) {
        phrase = s_cycling_phrases[rand() % (sizeof(s_cycling_phrases) / sizeof(s_cycling_phrases[0]))];
      } else if (next == ROBOT_AWAY) {
        phrase = s_away_phrases[rand() % (sizeof(s_away_phrases) / sizeof(s_away_phrases[0]))];
      }
      if (phrase) show_speech_text(phrase);
    }
  }
  s_idle_activity_countdown = 10 + rand() % 10;
}

// ---------------- TAP ----------------
static void bounce_reset_callback(void *data) {
  s_bounce = false;
  layer_mark_dirty(s_robot_layer);
}

// The OS's built-in tap detector needs a wrist-shake-level hit to fire on
// this hardware (a normal case-tap never registers - confirmed by testing).
// Rather than chase tap sensitivity further, a shake is now the intended
// gesture: any registered tap goes straight to the greeting, no double-tap
// timing window.
static void tap_handler(AccelAxisType axis, int32_t direction) {
  // The OS's own motion-wake gesture doesn't always catch a tap on the
  // case, which made the watch look unresponsive - force the backlight on
  // whenever the app itself sees a tap, so it's never relying on that.
  light_enable_interaction();

  s_bounce = true;
  layer_mark_dirty(s_robot_layer);
  app_timer_register(350, bounce_reset_callback, NULL);

  trigger_speech();
}

// ---------------- APPMESSAGE (weather) ----------------
static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *temp_tuple = dict_find(iter, MESSAGE_KEY_TEMPERATURE);
  Tuple *cond_tuple = dict_find(iter, MESSAGE_KEY_CONDITIONS);

  if (temp_tuple) {
    snprintf(s_weather_buffer, sizeof(s_weather_buffer), "%d\xC2\xB0",
             (int)temp_tuple->value->int32);
    layer_mark_dirty(s_weather_layer);
  }
  if (cond_tuple) {
    strncpy(s_conditions, cond_tuple->value->cstring, sizeof(s_conditions) - 1);
    s_conditions[sizeof(s_conditions) - 1] = '\0';
  }

  evaluate_state();
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {}

static void request_weather(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
    app_message_outbox_send();
  }
}

// ---------------- WINDOW LOAD ----------------
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, BG_COLOR);

  s_time_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  s_small_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_speech_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_steps_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  s_robot_layer = layer_create(GRect(0, 4, bounds.size.w, 76));
  layer_set_update_proc(s_robot_layer, robot_layer_update_proc);
  layer_add_child(window_layer, s_robot_layer);

  s_time_layer = text_layer_create(GRect(0, 80, bounds.size.w, 48));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, TEXT_COLOR);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  // Steps, weather+heart, and the combined day/date line share the
  // remaining 100px below the time - day and date used to be separate
  // rows, freeing space for the heart-rate readout added alongside weather.
  s_steps_layer = layer_create(GRect(0, 128, bounds.size.w, 28));
  layer_set_update_proc(s_steps_layer, steps_layer_update_proc);
  layer_add_child(window_layer, s_steps_layer);

  s_weather_layer = layer_create(GRect(0, 157, bounds.size.w, 34));
  layer_set_update_proc(s_weather_layer, weather_layer_update_proc);
  layer_add_child(window_layer, s_weather_layer);

  s_date_layer = text_layer_create(GRect(0, 194, bounds.size.w, 28));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorLightGray);
  text_layer_set_font(s_date_layer, s_small_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);
  update_time(tick_time);
  update_steps();
  update_heart_rate();

  s_anim_timer = app_timer_register(600, anim_timer_callback, NULL);
}

static void window_unload(Window *window) {
  if (s_anim_timer) app_timer_cancel(s_anim_timer);
  if (s_speech_timer) app_timer_cancel(s_speech_timer);
  layer_destroy(s_robot_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  layer_destroy(s_steps_layer);
  layer_destroy(s_weather_layer);
}

// ---------------- INIT ----------------
static void init(void) {
  srand(time(NULL));

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  health_service_events_subscribe(health_handler, NULL);
  health_service_set_heart_rate_sample_period(60);
  accel_tap_service_subscribe(tap_handler);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  request_weather();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();
  accel_tap_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
