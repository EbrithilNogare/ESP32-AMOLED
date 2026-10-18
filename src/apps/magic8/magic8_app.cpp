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
static constexpr float kCx = LCD_WIDTH / 2.0f, kTextY = 204;
static constexpr int32_t kLineH = 54, kCaptionY = 404;
static constexpr uint32_t kTapShakeMs = 450;
static constexpr float kRise = 24;  // px the answer rises while it surfaces
static constexpr char kPrompt[] = "ASK ME\nANYTHING";

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

// Overshoots a little past 1 and settles: the answer bobs as it surfaces.
static float easeOutBack(float t) {
  const float c = 1.6f, u = t - 1;
  return 1 + (c + 1) * u * u * u + c * u * u;
}

// Up to three lines, centred on (cx, cy), one draw::text per line.
static void drawLines(lv_layer_t *layer, const char *text, uint32_t color, float cx, float cy, lv_opa_t opa) {
  char buf[40];
  strlcpy(buf, text, sizeof buf);
  const char *lines[3];
  int n = 0;
  for (char *p = buf, *tok; n < 3 && (tok = strsep(&p, "\n"));) lines[n++] = tok;
  for (int i = 0; i < n; i++)
    ui::draw::text(layer, &font_marker_44, color, opa, lines[i], int32_t(cx),
                   int32_t(cy + (i - (n - 1) / 2.0f) * kLineH));
}

void Magic8App::draw(lv_layer_t *layer) {
  lv_area_t a;
  lv_obj_get_coords(stage_, &a);  // follows pixel shift
  const float ox = float(a.x1), oy = float(a.y1);
  const uint32_t t = millis() - phaseMs_;
  const bool imu = core::imu::present();
  const bool answered = answer_ >= 0;
  const eightball::Answer *ans = answered ? &eightball::answer(answer_) : nullptr;

  const char *caption = imu ? "THEN SHAKE" : "THEN TAP";
  switch (phase_) {
    case Phase::Idle: drawLines(layer, kPrompt, ui::col::text, ox + kCx, oy + kTextY, LV_OPA_COVER); break;
    case Phase::Shaking: {
      // What was there rattles sideways and sinks into the dark.
      const float amp = 9.0f * expf(-float(t) / 700.0f) + 3.0f;
      const float dx = amp * sinf(float(t) * 0.05f), dy = 0.4f * amp * sinf(float(t) * 0.067f + 1.0f);
      const float fade = fmaxf(0.25f, 1.0f - float(t) / 500.0f);
      drawLines(layer, ans ? ans->text : kPrompt, ans ? ans->color : ui::col::text, ox + kCx + dx, oy + kTextY + dy,
                lv_opa_t(255 * fade));
      caption = ". . .";
      break;
    }
    case Phase::Revealing:
    case Phase::Answer: {
      const float p = phase_ == Phase::Answer ? 1.0f : fminf(1.0f, float(t) / MAGIC8_REVEAL_MS);
      const lv_opa_t opa = lv_opa_t(255 * fminf(1.0f, p * 1.6f));
      if (ans) drawLines(layer, ans->text, ans->color, ox + kCx, oy + kTextY + kRise * (1 - easeOutBack(p)), opa);
      caption = imu ? "SHAKE AGAIN" : "TAP AGAIN";
      break;
    }
  }
  ui::draw::text(layer, &font_marker_30, ui::col::textOff, LV_OPA_COVER, caption, int32_t(ox + kCx),
                 int32_t(oy + kCaptionY));
}

}  // namespace apps
