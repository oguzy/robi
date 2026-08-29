#include <pebble.h>

// ---------- CONFIG ----------
#define STEP_GOAL 10000
#define ACCENT_COLOR GColorFromHEX(0x55EFEF)
#define ACCENT_DIM GColorFromHEX(0x2F8F8F)
#define BODY_LIGHT GColorFromHEX(0x8A9096)
#define BODY_MID GColorFromHEX(0x52565C)
#define BODY_DARK GColorFromHEX(0x26282B)
#define VISOR_COLOR GColorFromHEX(0x0C1012)
#define BG_COLOR GColorBlack
#define TEXT_COLOR GColorWhite
#define DOUBLE_TAP_WINDOW_MS 400
#define SPEECH_DURATION_MS 1800

enum {
  KEY_TEMPERATURE = 0,
  KEY_CONDITIONS = 1,
  KEY_WEATHER_ICON = 2,
  KEY_REQUEST_WEATHER = 3
};

typedef enum {
  ROBOT_IDLE,
  ROBOT_WALKING,
  ROBOT_RUNNING,
  ROBOT_STAIRS_UP,
  ROBOT_STAIRS_DOWN,
  ROBOT_GOAL_REACHED,
  ROBOT_WEATHER_SUN,
  ROBOT_WEATHER_RAIN,
  ROBOT_WEATHER_COLD,
  ROBOT_SLEEPY
} RobotState;

static Window *s_window;
static Layer *s_robot_layer;
static TextLayer *s_time_layer;
static TextLayer *s_day_layer;
static TextLayer *s_date_layer;
static TextLayer *s_steps_layer;
static TextLayer *s_weather_layer;

static GFont s_time_font;
static GFont s_small_font;
static GFont s_speech_font;

static char s_time_buffer[8];
static char s_day_buffer[12];
static char s_date_buffer[12];
static char s_steps_buffer[32];
static char s_weather_buffer[16] = "--\xC2\xB0";
static char s_conditions[16] = "";

static bool s_blink = false;
static bool s_bounce = false;

static RobotState s_state = ROBOT_IDLE;
static int s_anim_phase = 0;
static bool s_goal_celebrated_today = false;
static AppTimer *s_anim_timer = NULL;

static int16_t s_last_z_avg = 0;
static bool s_accel_streaming = false;

static time_t s_last_tap_sec = 0;
static uint16_t s_last_tap_ms = 0;
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

static bool sample_stairs_direction(int *direction_out) {
  AccelData data;
  if (accel_service_peek(&data) != 0) return false;
  int16_t z_avg = data.z;
  int16_t delta = z_avg - s_last_z_avg;
  s_last_z_avg = z_avg;
  if (delta > 150) { *direction_out = 1; return true; }
  if (delta < -150) { *direction_out = -1; return true; }
  return false;
}

// ---------------- STATE EVALUATION ----------------
static void evaluate_state(void) {
  HealthValue steps = health_service_metric_accessible(HealthMetricStepCount,
        time_start_of_today(), time(NULL))
        ? health_service_sum_today(HealthMetricStepCount) : 0;

  if (steps >= STEP_GOAL && !s_goal_celebrated_today) {
    s_state = ROBOT_GOAL_REACHED;
    s_goal_celebrated_today = true;
    s_anim_phase = 0;
    return;
  }

  HealthActivityMask activities = health_service_peek_current_activities();

  // Raw accelerometer streaming (needed only for the stairs-direction
  // heuristic below) is subscribed just-in-time and dropped the rest of the
  // time. Keeping it running continuously fights the accelerometer's
  // low-power tap-interrupt mode, which is what both the watch's own
  // tap-to-wake gesture and our double-tap greeting rely on - with it
  // subscribed all the time, taps stopped registering once the screen went
  // to sleep.
  bool want_accel_streaming = (activities & HealthActivityWalk) != 0;
  if (want_accel_streaming != s_accel_streaming) {
    if (want_accel_streaming) {
      accel_data_service_subscribe(0, NULL);
    } else {
      accel_data_service_unsubscribe();
    }
    s_accel_streaming = want_accel_streaming;
  }

  if (activities & HealthActivityRun) { s_state = ROBOT_RUNNING; return; }

  if (activities & HealthActivityWalk) {
    int dir = 0;
    if (sample_stairs_direction(&dir)) {
      s_state = (dir > 0) ? ROBOT_STAIRS_UP : ROBOT_STAIRS_DOWN;
    } else {
      s_state = ROBOT_WALKING;
    }
    return;
  }

  if (is_night()) { s_state = ROBOT_SLEEPY; return; }

  if (strcmp(s_conditions, "Rain") == 0 || strcmp(s_conditions, "Drizzle") == 0) {
    s_state = ROBOT_WEATHER_RAIN; return;
  }
  if (strcmp(s_conditions, "Snow") == 0) { s_state = ROBOT_WEATHER_COLD; return; }
  if (strcmp(s_conditions, "Clear") == 0) { s_state = ROBOT_WEATHER_SUN; return; }

  s_state = ROBOT_IDLE;
}

