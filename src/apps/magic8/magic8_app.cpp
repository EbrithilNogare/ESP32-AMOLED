#include "magic8_app.h"

#include <Arduino.h>
#include <esp_random.h>
#include <math.h>
#include <string.h>

#include "board.h"
#include "config.h"
#include "core/imu.h"
#include "core/store.h"
#include "core/ticks.h"
#include "ui/draw.h"
#include "ui/theme.h"

namespace apps {

static const AppInfo kInfo = {"magic8", "Magic 8", ICON_COUNTER_8, MAGIC8_COLOR};
static constexpr char kParkKey[] = "magic8";

// Stage layout (screen coordinates; the stage fills the root).
static constexpr float kCx = LCD_WIDTH / 2.0f, kCy = 206, kR = 150;
static constexpr int32_t kCaptionY = 404;
static constexpr uint32_t kTapShakeMs = 450;
static constexpr uint32_t kHighlight = 0xFFB8C4;

Magic8App::Magic8App() : shake_({MAGIC8_SHAKE_G, 3, 800, MAGIC8_SETTLE_MS}) {}

const AppInfo &Magic8App::info() const { return kInfo; }

void Magic8App::onWake(Wake wake) {
  Parked p;
  if (wake == Wake::Cold || !core::store::unpark(kParkKey, &p, sizeof p)) return;
  if (p.answer >= 0 && p.answer < eightball::kAnswers) answer_ = p.answer;
  phase_ = p.showing && answer_ >= 0 ? Phase::Answer : Phase::Idle;
}

void Magic8App::onSleep(sleepplan::Kind kind) {
  (void)kind;
  // A shake in progress is dropped: wake up to the last answer, or idle.
  const bool showing = phase_ == Phase::Answer || phase_ == Phase::Revealing;
  const Parked p = {int8_t(answer_), uint8_t(showing && answer_ >= 0)};
  core::store::park(kParkKey, &p, sizeof p);
}

void Magic8App::onCreate(lv_obj_t *root) {
  stage_ = ui::box(root);
  lv_obj_set_size(stage_, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_clickable(stage_, true);
  lv_obj_add_event_cb(stage_, onStageDraw, LV_EVENT_DRAW_MAIN, this);
  lv_obj_add_event_cb(stage_, onStageClick, LV_EVENT_CLICKED, this);
  if (phase_ == Phase::Shaking || phase_ == Phase::Revealing) phase_ = answer_ >= 0 ? Phase::Answer : Phase::Idle;
  shake_.reset();
  core::imu::enable(true);
  nextSampleMs_ = nextFrameMs_ = millis();
}

void Magic8App::onDestroy() {
  core::imu::enable(false);
  stage_ = nullptr;
}

void Magic8App::onScreen(bool on) {
  core::imu::enable(on);
  shake_.reset();
  if (!on && phase_ == Phase::Shaking) setPhase(answer_ >= 0 ? Phase::Answer : Phase::Idle, millis());
}

void Magic8App::onStageClick(lv_event_t *e) { static_cast<Magic8App *>(lv_event_get_user_data(e))->ask(millis()); }

void Magic8App::ask(uint32_t nowMs) {
  if (phase_ == Phase::Shaking) return;
  tapped_ = true;
  settleAtMs_ = nowMs + kTapShakeMs;
  setPhase(Phase::Shaking, nowMs);
}

void Magic8App::setPhase(Phase p, uint32_t nowMs) {
  if (p == Phase::Revealing) answer_ = eightball::pick(esp_random(), answer_);
  if (p != Phase::Shaking) tapped_ = false;
  phase_ = p;
  phaseMs_ = nowMs;
  if (stage_) lv_obj_invalidate(stage_);
}

void Magic8App::sample(uint32_t nowMs) {
  level::Accel a;
  if (!core::imu::read(a)) return;
  switch (shake_.update(a.x, a.y, a.z, nowMs)) {
    case eightball::Shake::Event::Started:
      tapped_ = false;
      setPhase(Phase::Shaking, nowMs);
      break;
    case eightball::Shake::Event::Settled:
      if (phase_ == Phase::Shaking && !tapped_) setPhase(Phase::Revealing, nowMs);
      break;
    case eightball::Shake::Event::None: break;
  }
}

void Magic8App::onTick(uint32_t nowMs) {
  if (!stage_) return;
  if (core::imu::present() && core::reached(nowMs, nextSampleMs_)) {
    nextSampleMs_ = nowMs + MAGIC8_SAMPLE_MS;
    sample(nowMs);
  }
  if (phase_ == Phase::Shaking && tapped_ && core::reached(nowMs, settleAtMs_)) setPhase(Phase::Revealing, nowMs);
  if (phase_ == Phase::Revealing && nowMs - phaseMs_ >= MAGIC8_REVEAL_MS) setPhase(Phase::Answer, nowMs);
  const bool moving = phase_ == Phase::Shaking || phase_ == Phase::Revealing;
  if (moving && core::reached(nowMs, nextFrameMs_)) {
    nextFrameMs_ = nowMs + 1000 / MAGIC8_FPS;
    lv_obj_invalidate(stage_);
  }
}

void Magic8App::onStageDraw(lv_event_t *e) {
  static_cast<Magic8App *>(lv_event_get_user_data(e))->draw(lv_event_get_layer(e));
}

static float easeOutCubic(float t) {
  const float u = 1 - t;
  return 1 - u * u * u;
}

// The answer's lines, centred on (cx, cy), one draw::text per line.
static void drawLines(lv_layer_t *layer, const char *text, float cx, float cy, lv_opa_t opa) {
  char buf[32];
  strlcpy(buf, text, sizeof buf);
  const char *lines[3];
  int n = 0;
  for (char *p = buf, *tok; n < 3 && (tok = strsep(&p, "\n"));) lines[n++] = tok;
  const float lh = 36;
  for (int i = 0; i < n; i++)
    ui::draw::text(layer, &font_marker_30, ui::col::text, opa, lines[i], int32_t(cx),
                   int32_t(cy + (i - (n - 1) / 2.0f) * lh));
}

void Magic8App::draw(lv_layer_t *layer) {
  using namespace ui::draw;
  lv_area_t a;
  lv_obj_get_coords(stage_, &a);  // follows pixel shift
  const float ox = float(a.x1), oy = float(a.y1);
  const uint32_t now = millis();
  const uint32_t t = now - phaseMs_;

  // The ball follows the shake: a jitter that fades as the shake goes on.
  float dx = 0, dy = 0;
  if (phase_ == Phase::Shaking) {
    const float amp = 7.0f * expf(-float(t) / 900.0f) + 2.0f;
    dx = amp * sinf(float(t) * 0.047f);
    dy = 0.7f * amp * sinf(float(t) * 0.061f + 1.0f);
  }
  const float cx = ox + kCx + dx, cy = oy + kCy + dy;

  // A red halo around the ball, as thin concentric washes (fewer, stronger
  // steps band visibly), kept within the screen's width so it stays round.
  for (int i = 0; i < 9; i++) circle(layer, ox + kCx, oy + kCy, kR + 32 - i * 4, MAGIC8_GLOW, 34);

  // The ball: near-black with a faint rim, and a soft highlight top left,
  // tinted by the red glow (a neutral grey this dark turns green in RGB565).
  circle(layer, cx, cy, kR, 0x0C080A, LV_OPA_COVER);
  ring(layer, cx, cy, kR - 1, 2, 0x4A2A2E, LV_OPA_60);
  circle(layer, cx - 52, cy - 64, 46, kHighlight, 12);
  circle(layer, cx - 58, cy - 72, 26, kHighlight, 18);
  circle(layer, cx - 62, cy - 78, 10, kHighlight, 44);

  const char *caption = core::imu::present() ? "ASK. THEN SHAKE." : "TAP THE BALL";
  switch (phase_) {
    case Phase::Idle:
      circle(layer, cx, cy, 64, ui::col::text, LV_OPA_COVER);
      text(layer, &font_marker_96, ui::col::bg, LV_OPA_COVER, "8", int32_t(cx), int32_t(cy + 4));
      break;
    case Phase::Shaking: caption = ". . ."; break;
    case Phase::Revealing:
    case Phase::Answer: {
      // The answer surfaces: fades in, grows from 85 % and rises 18 px.
      const float p = phase_ == Phase::Answer ? 1.0f : easeOutCubic(fminf(1.0f, float(t) / MAGIC8_REVEAL_MS));
      const lv_opa_t opa = lv_opa_t(255 * p);
      const float rise = 18 * (1 - p);
      lv_draw_image_dsc_t d;
      lv_draw_image_dsc_init(&d);
      d.src = &img_brush;
      d.recolor = ui::c(MAGIC8_COLOR);
      d.recolor_opa = LV_OPA_COVER;
      d.opa = opa;
      const int32_t w = img_brush.header.w, h = img_brush.header.h;
      d.scale_x = d.scale_y = int32_t(LV_SCALE_NONE * (0.85f + 0.15f * p));
      d.pivot = {w / 2, h / 2};
      const lv_area_t box = {int32_t(cx) - w / 2, int32_t(cy + rise) - h / 2, int32_t(cx) - w / 2 + w - 1,
                             int32_t(cy + rise) - h / 2 + h - 1};
      lv_draw_image(layer, &d, &box);
      if (answer_ >= 0) drawLines(layer, eightball::answer(answer_).text, cx, cy + rise - 2, opa);
      caption = "ASK AGAIN";
      break;
    }
  }
  text(layer, &font_marker_30, ui::col::textOff, LV_OPA_COVER, caption, int32_t(ox + kCx), int32_t(oy + kCaptionY));
}

}  // namespace apps