// ---------------- ANIMATION TICK ----------------
static void anim_timer_callback(void *data) {
  s_anim_phase++;
  layer_mark_dirty(s_robot_layer);

  if (s_state == ROBOT_GOAL_REACHED && s_anim_phase > 20) {
    s_state = ROBOT_IDLE;
    s_anim_phase = 0;
  }

  bool needs_smooth_anim =
      (s_state == ROBOT_WALKING || s_state == ROBOT_RUNNING ||
       s_state == ROBOT_STAIRS_UP || s_state == ROBOT_STAIRS_DOWN ||
       s_state == ROBOT_GOAL_REACHED || s_state == ROBOT_WEATHER_RAIN ||
       s_state == ROBOT_WEATHER_COLD || s_show_speech);

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

// Staircase for stairs up/down: rather than sliding the whole robot across
// a fixed flight of steps (which looked like a snap-reset once it ran out
// of room), the robot stays put and a diagonal "treadmill" of scrolling
// stripes moves behind it - same trick as the scrolling road, just on a
// diagonal. Reads clearly as climbing/descending and loops seamlessly
// forever with no jump.
static void draw_stairs_scroll_background(GContext *ctx, GRect bounds, bool going_up) {
  int spacing = 18;
  int raw = s_anim_phase * (going_up ? 4 : -4);
  int offset = raw % spacing;
  if (offset < 0) offset += spacing;

  int span = bounds.size.w + bounds.size.h + spacing * 2;

  graphics_context_set_stroke_width(ctx, 5);
  for (int k = -2; k * spacing < span; k++) {
    int d = k * spacing - offset;
    graphics_context_set_stroke_color(ctx, (k % 3 == 0) ? ACCENT_COLOR : GColorDarkGray);
    GPoint p1 = GPoint(bounds.origin.x + d, bounds.origin.y - 6);
    GPoint p2 = GPoint(bounds.origin.x + d - bounds.size.h - 12, bounds.origin.y + bounds.size.h + 6);
    graphics_draw_line(ctx, p1, p2);
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

static void draw_rain(GContext *ctx, GRect head) {
  graphics_context_set_stroke_color(ctx, GColorVividCerulean);
  for (int i = 0; i < 3; i++) {
    int x = head.origin.x + 14 + i * 20;
    int y = head.origin.y - 10 + ((s_anim_phase * 4 + i * 6) % 14);
    graphics_draw_line(ctx, GPoint(x, y), GPoint(x - 2, y + 6));
  }
}

static void draw_sun(GContext *ctx, GRect head) {
  GPoint c = GPoint(head.origin.x + head.size.w - 8, head.origin.y - 4);
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_circle(ctx, c, 5);
  graphics_context_set_stroke_color(ctx, GColorYellow);
  for (int i = 0; i < 6; i++) {
    int32_t angle = (TRIG_MAX_ANGLE / 6) * i;
    GPoint p1 = GPoint(c.x + sin_lookup(angle) * 8 / TRIG_MAX_RATIO,
                        c.y - cos_lookup(angle) * 8 / TRIG_MAX_RATIO);
    GPoint p2 = GPoint(c.x + sin_lookup(angle) * 11 / TRIG_MAX_RATIO,
                        c.y - cos_lookup(angle) * 11 / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, p1, p2);
  }
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

  int bob = 0, tilt = 0, top_shift = 0;
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
    case ROBOT_STAIRS_UP: {
      draw_stairs_scroll_background(ctx, bounds, true);
      tilt = 3;
      top_shift = -(abs(sine_wave(2, TRIG_MAX_ANGLE / 10)));
      leg_swing = sine_wave(3, TRIG_MAX_ANGLE / 10);
      arm_swing = sine_wave(2, TRIG_MAX_ANGLE / 10);
      break;
    }
    case ROBOT_STAIRS_DOWN: {
      draw_stairs_scroll_background(ctx, bounds, false);
      tilt = -3;
      top_shift = -(abs(sine_wave(2, TRIG_MAX_ANGLE / 10)));
      leg_swing = sine_wave(3, TRIG_MAX_ANGLE / 10);
      arm_swing = sine_wave(2, TRIG_MAX_ANGLE / 10);
      break;
    }
    case ROBOT_GOAL_REACHED:
      bob = -(int)(abs(sine_wave(5, TRIG_MAX_ANGLE / 8)));
      arm_swing = -6;
      break;
    case ROBOT_WEATHER_COLD:
      tilt = sine_wave(1, TRIG_MAX_ANGLE / 6);
      break;
    default:
      break;
  }

  if (s_show_speech) arm_swing = -6;
  if (s_bounce) top_shift -= 2;
  int top = bounds.origin.y + top_shift;

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
  else if (s_state == ROBOT_WEATHER_SUN) eye_h = 4;
  else if (s_state == ROBOT_GOAL_REACHED || s_show_speech || s_show_smile) eye_h = 11;
  else if (s_state == ROBOT_WEATHER_RAIN) eye_h = 6;

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
  if (s_state == ROBOT_WEATHER_RAIN) draw_rain(ctx, head);
  if (s_state == ROBOT_WEATHER_SUN) draw_sun(ctx, head);
  if (s_show_speech) draw_speech_bubble(ctx, head, bounds, s_speech_text);
}

// ---------------- TIME / DATE ----------------
static void update_time(struct tm *tick_time) {
  strftime(s_time_buffer, sizeof(s_time_buffer),
            clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  strftime(s_day_buffer, sizeof(s_day_buffer), "%A", tick_time);
  text_layer_set_text(s_day_layer, s_day_buffer);

  strftime(s_date_buffer, sizeof(s_date_buffer), "%b %d", tick_time);
  text_layer_set_text(s_date_layer, s_date_buffer);

  if (tick_time->tm_hour == 0 && tick_time->tm_min == 0) {
    s_goal_celebrated_today = false;
  }
}

// ---------------- STEPS ----------------
static void update_steps(void) {
  if (!health_service_metric_accessible(HealthMetricStepCount,
        time_start_of_today(), time(NULL))) {
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "Steps: n/a");
    text_layer_set_text(s_steps_layer, s_steps_buffer);
    return;
  }

  HealthValue steps = health_service_sum_today(HealthMetricStepCount);
  int pct = (int)((steps * 100) / STEP_GOAL);
  if (pct > 100) pct = 100;

  if (steps >= STEP_GOAL) {
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%d steps - Goal! \xE2\x9C\x93", (int)steps);
  } else {
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%d steps (%d%%)", (int)steps, pct);
  }
  text_layer_set_text(s_steps_layer, s_steps_buffer);

  evaluate_state();
}

static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSignificantUpdate || event == HealthEventMovementUpdate) {
    update_steps();
  }
}

// ---------------- BLINK ----------------
static void blink_timer_callback(void *data) {
  s_blink = false;
  layer_mark_dirty(s_robot_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);

  if (units_changed & MINUTE_UNIT) update_steps();

  if (s_state == ROBOT_IDLE && (tick_time->tm_sec % 9) == 0 && (rand() % 3 == 0)) {
    s_blink = true;
    layer_mark_dirty(s_robot_layer);
    app_timer_register(180, blink_timer_callback, NULL);
  }

  if (is_special_time()) {
    s_state = ROBOT_GOAL_REACHED;
    s_anim_phase = 0;
  }

  if (tick_time->tm_min % 30 == 0) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_uint8(iter, KEY_REQUEST_WEATHER, 1);
      app_message_outbox_send();
    }
  }

  if (s_state == ROBOT_IDLE || s_state == ROBOT_SLEEPY ||
      s_state == ROBOT_WEATHER_SUN || s_state == ROBOT_WEATHER_RAIN ||
      s_state == ROBOT_WEATHER_COLD) {
    evaluate_state();
  }
}

// ---------------- SPEECH BUBBLE / SMILE ----------------
static void speech_hide_callback(void *data) {
  s_show_speech = false;
  s_show_smile = false;
  layer_mark_dirty(s_robot_layer);
}

// Double-tap reaction: pick a random greeting word and show it in a speech
// bubble alongside a wave.
static const char *const s_greetings[] = { "Hi!", "Hello!", "Hoi!" };

static void trigger_speech(void) {
  s_speech_text = s_greetings[rand() % (sizeof(s_greetings) / sizeof(s_greetings[0]))];
  s_show_speech = true;
  s_show_smile = false;
  s_blink = false;
  layer_mark_dirty(s_robot_layer);

  if (s_speech_timer) app_timer_cancel(s_speech_timer);
  s_speech_timer = app_timer_register(SPEECH_DURATION_MS, speech_hide_callback, NULL);
}

// ---------------- TAP ----------------
static void bounce_reset_callback(void *data) {
  s_bounce = false;
  layer_mark_dirty(s_robot_layer);
}

// TEMP DIAGNOSTIC: isolates which branch of the double-tap window check
// fires on real hardware, without relying on the backlight (e-paper stays
// visible either way). Blue = else branch (lone/slow tap, resets the
// window). Red = if branch (fast second tap, calls trigger_speech()).
// Persisted (no timer) so it's checkable after the fact. Remove once the
// double-tap-not-showing bug is understood.
static void tap_handler(AccelAxisType axis, int32_t direction) {
  // The OS's own motion-wake gesture doesn't always catch a tap on the
  // case, which made the watch look unresponsive - force the backlight on
  // whenever the app itself sees a tap, so it's never relying on that.
  light_enable_interaction();

  time_t now_sec;
  uint16_t now_ms;
  time_ms(&now_sec, &now_ms);

  int32_t elapsed_ms = (int32_t)(now_sec - s_last_tap_sec) * 1000 +
                        ((int32_t)now_ms - (int32_t)s_last_tap_ms);

  if (elapsed_ms >= 0 && elapsed_ms < DOUBLE_TAP_WINDOW_MS) {
    window_set_background_color(s_window, GColorRed);
    trigger_speech();
    s_last_tap_sec = 0;
    s_last_tap_ms = 0;
  } else {
    window_set_background_color(s_window, GColorBlue);
    s_last_tap_sec = now_sec;
    s_last_tap_ms = now_ms;

    s_bounce = true;
    s_blink = true;
    layer_mark_dirty(s_robot_layer);
    app_timer_register(200, blink_timer_callback, NULL);
    app_timer_register(350, bounce_reset_callback, NULL);
  }
}

// ---------------- APPMESSAGE (weather) ----------------
static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *temp_tuple = dict_find(iter, KEY_TEMPERATURE);
  Tuple *cond_tuple = dict_find(iter, KEY_CONDITIONS);

  if (temp_tuple) {
    snprintf(s_weather_buffer, sizeof(s_weather_buffer), "%d\xC2\xB0",
             (int)temp_tuple->value->int32);
    text_layer_set_text(s_weather_layer, s_weather_buffer);
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
    dict_write_uint8(iter, KEY_REQUEST_WEATHER, 1);
    app_message_outbox_send();
  }
}

// ---------------- WINDOW LOAD ----------------
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, BG_COLOR);

  s_time_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  s_small_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_speech_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  s_robot_layer = layer_create(GRect(0, 4, bounds.size.w, 76));
  layer_set_update_proc(s_robot_layer, robot_layer_update_proc);
  layer_add_child(window_layer, s_robot_layer);

  s_time_layer = text_layer_create(GRect(0, 80, bounds.size.w, 48));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, TEXT_COLOR);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  // Steps and weather each get the full width on their own line now - at
  // this font size, a half-width column isn't wide enough for something
  // like "8543 steps (85%)" without wrapping into the row below.
  s_steps_layer = text_layer_create(GRect(0, 128, bounds.size.w, 25));
  text_layer_set_background_color(s_steps_layer, GColorClear);
  text_layer_set_text_color(s_steps_layer, ACCENT_COLOR);
  text_layer_set_font(s_steps_layer, s_small_font);
  text_layer_set_text_alignment(s_steps_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_steps_layer));

  s_weather_layer = text_layer_create(GRect(0, 153, bounds.size.w, 25));
  text_layer_set_background_color(s_weather_layer, GColorClear);
  text_layer_set_text_color(s_weather_layer, ACCENT_COLOR);
  text_layer_set_font(s_weather_layer, s_small_font);
  text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);
  text_layer_set_text(s_weather_layer, s_weather_buffer);
  layer_add_child(window_layer, text_layer_get_layer(s_weather_layer));

  s_day_layer = text_layer_create(GRect(0, bounds.size.h - 50, bounds.size.w, 25));
  text_layer_set_background_color(s_day_layer, GColorClear);
  text_layer_set_text_color(s_day_layer, GColorLightGray);
  text_layer_set_font(s_day_layer, s_small_font);
  text_layer_set_text_alignment(s_day_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_day_layer));

  s_date_layer = text_layer_create(GRect(0, bounds.size.h - 25, bounds.size.w, 25));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorLightGray);
  text_layer_set_font(s_date_layer, s_small_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);
  update_time(tick_time);
  update_steps();

  s_anim_timer = app_timer_register(600, anim_timer_callback, NULL);
}

static void window_unload(Window *window) {
  if (s_anim_timer) app_timer_cancel(s_anim_timer);
  if (s_speech_timer) app_timer_cancel(s_speech_timer);
  layer_destroy(s_robot_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_day_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_steps_layer);
  text_layer_destroy(s_weather_layer);
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
  if (s_accel_streaming) accel_data_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
